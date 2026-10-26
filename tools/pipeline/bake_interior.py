#!/usr/bin/env python3
"""Bake Toontown Central, playground, and street building interiors.

Assembles upstream PC-identical interiors to .ttm, .ttc, .pts, and .json sidecars.

Usage:
    # Bake everything (all playgrounds and streets: 536 interiors)
    .venv/bin/python -I tools/pipeline/bake_interior.py --all

    # Bake all playground interiors (31 buildings across TTC, DD, BR, MM, DG, DL, GS)
    .venv/bin/python -I tools/pipeline/bake_interior.py --all-hoods

    # Bake all TTC playground interiors (8 buildings)
    .venv/bin/python -I tools/pipeline/bake_interior.py --all-ttc

    # Bake all street interiors (505 buildings)
    .venv/bin/python -I tools/pipeline/bake_interior.py --all-streets

    # Bake by hood or street
    .venv/bin/python -I tools/pipeline/bake_interior.py --hood 1000
    .venv/bin/python -I tools/pipeline/bake_interior.py --street 2100

    # Bake a single interior
    .venv/bin/python -I tools/pipeline/bake_interior.py --zone 1506 --block 6

    # Generate sample preview comparisons (one per hood + shops)
    .venv/bin/python -I tools/pipeline/bake_interior.py --sample-previews

    # The Toontorial's interiors (20501 Toon HQ, 20502 Tutorial Tom's), with the tutorial's own
    # DNA store (docs/design/tutorial.md section 4); not part of --all
    .venv/bin/python -I tools/pipeline/bake_interior.py --tutorial

Outputs written to sdmc-out/3ds/toontown3ds/data/interiors/:
    <zone>.ttm   - GPU geometry with merged batches and ETC1/ETC1A4 textures
    <zone>.ttc   - 3DS collision mesh with floor/wall/camera classifications and triggers
    <zone>.pts   - Named anchor points (doors, triggers, NPC origins)
    <zone>.json  - Sidecar metadata (zone, block, interior type, spawn & teleport points, npcs)

Preview PNGs written to sdmc-out/previews/interiors/:
    <zone>_compare.png - Side-by-side comparison (Left: Unbaked Panda, Right: Baked .ttm)
"""
import argparse
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

DEFAULT_RESOURCES = os.path.join(HERE, '..', '..', 'ref', 'resources')
if not os.path.isdir(os.path.join(DEFAULT_RESOURCES, 'phase_4')):
    DEFAULT_RESOURCES = os.path.join(HERE, '..', '..', 'ref', 'ref', 'resources')
DEFAULT_OUT_DIR = os.path.join(HERE, '..', '..', 'sdmc-out', '3ds', 'toontown3ds', 'data', 'interiors')
DEFAULT_PREVIEW_DIR = os.path.join(HERE, '..', '..', 'sdmc-out', 'previews', 'interiors')

# Budget constraints from AGENTS.md / AGENT_TASK.md
MAX_BATCHES = 150
MAX_TRIS = 15000

# Representative sample set of zones for visual comparison previews (Goal 4)
SAMPLE_PREVIEW_ZONES = [
    # TTC
    2513, 2519, 2520, 2522, 2601,
    # DD
    1506, 1507, 1508, 1510, 1601,
    # BR
    3507, 3508, 3509, 3511, 3601,
    # MM
    4503, 4504, 4506, 4508, 4601,
    # DG
    5501, 5502, 5503, 5505, 5601,
    # DL
    9501, 9504, 9505, 9508, 9601,
    # GS
    8501,
]


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
    # ---- tutorial ----
    tutorial = interior_assemble.is_tutorial_zone(zone_id)
    storage_files = interior_assemble.TUTORIAL_STORAGE_FILES if tutorial else interior_assemble.ALL_STORAGE_FILES
    # ---- end tutorial ----
    for f in storage_files:
        p = os.path.join(resources, f)
        if os.path.exists(p):
            store.load(p)

    if tutorial:
        specs = interior_assemble.get_tutorial_interior_specs(resources)
    else:
        specs = interior_assemble.get_all_interior_specs(resources)
    spec = specs.get(zone_id)

    root, spec, _ = interior_assemble.assemble_interior(zone_id, block, base.loader, store, spec=spec)
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

    # Combine into side-by-side comparison (800x240)
    w, h = pnm_unbaked.getXSize(), pnm_unbaked.getYSize()
    compare = PNMImage(w * 2, h, 4)
    compare.copySubImage(pnm_unbaked, 0, 0, 0, 0, w, h)
    compare.copySubImage(pnm_baked, w, 0, 0, 0, w, h)

    os.makedirs(os.path.dirname(os.path.abspath(out_png)), exist_ok=True)
    compare.write(Filename(out_png))


