#!/usr/bin/env python3
"""Bake Toontown Central building interiors (PC-identical assembly -> .ttm, .ttc, .pts, .json).

Usage:
    .venv/bin/python -I tools/pipeline/bake_interior.py --zone 2519 --block 19
    .venv/bin/python -I tools/pipeline/bake_interior.py --all-ttc

Outputs written to sdmc-out/3ds/toontown3ds/data/interiors/:
    <zone>.ttm   - GPU geometry with merged batches and ETC1/ETC1A4 textures
    <zone>.ttc   - 3DS collision mesh with floor/wall/camera classifications and triggers
    <zone>.pts   - Named anchor points (doors, triggers, NPC origins)
    <zone>.json  - Sidecar metadata (zone, block, interior type, spawn & teleport points)

Preview PNGs written to sdmc-out/previews/interiors/:
    <zone>_compare.png - Side-by-side comparison (Left: Unbaked Panda, Right: Baked .ttm)
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

DEFAULT_RESOURCES = os.path.join(HERE, '..', '..', 'ref', 'resources')
DEFAULT_OUT_DIR = os.path.join(HERE, '..', '..', 'sdmc-out', '3ds', 'toontown3ds', 'data', 'interiors')
DEFAULT_PREVIEW_DIR = os.path.join(HERE, '..', '..', 'sdmc-out', 'previews', 'interiors')

# Budget constraints from AGENTS.md / AGENT_TASK.md
MAX_BATCHES = 150
MAX_TRIS = 15000


def render_preview_child(zone_id, block, ttm_path, out_png, resources):
    """Child process: offscreen side-by-side render of unbaked assembled vs baked .ttm."""
    from panda3d.core import Filename, PNMImage, loadPrcFileData
    loadPrcFileData('', (
        'window-type offscreen\n'
        'audio-library-name null\n'
        'notify-level warning\n'
        'win-size 400 240\n'
        'default-model-extension .bam\n'
        'textures-power-2 none\n'
    ))
    from direct.showbase.ShowBase import ShowBase
    import interior_dna
    import interior_assemble
    import preview_ttm

    base = ShowBase()
    base.setBackgroundColor(0.25, 0.35, 0.45, 1.0)

    store = interior_dna.InteriorDNAStore(base.loader, resources)
    store.load(os.path.join(resources, 'phase_4/dna/storage.dna'))
    store.load(os.path.join(resources, 'phase_3.5/dna/storage_interior.dna'))
    store.load(os.path.join(resources, 'phase_4/dna/storage_TT.dna'))
    store.load(os.path.join(resources, 'phase_4/dna/storage_TT_sz.dna'))

    root, spec, _ = interior_assemble.assemble_interior(zone_id, block, base.loader, store)
    root.reparentTo(base.render)

    cam = spec.get('camera', (0.0, -2.0, 5.0, 0.0, 14.0, 4.0, 70))
    base.camera.setPos(cam[0], cam[1], cam[2])
    base.camera.lookAt(cam[3], cam[4], cam[5])
    base.camLens.setFov(cam[6])
    base.camLens.setNearFar(0.1, 1000)

    base.graphicsEngine.renderFrame()
    base.graphicsEngine.renderFrame()

    pnm_unbaked = PNMImage()
    base.win.getScreenshot(pnm_unbaked)
    root.detachNode()

    # Load and render baked .ttm
    baked_np = preview_ttm.load(ttm_path, {})
    for b in baked_np.getChildren():
        if b.getBinName() == 'fixed' and not b.hasTransparency():
            b.setBin('opaque', b.getBinDrawOrder())
    baked_np.reparentTo(base.render)

    base.graphicsEngine.renderFrame()
    base.graphicsEngine.renderFrame()

    pnm_baked = PNMImage()
    base.win.getScreenshot(pnm_baked)

    # Combine into side-by-side comparison
    w, h = pnm_unbaked.getXSize(), pnm_unbaked.getYSize()
    compare = PNMImage(w * 2, h, 4)
    compare.copySubImage(pnm_unbaked, 0, 0, 0, 0, w, h)
    compare.copySubImage(pnm_baked, w, 0, 0, 0, w, h)

    os.makedirs(os.path.dirname(os.path.abspath(out_png)), exist_ok=True)
    compare.write(Filename(out_png))


def bake_one(zone_id, block, base, store, resources, out_dir, preview_dir,
             max_tex=128, want_preview=True):
    """Assemble and bake one interior to .ttm, .ttc, .pts, and .json."""
    import assemble
    import collide
    import extract
    import interior_assemble
    import ttm

    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(preview_dir, exist_ok=True)

    # 1. Assemble scene graph
    root, spec, room_code = interior_assemble.assemble_interior(
        zone_id, block, base.loader, store)

    stem = os.path.join(out_dir, str(zone_id))
    ttm_path = stem + '.ttm'
    ttc_path = stem + '.ttc'
    pts_path = stem + '.pts'
    json_path = stem + '.json'
    preview_png = os.path.join(preview_dir, f'{zone_id}_compare.png')

    # 2. Extract geometry and write .ttm
    ex = extract.Extractor(max_tex=max_tex, layer_rules=assemble.LAYER_RULES)
    ex.add(root)
    batches = ex.batches()
    ttm_stats = ttm.write(ttm_path, ex.textures, batches)

    num_batches = len(batches)
    num_tris = ttm_stats['triangles']
    if num_batches > MAX_BATCHES:
        print(f'WARNING: Zone {zone_id} batches ({num_batches}) exceed budget ({MAX_BATCHES})!')
    if num_tris > MAX_TRIS:
        print(f'WARNING: Zone {zone_id} tris ({num_tris}) exceed budget ({MAX_TRIS})!')

    # 3. Extract collision and write .ttc
    mesh = collide.extract(root, fallback_floor='auto')
    ttc_stats = mesh.write(ttc_path, cell_size=8.0, max_bytes=1024 * 1024)

    # 4. Extract anchors and write .pts
    anchors = interior_assemble.extract_anchors(root)
    pts_lines = [f'{name} {x:.3f} {y:.3f} {z:.3f} {h:.2f}' for name, x, y, z, h in anchors]
    with open(pts_path, 'w') as f:
        f.write('\n'.join(pts_lines) + ('\n' if pts_lines else ''))

    # 5. Compute spawn & sidecar metadata and write .json
    meta = interior_assemble.compute_spawn_data(root, zone_id, block, spec)
    meta['roomCode'] = room_code
    meta['stats'] = {
        'tris': num_tris,
        'batches': num_batches,
        'textures': len(ex.textures),
        'ttmBytes': ttm_stats['file_bytes'],
        'ttcBytes': ttc_stats['bytes'],
        'collTris': ttc_stats['tris'],
        'triggers': [t.name for t in mesh.triggers],
    }
    with open(json_path, 'w') as f:
        json.dump(meta, f, indent=2)

    # 6. Render preview in child process if requested
    if want_preview:
        cmd = [
            sys.executable, '-I', os.path.abspath(__file__),
            '--render-preview', str(zone_id), str(block),
            os.path.abspath(ttm_path), os.path.abspath(preview_png),
            os.path.abspath(resources),
        ]
        subprocess.run(cmd, check=True)

    return {
        'zoneId': zone_id,
        'block': block,
        'name': spec['name'],
        'interiorType': spec['interior_type'],
        'roomCode': room_code,
        'batches': num_batches,
        'triangles': num_tris,
        'textures': len(ex.textures),
        'ttmBytes': ttm_stats['file_bytes'],
        'ttcBytes': ttc_stats['bytes'],
        'collTris': ttc_stats['tris'],
        'triggers': [t.name for t in mesh.triggers],
        'anchors': len(anchors),
        'files': {
            'ttm': ttm_path,
            'ttc': ttc_path,
            'pts': pts_path,
            'json': json_path,
            'preview': preview_png if want_preview else None,
        },
    }


def main():
    if len(sys.argv) == 7 and sys.argv[1] == '--render-preview':
        render_preview_child(
            int(sys.argv[2]), int(sys.argv[3]),
            sys.argv[4], sys.argv[5], sys.argv[6],
        )
        return

    ap = argparse.ArgumentParser(description='Bake Toontown Central building interiors.')
    ap.add_argument('--zone', type=int, help='Interior zone ID (e.g. 2519)')
    ap.add_argument('--block', type=int, help='DNA building block number (e.g. 19)')
    ap.add_argument('--all-ttc', action='store_true', help='Bake all 8 TTC playground interiors')
    ap.add_argument('--out-dir', default=DEFAULT_OUT_DIR, help='Output directory for baked data')
    ap.add_argument('--preview-dir', default=DEFAULT_PREVIEW_DIR, help='Output directory for preview PNGs')
    ap.add_argument('--max-tex', type=int, default=128, help='Max texture dimension (default 128)')
    ap.add_argument('--resources', default=DEFAULT_RESOURCES, help='Resources directory path')
    ap.add_argument('--no-preview', action='store_true', help='Skip preview PNG generation')
    args = ap.parse_args()

    import interior_assemble

    if not args.all_ttc and args.zone is None and args.block is None:
        ap.error('Specify --all-ttc or --zone ZONE (and optionally --block BLOCK)')

    targets = []
    if args.all_ttc:
        for zone, spec in sorted(interior_assemble.TTC_INTERIORS.items()):
            targets.append((zone, spec['block']))
    else:
        zone = args.zone
        block = args.block
        if zone is None and block is not None:
            zone = interior_assemble.BLOCK_TO_ZONE.get(block, 2500 + block)
        elif zone is not None and block is None:
            spec = interior_assemble.TTC_INTERIORS.get(zone)
            block = spec['block'] if spec else (zone % 100)
        targets.append((zone, block))

    # Initialize Panda3D headless loader
    from panda3d.core import loadPrcFileData
    loadPrcFileData('', (
        'window-type none\n'
        'audio-library-name null\n'
        'notify-level warning\n'
        'default-model-extension .bam\n'
        'textures-power-2 none\n'
    ))
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()

    # Load storage files
    import interior_dna
    store = interior_dna.InteriorDNAStore(base.loader, args.resources)
    store.load(os.path.join(args.resources, 'phase_4/dna/storage.dna'))
    store.load(os.path.join(args.resources, 'phase_3.5/dna/storage_interior.dna'))
    store.load(os.path.join(args.resources, 'phase_4/dna/storage_TT.dna'))
    store.load(os.path.join(args.resources, 'phase_4/dna/storage_TT_sz.dna'))

    results = []
    print(f'Baking {len(targets)} interior(s)...')
    for zone_id, block_num in targets:
        print(f'  Baking Zone {zone_id} (Block {block_num})...')
        res = bake_one(
            zone_id=zone_id,
            block=block_num,
            base=base,
            store=store,
            resources=args.resources,
            out_dir=args.out_dir,
            preview_dir=args.preview_dir,
            max_tex=args.max_tex,
            want_preview=not args.no_preview,
        )
        results.append(res)
        print(f'    -> {res["name"]} ({res["interiorType"]}): {res["triangles"]} tris, '
              f'{res["batches"]} batches, {res["ttmBytes"]} ttm bytes, '
              f'{res["ttcBytes"]} ttc bytes, {res["anchors"]} anchors')

    print('\nAll bakes complete!')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
