#!/usr/bin/env python3
"""Bake Cog HQ exteriors and lobbies (single .bam scenes) to streamed .tz files.

    .venv/bin/python -I tools/pipeline/bake_coghq.py [--zone all|11000,11100,...]
        [--out sdmc-out/3ds/toontown3ds/data/zones] [--preview] [--audio]

Each zone is one scene model (see coghq_catalog.py), not DNA. The baker applies the same node
renames the upstream CogHQLoader subclasses apply at load time, extracts collision, tunnel and
door triggers with collide.py, then chunks the visible geometry into a uniform XY grid exactly
like the playgrounds of bake_street.py (PVS = cells within --distance, far LOD from
street_lod.py, atlases from street_atlas.py) and writes TZ v2 with tz.py.

Outputs per zone: <zone>.tz, <zone>.pts (tunnel/door anchors) and <zone>.json (budget report).
--audio also bakes the HQ music to <out>/../audio/<hood>.tsnd for the four HQ hoods.
At most 3 ETC1 encoder threads; no source asset is modified.
"""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
import tempfile

os.environ['TT_ETC1_THREADS'] = '3'
os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
os.environ['VECLIB_MAXIMUM_THREADS'] = '1'
os.environ['MKL_NUM_THREADS'] = '1'
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
ROOT = HERE.parent.parent
MAIN_REF = Path('/Users/alexchadwick/toontown-3ds/ref/resources')


def default_resources():
    mine = ROOT / 'ref/resources'
    if (mine / 'phase_9').exists():
        return str(mine)
    ref_ref = ROOT / 'ref/ref/resources'
    if (ref_ref / 'phase_9').exists():
        return str(ref_ref)
    return str(MAIN_REF)


def build_scene(zone_id, cfg, loader, resources):
    """Load the scene and apply the CogHQLoader renames. Returns (root, report)."""
    from panda3d.core import NodePath, PandaNode
    import assemble
    root = NodePath(PandaNode(f'coghq_{zone_id}'))
    model = loader.loadModel(cfg['model'], noCache=True)
    if model is None or model.isEmpty():
        raise FileNotFoundError(cfg['model'])
    model.reparentTo(root)
    info = {'dropped': [], 'tunnels': [], 'doors': []}
    for pattern in cfg['drop']:
        for n in root.findAllMatches('**/' + pattern):
            info['dropped'].append(n.getName())
            n.stash()
    for old, new in cfg['tunnels'].items():
        n = root.find('**/' + old)
        if n.isEmpty():
            raise ValueError(f'{zone_id}: tunnel node {old} not found')
        n.setName(new)
        for s in n.findAllMatches('**/tunnel_sphere'):
            s.setName('tunnel_trigger')
        info['tunnels'].append(new)
    for cn in root.findAllMatches('**/door_trigger_*'):
        index = int(cn.getName().rsplit('_', 1)[1])
        dest = cfg['doors'].get(index)
        p = cn.getParent()
        while not p.isEmpty() and p != root and p.getName() != f'door_{index}':
            p = p.getParent()
        if dest is None or p.isEmpty() or p == root:
            raise ValueError(f'{zone_id}: door_trigger_{index} has no door node/destination')
        p.setName(f'cogdoor_{dest}_{index}_DNARoot')
        info['doors'].append(p.getName())
    # Closed doors: upstream hides the black hole decals until the door opens.
    for n in root.findAllMatches('**/doorFrameHole*'):
        n.hide()
    if zone_id == 10000:
        top = root.find('**/TunnelEntrance')
        if not top.isEmpty():
            origin = top.find('**/tunnel_origin')
            if not origin.isEmpty():
                origin.setH(-33.33)
    assemble.lift_decals(root)
    info['billboards_crossed'] = assemble.cross_billboards(root)
    return root, info


def write_points(root, cfg, path):
    """Anchors in the .pts format Place parses: name x y z h (zone space)."""
    lines = []
    for new in cfg['tunnels'].values():
        n = root.find('**/' + new)
        origin = n.find('**/tunnel_origin')
        ref = origin if not origin.isEmpty() else n
        p, h = ref.getPos(root), ref.getH(root)
        lines.append(f'{new} {p[0]:.3f} {p[1]:.3f} {p[2]:.3f} {h:.2f}')
    for index, dest in sorted(cfg['doors'].items()):
        o = root.find(f'**/cogdoor_{dest}_{index}_DNARoot/**/door_origin_{index}')
        if o.isEmpty():
            o = root.find(f'**/door_origin_{index}')
        if o.isEmpty():
            continue
        p, h = o.getPos(root), o.getH(root)
        lines.append(f'cogdoor_{dest}_{index} {p[0]:.3f} {p[1]:.3f} {p[2]:.3f} {h:.2f}')
    Path(path).write_text('\n'.join(lines) + ('\n' if lines else ''))
    return len(lines)