def _setup_texture_caching():
    """Share encoded textures across interiors without making a bake depend on bake order.

    The cache key is everything the encoded texture depends on (name, wrap modes, size limit,
    ETC1 setting and the pixels themselves), so an interior bakes to the same bytes whether it
    is baked alone or after any others; repeats only skip the costly encode.
    """
    import hashlib
    import extract
    import ttm
    if getattr(extract.Extractor, '_cached_texture_installed', False):
        return
    global_cache = {}
    orig_texture = extract.Extractor._texture

    def cached_texture(self, tex):
        key = tex.getFullpath().getFullpath() or tex.getName()
        if key in self._tex_index:
            return self._tex_index[key]
        if not self.encode_textures:
            return orig_texture(self, tex)
        px, ncomp, pfmt = extract.texture_pixels(tex, self.max_tex)
        wrap_u = extract._WRAP.get(tex.getWrapU(), ttm.GPU_REPEAT)
        wrap_v = extract._WRAP.get(tex.getWrapV(), ttm.GPU_REPEAT)
        content = (tex.getName(), wrap_u, wrap_v, self.max_tex, self.etc1, ncomp, str(pfmt), px.shape,
                   hashlib.sha1(px.tobytes()).hexdigest())
        cached = global_cache.get(content)
        if cached is None:
            fmt = extract.choose_format(px, ncomp, pfmt, etc1=self.etc1)
            cached = ttm.Texture(tex.getName(), px, fmt, wrap_u, wrap_v)
            global_cache[content] = cached
        self._tex_index[key] = len(self.textures)
        self.textures.append(cached)
        return self._tex_index[key]

    extract.Extractor._texture = cached_texture
    extract.Extractor._cached_texture_installed = True


