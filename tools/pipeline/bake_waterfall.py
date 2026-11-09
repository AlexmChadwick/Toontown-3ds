#!/usr/bin/env python3
"""Bake Acorn Acres' waterfall and geyser (OZSafeZoneLoader.load) to data/zones/6000_*.ttm.

Upstream loads golf_waterfall_model as an Actor at the playground's ``waterfall*`` placer and
loops golf_waterfall at play rate 3.5. Its mesh does not move: the animation only slides the
``uvj_WakeWhiteTide1`` joint, which drives the texture through setTexProjector, by +1.0 in v
per 200 frames (24 fps), so the falling water scrolls. The zone bake never had it (a bare rock
cleft over the pond). The mesh is baked in zone space at frame 0 with the projector's frame-0
offset folded into its UVs; source/world/waterfall.cpp scrolls v at the same rate.

The geyser (golf_geyser_model, play rate 8.6, its projector -5.0 in v per 200 frames) is baked
the same way but in actor space: setGeyserAnim moves and scales the actor every 20 s, which
the runtime replays (the launch of toons standing in it is not reproduced).
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

ZONE = 6000
# name: (placer glob, v per animation frame (checked against the anim), play rate, zone space)
ACTORS = {'waterfall': ('waterfall*', 1.0 / 200, 3.5, True),
          'geyser': ('geyser*', -5.0 / 200, 8.6, False)}


def bake(resources, out):
    from panda3d.core import loadPrcFileData, BamCache
    BamCache.getGlobalPtr().setActive(False)
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                        'textures-power-2 none\nmodel-cache-dir')
    from direct.showbase.ShowBase import ShowBase
    import assemble
    import dna
    import street_catalog
    base = ShowBase()
    store = dna.Storage(base.loader, str(resources))
    _, storages, src = street_catalog.paths(ZONE)
    for s in storages:
        store.load(str(Path(resources) / s))
    tree = dna.parse_file(Path(resources) / src, [])
    geom, _ = assemble.assemble(tree, store, seed=1)
    report = {}
    for name, (glob, per_frame, play_rate, zone_space) in ACTORS.items():
        report[name] = _bake_actor(geom, name, glob, per_frame, play_rate, zone_space, resources, out)
    base.destroy()
    return report


def _bake_actor(geom, name, glob, per_frame, play_rate, zone_space, resources, out):
    from panda3d.core import Filename, NodePath, TextureStage, TransformState
    from direct.actor.Actor import Actor
    import extract
    import ttm
    placer = geom.find('**/' + glob)
    if placer.isEmpty():
        raise RuntimeError(f'no {name} placer')

    def path(rel):
        return Filename.fromOsSpecific(str(Path(resources) / rel))
    actor = Actor(path(f'phase_6/models/golf/golf_{name}_model.bam'), {'idle': path(f'phase_6/models/golf/golf_{name}.bam')})
    root = NodePath('zone')
    actor.reparentTo(root)
    if zone_space:
        actor.setPos(placer.getPos())  # OZSafeZoneLoader: setPos(waterfallPlacer.getPos())
    else:
        actor.setTwoSided(True, 11)    # the geyser: setTwoSided(True, 11)
    joint = actor.exposeJoint(None, 'modelRoot', 'uvj_WakeWhiteTide1')
    bundle = actor.getPartBundle('modelRoot')
    offsets = []
    for frame in (0, 200):
        actor.pose('idle', frame)
        bundle.forceUpdate()
        offsets.append(joint.getMat(actor).getRow3(3))
    rate = (offsets[1][1] - offsets[0][1]) / 200
    if abs(rate - per_frame) > 1e-4 or abs(offsets[1][0] - offsets[0][0]) > 1e-4:
        raise RuntimeError(f'{name} projector moved {offsets}: update the runtime scroll')
    actor.pose('idle', 0)
    bundle.forceUpdate()
    # setTexProjector(stage, joint, actor): the texture matrix is the joint's transform in
    # actor space, a translation; its whole part does not change the repeating texture.
    u, v = offsets[0][0] % 1.0, offsets[0][1] % 1.0
    mesh = actor.find('**/mesh_tide1')
    mesh.setTexTransform(TextureStage.getDefault(), TransformState.makePos2d((u, v)))
    ex = extract.Extractor(max_tex=256)
    ex.add(root, animated=True)
    out.mkdir(parents=True, exist_ok=True)
    stats = ttm.write(str(out / f'{ZONE}_{name}.ttm'), ex.textures, ex.batches())
    if stats['uv_clamped']:
        raise ValueError('UV overflow')
    lo, hi = actor.getTightBounds(root)
    stats['actor_bounds'] = [list(lo), list(hi)]
    stats['uv_offset'] = [u, v]
    stats['scroll_per_second'] = per_frame * 24 * play_rate
    stats['placer'] = list(placer.getPos())
    actor.cleanup()
    root.removeNode()
    return stats


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--resources', default=str(ROOT / 'ref/resources'))
    ap.add_argument('--data', default=str(ROOT / 'sdmc-out/3ds/toontown3ds/data'))
    args = ap.parse_args()
    print(json.dumps(bake(args.resources, Path(args.data) / 'zones')), flush=True)


if __name__ == '__main__':
    main()