def bake(zone_id, args, base):
    import numpy as np
    import collide
    import coghq_catalog
    import street_atlas
    import street_lod
    import ttm
    import tz
    from bake_street import split_grid
    cfg = coghq_catalog.CATALOG[zone_id]
    root, info = build_scene(zone_id, cfg, base.loader, args.resources)
    ex = street_atlas.Extractor()
    whole = collide.extract(root, fallback_floor=False)
    collision, collision_stats = whole.pack()
    ex.add(root)
    raw_batches = ex.batches()
    groups = split_grid(raw_batches, args.grid_size)
    size = args.grid_size
    floor_tris = [(np.array([a, b, c]), normal) for a, b, c, normal, flags in whole.tris
                  if flags & collide.FLOOR and not flags & collide.TRIGGER and normal[2] >= 0.707]
    if not floor_tris:
        raise ValueError(f'{zone_id}: no floor collision')

    def cells_of(p):
        return [(cx, cy)
                for cy in range(math.floor(p[:, 1].min() / size), math.floor(p[:, 1].max() / size) + 1)
                for cx in range(math.floor(p[:, 0].min() / size), math.floor(p[:, 0].max() / size) + 1)]

    def clip_cell(p, cx, cy):
        poly = list(np.pad(p, ((0, 0), (0, 6))))
        for axis, key in ((0, cx), (1, cy)):
            poly = street_atlas.clip(street_atlas.clip(poly, axis, key * size, True), axis, (key + 1) * size, False) if poly else []
        return poly
    # Grid cells crossed by floor triangles exist even when they own no render triangles.
    for p, _ in floor_tris:
        for cx, cy in cells_of(p):
            if len(clip_cell(p, cx, cy)) >= 3:
                groups.setdefault((cx, cy), [])
    keys = sorted(groups)
    index_of = {k: i for i, k in enumerate(keys)}
    if len(keys) > 256:
        raise ValueError(f'{zone_id}: {len(keys)} grid cells exceed the TZ limit of 256; raise --grid-size')
    raw, chunks, floors = [], [], []
    for i, key in enumerate(keys):
        raw.append(groups[key])
        chunks.append({'name': f'g{i}', 'zone': zone_id, 'vis': [], 'grid': key})
    for p, _ in floor_tris:
        for cx, cy in cells_of(p):
            poly = clip_cell(p, cx, cy)
            i = index_of[(cx, cy)]
            for j in range(1, len(poly) - 1):
                tri = np.array([poly[k][:3] for k in (0, j, j + 1)])
                if np.linalg.norm(np.cross(tri[1] - tri[0], tri[2] - tri[0])) > 1e-6:
                    floors.append((tri, i, zone_id))
    floors.sort(key=lambda f: f[1])
    textures, batches = street_atlas.build(ex.sources, raw)
    batches = [[street_atlas.rebase_large_uvs(b, textures) for b in bs] for bs in batches]
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    def pack(bs, tmp):
        if not bs:
            return b'', {'batches': 0, 'triangles': 0, 'geometry_bytes': 0, 'texture_bytes': 0,
                         'bounds': [[0, 0, 0], [0, 0, 0]]}
        used = sorted({b.texture for b in bs if b.texture is not None})
        mapping = {old: new for new, old in enumerate(used)}
        copies = [ttm.Batch(mapping[b.texture] if b.texture is not None else None,
                            b.blend, b.two_sided, b.tint, b.layer, b.verts, b.indices) for b in bs]
        path = Path(tmp) / 'chunk.ttm'
        stats = ttm.write(path, [textures[j] for j in used], copies)
        if stats['uv_clamped']:
            raise ValueError('UV overflow')
        return path.read_bytes(), stats
    with tempfile.TemporaryDirectory() as tmp:
        for chunk, bs in zip(chunks, batches):
            chunk['model'], chunk['stats'] = pack(bs, tmp)
    # Triangle ownership uses centroids, not clipping. A long triangle can cross
    # the camera's cell while belonging several cells away. A fixed 3x3 PVS
    # discarded such geometry, leaving exposed stair edges and missing surfaces.
    # Match playground PVS: full chunk bounds against the cell plus camera reach.
    for i, c in enumerate(chunks):
        lo = np.array(c['grid']) * args.grid_size
        hi = lo + args.grid_size
        for j, d in enumerate(chunks):
            bmin,bmax = map(np.array,d['stats']['bounds'])
            delta = np.maximum(np.maximum(bmin[:2]-hi,lo-bmax[:2]),0)
            if i==j or np.linalg.norm(delta)<=args.distance+38:
                c['vis'].append(j)
    low, lod_report = street_lod.make_low(chunks, batches)
    with tempfile.TemporaryDirectory() as tmp:
        for chunk, bs in zip(chunks, low):
            chunk['low_model'], chunk['low_stats'] = pack(bs, tmp)
            if chunk['low_model']:
                chunk['stats']['bounds'] = [
                    np.minimum(chunk['stats']['bounds'][0], chunk['low_stats']['bounds'][0]).tolist(),
                    np.maximum(chunk['stats']['bounds'][1], chunk['low_stats']['bounds'][1]).tolist()]
    before = tz.report(chunks)
    low_report = tz.report([dict(c, stats=c['low_stats']) for c in chunks])
    samples = []
    for i, c in enumerate(chunks):
        points = [np.mean(f[0], axis=0) for f in floors if f[1] == i]
        if not points:
            points = [np.mean(c['stats']['bounds'], axis=0)]
        for p in points[::max(1, len(points) // 8)]:
            _, metrics = street_lod.plan(chunks, i, p)
            samples.append(metrics)
    planned = {k: max(row[k] for row in samples) for k in ('batches', 'triangles', 'geometry_bytes', 'texture_bytes')}
    triggers = []
    for t in whole.triggers:
        tunnel = 'tunnel' in (t.name + ' ' + t.owner).lower()
        triggers.append({'min': list(t.lo), 'max': list(t.hi),
                         'kind': 2 if tunnel else 1 if 'door' in t.name.lower() else 0,
                         'name': t.name, 'owner': t.owner})
    report = tz.write(out / f'{zone_id}.tz', zone_id, chunks, floors, triggers, collision)
    num_points = write_points(root, cfg, out / f'{zone_id}.pts')
    import hq_arrivals
    arrival = hq_arrivals.write_anchor(args.resources, zone_id, out/f'{zone_id}.pts', whole)
    num_points += int(arrival is not None)
    report.update({
        'arrival': arrival, 'version': 2, 'zone': zone_id, 'name': cfg['name'], 'model': cfg['model'],
        'before': before['worst'], 'low': low_report['worst'], 'planned_worst': planned, 'lod': lod_report,
        'points': num_points, 'floor_triangles': len(floors), 'triggers': len(triggers),
        'trigger_list': [{k: t[k] for k in ('kind', 'name', 'owner')} for t in triggers],
        'file_bytes': (out / f'{zone_id}.tz').stat().st_size, 'atlas_pages': len(textures),
        'grid_size': size, 'distance': args.distance, 'collision': collision_stats, **info,
        'source_triangles': sum(len(b.indices) // 3 for b in raw_batches),
        'source_batches': len(raw_batches)})
    (out / f'{zone_id}.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k not in ('visible_sets', 'collision', 'trigger_list')}, indent=2),
          flush=True)
    root.removeNode()
    return out / f'{zone_id}.tz'


def bake_music(out_dir, resources):
    import shutil
    import bake_audio
    import coghq_catalog
    audio = Path(out_dir).parent / 'audio'
    audio.mkdir(parents=True, exist_ok=True)
    for hood, src in sorted(coghq_catalog.MUSIC.items()):
        dst = audio / f'{hood}.tsnd'
        bake_audio.bake_music(str(Path(resources) / src), str(dst), True)
        print(f'music {hood}: {src} -> {dst} ({dst.stat().st_size} bytes)', flush=True)
        for zid in coghq_catalog.HOOD_ZONES.get(hood, ()):
            z_dst = audio / f'{zid}.tsnd'
            if z_dst != dst:
                shutil.copyfile(dst, z_dst)
                print(f'music {zid}: copied from {hood}.tsnd ({z_dst.stat().st_size} bytes)', flush=True)


def main():
    import coghq_catalog
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--zone', default='all', help='all or comma-separated zone ids')
    ap.add_argument('--resources', default=default_resources())
    ap.add_argument('--out', default=str(ROOT / 'sdmc-out/3ds/toontown3ds/data/zones'))
    ap.add_argument('--grid-size', type=float, default=None,
                    help='cell size (Cashbot 12000: 192; other HQ zones: 96)')
    ap.add_argument('--distance', type=float, default=120)
    ap.add_argument('--preview', action='store_true')
    ap.add_argument('--audio', action='store_true', help='also bake the four HQ music tracks')
    args = ap.parse_args()
    if (args.grid_size is not None and args.grid_size <= 0) or args.distance <= 0:
        ap.error('grid size and distance must be positive')
    zones = coghq_catalog.ZONES if args.zone == 'all' else tuple(int(z) for z in args.zone.split(','))
    for z in zones:
        if z not in coghq_catalog.CATALOG:
            ap.error(f'unsupported zone {z}')
    if len(zones) > 1:
        for z in zones:
            command = [sys.executable, '-I', str(Path(__file__).resolve()), '--zone', str(z),
                       '--resources', args.resources, '--out', args.out,
                       '--grid-size', str(args.grid_size or (192 if z==12000 else 96)), '--distance', str(args.distance)]
            if args.preview:
                command.append('--preview')
            subprocess.run(command, check=True)
        if args.audio:
            bake_music(args.out, args.resources)
        return
    args.grid_size = args.grid_size or (192 if zones[0]==12000 else 96)
    from panda3d.core import loadPrcFileData, Filename, getModelPath, BamCache
    BamCache.getGlobalPtr().setActive(False)
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                    'textures-power-2 none\nmodel-cache-dir')
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()
    getModelPath().prependDirectory(Filename.fromOsSpecific(os.path.abspath(args.resources)))
    print(f'Baking zone {zones[0]}', flush=True)
    path = bake(zones[0], args, base)
    base.destroy()
    if args.preview:
        subprocess.run([sys.executable, '-I', str(HERE / 'street_preview.py'), str(path),
                        str(ROOT / 'sdmc-out/previews/coghq')], check=True)
    if args.audio:
        bake_music(args.out, args.resources)


if __name__ == '__main__':
    main()