def bake_one(zone_id, block, base, store, resources, out_dir, preview_dir,
             spec=None, max_tex=128, want_preview=False):
    """Assemble and bake one interior to .ttm, .ttc, .pts, and .json."""
    import assemble
    import collide
    import extract
    import interior_assemble
    import ttm

    _setup_texture_caching()
    os.makedirs(out_dir, exist_ok=True)
    if want_preview:
        os.makedirs(preview_dir, exist_ok=True)

    # 1. Assemble scene graph
    root, spec, room_code = interior_assemble.assemble_interior(
        zone_id, block, base.loader, store, spec=spec)

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

    ap = argparse.ArgumentParser(description='Bake Toontown Central, playground, and street building interiors.')
    ap.add_argument('--zone', type=int, help='Interior zone ID (e.g. 2519 or 2601)')
    ap.add_argument('--block', type=int, help='DNA building block number')
    ap.add_argument('--all', action='store_true', help='Bake all 536 interiors across all playgrounds and streets')
    ap.add_argument('--all-hoods', action='store_true', help='Bake all playground interiors across all 7 hoods')
    ap.add_argument('--all-streets', action='store_true', help='Bake all street interiors across all 6 hoods')
    ap.add_argument('--all-ttc', action='store_true', help='Bake all 8 TTC playground interiors')
    ap.add_argument('--hood', type=int, help='Bake all interiors for a specific hood (1000, 2000, 3000, 4000, 5000, 8000, 9000)')
    ap.add_argument('--street', type=int, help='Bake all interiors for a specific street branch (e.g. 2100, 1100)')
    ap.add_argument('--sample-previews', action='store_true', help='Generate visual comparison PNGs for the sample set')
    ap.add_argument('--tutorial', action='store_true', help="Bake the Toontorial's two interiors (20501, 20502)")
    ap.add_argument('--out-dir', default=DEFAULT_OUT_DIR, help='Output directory for baked data')
    ap.add_argument('--preview-dir', default=DEFAULT_PREVIEW_DIR, help='Output directory for preview PNGs')
    ap.add_argument('--max-tex', type=int, default=128, help='Max texture dimension (default 128)')
    ap.add_argument('--resources', default=DEFAULT_RESOURCES, help='Resources directory path')
    ap.add_argument('--no-preview', action='store_true', help='Skip preview PNG generation')
    args = ap.parse_args()

    import interior_assemble

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

    # Load all storage DNA files (the Toontorial has its own store: PlayGame.loadDnaStoreTutorial)
    import interior_dna
    store = interior_dna.InteriorDNAStore(base.loader, args.resources)
    t0 = time.time()
    tutorial = args.tutorial or (args.zone is not None and interior_assemble.is_tutorial_zone(args.zone))
    for f in (interior_assemble.TUTORIAL_STORAGE_FILES if tutorial else interior_assemble.ALL_STORAGE_FILES):
        p = os.path.join(args.resources, f)
        if os.path.exists(p):
            store.load(p)
    print(f'Loaded all storage files in {time.time() - t0:.2f} s')

    # Extract all interior specifications
    if tutorial:
        specs = interior_assemble.get_tutorial_interior_specs(args.resources)
    else:
        specs = interior_assemble.get_all_interior_specs(args.resources)

    # Determine targets
    targets = []
    if args.tutorial:
        for z, sp in sorted(specs.items()):
            targets.append((z, sp['block'], sp))
    elif args.all:
        for z in sorted(specs.keys()):
            targets.append((z, specs[z]['block'], specs[z]))
    elif args.all_hoods:
        for z, sp in sorted(specs.items()):
            if sp.get('is_safezone', False):
                targets.append((z, sp['block'], sp))
    elif args.all_streets:
        for z, sp in sorted(specs.items()):
            if not sp.get('is_safezone', False):
                targets.append((z, sp['block'], sp))
    elif args.all_ttc:
        for z in (2513, 2514, 2516, 2518, 2519, 2520, 2521, 2522):
            sp = specs.get(z) or interior_assemble.TTC_INTERIORS.get(z)
            block = sp['block'] if sp else (z % 100)
            targets.append((z, block, sp))
    elif args.hood:
        for z, sp in sorted(specs.items()):
            if sp['hood_id'] == args.hood:
                targets.append((z, sp['block'], sp))
    elif args.street:
        for z, sp in sorted(specs.items()):
            if sp['branch_id'] == args.street:
                targets.append((z, sp['block'], sp))
    elif args.sample_previews:
        for z in SAMPLE_PREVIEW_ZONES:
            if z in specs:
                targets.append((z, specs[z]['block'], specs[z]))
            elif z in interior_assemble.TTC_INTERIORS:
                targets.append((z, interior_assemble.TTC_INTERIORS[z]['block'], interior_assemble.TTC_INTERIORS[z]))
    elif args.zone is not None:
        z = args.zone
        sp = specs.get(z)
        if sp is None and z in interior_assemble.TTC_INTERIORS:
            sp = interior_assemble.TTC_INTERIORS[z]
        block = args.block if args.block is not None else (sp['block'] if sp else (z % 100))
        targets.append((z, block, sp))
    else:
        ap.error('Specify a target flag (--all, --all-hoods, --all-streets, --all-ttc, --hood, --street, --zone, '
                 '--tutorial, or --sample-previews)')

    print(f'Baking {len(targets)} interior(s)...')
    t_start = time.time()
    results = []

    for i, (zone_id, block_num, sp) in enumerate(targets):
        is_sample = (zone_id in SAMPLE_PREVIEW_ZONES)
        want_preview = not args.no_preview and (args.sample_previews or (len(targets) <= 8 and not args.all_streets and not args.all) or is_sample)

        # Progress reporting every 25 interiors
        if len(targets) > 1 and (i % 25 == 0 or i == len(targets) - 1 or len(targets) <= 10):
            print(f'  [{i + 1}/{len(targets)}] Baking Zone {zone_id} (Block {block_num}: {sp["name"] if sp else "Building"})...', flush=True)

        res = bake_one(
            zone_id=zone_id,
            block=block_num,
            base=base,
            store=store,
            resources=args.resources,
            out_dir=args.out_dir,
            preview_dir=args.preview_dir,
            spec=sp,
            max_tex=args.max_tex,
            want_preview=want_preview,
        )
        results.append(res)

    dt_total = time.time() - t_start
    print(f'\nAll {len(results)} bakes complete in {dt_total:.2f} s ({dt_total / max(1, len(results)):.3f} s/interior)!')

    # Output stats summary table
    print('\n=== Baked Interiors Summary ===')
    print(f'{"Zone":<6} {"Block":<6} {"Name":<32} {"Type":<12} {"Tris":<6} {"Batches":<8} {"TTM KB":<8} {"TTC KB":<8}')
    print('-' * 88)
    for r in results[:20]:
        print(f'{r["zoneId"]:<6} {r["block"]:<6} {r["name"][:31]:<32} {r["interiorType"]:<12} {r["triangles"]:<6} {r["batches"]:<8} {r["ttmBytes"] // 1024:<8} {r["ttcBytes"] // 1024:<8}')
    if len(results) > 20:
        print(f'... and {len(results) - 20} more interiors.')


if __name__ == '__main__':
    main()
