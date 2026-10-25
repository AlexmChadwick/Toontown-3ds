#!/usr/bin/env python3
"""Assemble a DNA zone (storage files + zone file) and bake it to one .ttm.

    .venv/bin/python -I tools/pipeline/bake_zone.py \
        ref/resources/phase_4/dna/storage.dna ref/resources/phase_4/dna/storage_TT.dna \
        ref/resources/phase_4/dna/storage_TT_sz.dna \
        --zone ref/resources/phase_4/dna/toontown_central_sz.dna \
        --out romfs/baked/ttc_sz.ttm [--max-tex 128] [--preview out.png]

Storage paths are given in load order (the game loads storage.dna, then the hood
file, then the safe-zone file). Prints a stats JSON on stdout including codes
that did not resolve and DNA features that were skipped. ``--preview out.png``
writes out_top.png and out_ground.png rendered from the baked file.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
DEFAULT_RESOURCES = os.path.join(HERE, '..', '..', 'ref', 'resources')

# Preview cameras: name -> (x, y, z, look_x, look_y, look_z, fov, width, height)
PREVIEW_VIEWS = {
    'top': (0, -1, 520, 0, 0, 0, 60, 1024, 1024),
    'ground': (-20, -60, 12, 80, 20, 8, 70, 800, 480),
    'west': (20, 10, 14, -120, -40, 6, 70, 800, 480),
}


def render_preview(ttm_path, png, view):
    """Runs in a child process: offscreen render of a .ttm with one camera."""
    x, y, z, lx, ly, lz, fov, w, h = PREVIEW_VIEWS[view]
    from panda3d.core import loadPrcFileData
    import preview_ttm  # sets offscreen prc defaults on import
    loadPrcFileData('', f'win-size {w} {h}')
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()
    base.setBackgroundColor(0.41, 0.69, 0.85, 1)
    model = preview_ttm.load(ttm_path, {})
    model.reparentTo(base.render)
    # preview_ttm puts every layered batch in the 'fixed' bin, i.e. after alpha-blended
    # batches; blended quads that write depth (building drop shadows) then hide the
    # decals behind them. Draw opaque/alpha-test layers with the opaque pass instead
    # (the order extract.py should emit too; see the bake report).
    for b in model.getChildren():
        if b.getBinName() == 'fixed' and not b.hasTransparency():
            b.setBin('opaque', b.getBinDrawOrder())
    base.camera.setPos(x, y, z)
    base.camera.lookAt(lx, ly, lz)
    base.camLens.setFov(fov)
    base.camLens.setNearFar(0.5, 3000)
    base.graphicsEngine.renderFrame()
    base.graphicsEngine.renderFrame()
    base.win.saveScreenshot(png)


# Named anchor nodes the runtime needs: NPC origins (DistributedNPCToonBase parents the NPC
# to render.find('**/npc_origin_<posIndex>')), tunnels (linktunnel_<hood>_<zone>) and
# fishing spots. Text format, one per line: "<name> <x> <y> <z> <h>" in zone space.
POINT_PATTERNS = ['**/npc_*origin_*', '**/party_person_*', '**/linktunnel_*', '**/fishing_spot*']


def write_points(root, path):
    lines = []
    for pattern in POINT_PATTERNS:
        for np_ in root.findAllMatches(pattern):
            p, h = np_.getPos(root), np_.getH(root)
            lines.append(f'{np_.getName()} {p[0]:.3f} {p[1]:.3f} {p[2]:.3f} {h:.2f}')
    # The tunnel tracks (DistributedToon.getTunnelOutToonTrack / getTunnelInToonTrack) run
    # relative to the tunnel model's tunnel_origin node, ~100 units from its DNA root.
    for np_ in root.findAllMatches('**/linktunnel_*'):
        origin = np_.find('**/tunnel_origin')
        if origin.isEmpty():
            continue
        p, h = origin.getPos(root), origin.getH(root)
        lines.append(f'{np_.getName()}_origin {p[0]:.3f} {p[1]:.3f} {p[2]:.3f} {h:.2f}')
    # Building door origins: recorded from door collision triggers so door transitions start at the door
    for np_ in list(root.findAllMatches('**/door_*_trigger')) + list(root.findAllMatches('**/door_trigger_*')):
        p, h = np_.getPos(root), np_.getH(root)
        parent = np_.getParent()
        name = parent.getName() if not parent.isEmpty() else np_.getName()
        if not name.startswith('sz') and not name.startswith('tb'):
            gp = parent.getParent() if not parent.isEmpty() else None
            if gp and not gp.isEmpty() and (gp.getName().startswith('sz') or gp.getName().startswith('tb')):
                name = gp.getName()
            else:
                # Toon HQs have two doors, door_0 and door_1, deeper in the landmark model: name
                # them after their DNA root ("sz20:toon_landmark_hqTT_DNARoot/door_0") so a toon
                # leaving the interior's door N comes out of the matching outside door.
                building = parent
                while not building.isEmpty() and not (building.getName()[:2] in ('sz', 'tb') and
                                                      ':' in building.getName()):
                    building = building.getParent()
                if not building.isEmpty():
                    name = f'{building.getName()}/{parent.getName()}'
                    # Both door_N nodes sit at the model origin; each door is the midpoint of its
                    # leftDoor_N / rightDoor_N leaves, facing away from the leaves' own heading.
                    index = parent.getName().rsplit('_', 1)[-1]
                    left = parent.find(f'**/leftDoor_{index}')
                    right = parent.find(f'**/rightDoor_{index}')
                    if not left.isEmpty() and not right.isEmpty():
                        p = (left.getPos(root) + right.getPos(root)) * 0.5
                        h = left.getH(root) - 180.0

        lines.append(f'{name} {p[0]:.3f} {p[1]:.3f} {p[2]:.3f} {h:.2f}')
        lines.append(f'{name}_door_origin {p[0]:.3f} {p[1]:.3f} {p[2]:.3f} {h:.2f}')
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return len(lines)


def main():
    if len(sys.argv) == 5 and sys.argv[1] == '--render-preview':
        render_preview(sys.argv[2], sys.argv[3], sys.argv[4])
        return

    ap = argparse.ArgumentParser()
    ap.add_argument('storage', nargs='+', help='storage .dna files, in load order')
    ap.add_argument('--zone', required=True, help='zone .dna file')
    ap.add_argument('--out', required=True)
    ap.add_argument('--max-tex', type=int, default=128)
    ap.add_argument('--resources', default=DEFAULT_RESOURCES,
                    help='model-path root for DNA model/texture paths (default ref/resources)')
    ap.add_argument('--seed', type=int, default=1, help='window jitter seed (deterministic)')
    ap.add_argument('--billboards', choices=('cross', 'static'), default='cross',
                    help='billboard sprites: crossed two-sided quads, or frozen as modelled')
    ap.add_argument('--preview', help='write <stem>_top.png / _ground.png / _west.png')
    args = ap.parse_args()

    from panda3d.core import loadPrcFileData
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                    'textures-power-2 none')
    from direct.showbase.ShowBase import ShowBase

    import assemble
    import dna
    import extract
    import ttm

    base = ShowBase()
    store = dna.Storage(base.loader, args.resources)
    for path in args.storage:
        store.load(path)
    zone_warn = []
    zone = dna.parse_file(args.zone, zone_warn)
    root, asm = assemble.assemble(zone, store, seed=args.seed, billboards=args.billboards,
                                  name=os.path.splitext(os.path.basename(args.zone))[0])

    ex = extract.Extractor(max_tex=args.max_tex, layer_rules=assemble.LAYER_RULES)
    ex.add(root)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    stats = ttm.write(args.out, ex.textures, ex.batches())
    stats['points'] = write_points(root, os.path.splitext(args.out)[0] + '.pts')
    stats['collision_nodes'] = root.findAllMatches('**/+CollisionNode').getNumPaths()
    stats['visgroups'] = [np_.getTag(assemble.VISGROUP_TAG)
                          for np_ in root.findAllMatches('**/=' + assemble.VISGROUP_TAG)]
    stats['unresolved_codes'] = dict(sorted(asm.unresolved.items()))
    stats['skipped'] = dict(sorted(asm.skipped.items()))
    stats['billboards_crossed'] = asm.billboards_crossed
    stats['storage_errors'] = store.errors
    stats['zone_warnings'] = zone_warn

    if args.preview:
        stem = os.path.splitext(args.preview)[0]
        stats['previews'] = []
        for view in PREVIEW_VIEWS:
            png = f'{stem}_{view}.png'
            subprocess.run([sys.executable, '-I', os.path.abspath(__file__), '--render-preview',
                            os.path.abspath(args.out), png, view], check=True)
            stats['previews'].append(png)
    print(json.dumps(stats, indent=1))


if __name__ == '__main__':
    main()
