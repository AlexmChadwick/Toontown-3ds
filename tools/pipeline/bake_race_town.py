#!/usr/bin/env python3
"""Bake the towns of the urban race tracks (DistributedRace.loadUrbanTrack) to data/racing/.

City Circuit (RT_Urban_1, tracks 40/41) and Blizzard Boulevard (RT_Urban_2, 64/65) are
mostly town: DistributedRace loads urban_track_town.dna / urban_track_town_B.dna on top of
the track model at run time, so the track .bam alone is a bare road. The town is split into
`Buildings_inner-<i>` / `Buildings_outer-<i>` groups along the track (and, on
Blizzard Boulevard, `Buildings_<inner|outer>sidest-<inner|outer>barricade_<i>` side streets);
showBuildings keeps five groups per side around the kart and the side streets near it.

Each non-empty group becomes `town_<A|B>_<n>.ttm` (its own street_atlas.build_local pages,
textures sized for the kart camera along the track curve, as a street chunk). The index
`town_<A|B>.trt` lists them in stock order:

    TRT1 v1, little endian: char[4] "TRT1"; u16 version=1; u16 flags=0; u16 counts[4]
    (inner, outer, innersidest, outersidest); u16 reserved; then per group, in that side
    order: f32 centre[3] (the group's bounds centre, as getBounds().getCenter()), f32 radius,
    u32 triangles, u32 reserved.

The track's polySurface8 skyline (loadUrbanTrack hangs it on the kart's dummy node at scale
0.1, background bin, no depth) is written by bake_racing.py as `<track>_bg.ttm`.

    .venv/bin/python -I tools/pipeline/bake_race_town.py --resources <ref/resources>
"""
import argparse
import json
import os
import struct
import sys
from pathlib import Path

os.environ.setdefault('TT_ETC1_THREADS', '3')
HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
REF = ROOT / 'ref' if (ROOT / 'ref/open-toontown/toontown').is_dir() else ROOT / 'ref/ref'
sys.path.insert(0, str(HERE))

# DistributedRace.loadUrbanTrack's storage files, in its order.
STORAGE = ('phase_4/dna/storage.dna', 'phase_5/dna/storage_town.dna', 'phase_4/dna/storage_TT.dna',
           'phase_5/dna/storage_TT_town.dna', 'phase_8/dna/storage_BR.dna', 'phase_8/dna/storage_BR_town.dna')
# town key: (DNA file, the track model whose curve the kart camera follows)
TOWNS = {'A': ('phase_6/dna/urban_track_town.dna', 'phase_6/models/karting/RT_UrbanA'),
         'B': ('phase_6/dna/urban_track_town_B.dna', 'phase_6/models/karting/RT_UrbanB')}
SIDES = ('inner', 'outer', 'innersidest', 'outersidest')
HEADER = struct.Struct('<4sHH4HH')
GROUP = struct.Struct('<4fII')


def stock_groups(town):
    """loadUrbanTrack's buildingGroups, per side, in its order (empty inner/outer skipped)."""
    groups = {side: [] for side in SIDES}
    most = 0
    for side in ('inner', 'outer'):
        i = 0
        while True:
            g = town.find('**/Buildings_%s-%d' % (side, i))
            if g.isEmpty():
                break
            if not g.getBounds().isEmpty():
                groups[side].append(g)
            i += 1
        most = max(most, i)
    for side in ('innersidest', 'outersidest'):
        for i in range(most):
            for barricade in ('innerbarricade', 'outerbarricade'):
                g = town.find('**/Buildings_%s-%s_%d' % (side, barricade, i))
                if not g.isEmpty():
                    groups[side].append(g)
    return groups


def curve_points(base, resources, model_path, step=4):
    from panda3d.core import Point3
    model = base.loader.loadModel(str(Path(resources) / (model_path + '.bam')))
    pts = []
    for name in ('curve_forward', 'curve_reverse'):
        np_ = model.find('**/' + name)
        if np_.isEmpty():
            continue
        curve = np_.node()
        for i in range(0, 4000, step):
            p = Point3()
            curve.getPoint(i / 4000 * (curve.getMaxT() - 1e-11), p)
            pts.append((p.x, p.y, p.z + 6))  # DistributedVehicle camera: about 6 over the road
    model.removeNode()
    return pts


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--resources', type=Path, default=REF / 'resources')
    ap.add_argument('--out', type=Path, default=ROOT / 'sdmc-out/3ds/toontown3ds/data/racing')
    ap.add_argument('--only', choices=sorted(TOWNS), nargs='+')
    args = ap.parse_args()
    from panda3d.core import loadPrcFileData, BamCache
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level error\n'
                        'textures-power-2 none\nmodel-cache-dir\nmodel-path ' + str(args.resources))
    from direct.showbase.ShowBase import ShowBase
    import numpy as np
    import assemble
    import dna
    import street_atlas
    import ttm
    from bake_street import hide_wall_shadows, NEAR_ATLAS
    base = ShowBase()
    BamCache.getGlobalPtr().setActive(False)
    args.out.mkdir(parents=True, exist_ok=True)
    report = {}
    for key, (dna_file, track_model) in TOWNS.items():
        if args.only and key not in args.only:
            continue
        store = dna.Storage(base.loader, str(args.resources))
        for s in STORAGE:
            store.load(str(args.resources / s))
        tree = dna.parse_file(args.resources / dna_file, [])
        town, _ = assemble.assemble(tree, store, seed=1)
        town.findAllMatches('**/+CollisionNode').stash()  # loadUrbanTrack stashes them too
        hide_wall_shadows(town)
        groups = stock_groups(town)
        ordered = [(side, g) for side in SIDES for g in groups[side]]
        ex = street_atlas.Extractor()
        raw = []
        for side, g in ordered:
            ex._groups = {}
            ex.add(g)
            bs = ex.batches()
            mat = g.getMat(town)
            if not mat.isIdentity():
                for b in bs:
                    b.verts[:, :3] = [tuple(mat.xformPoint(tuple(p))) for p in b.verts[:, :3]]
            raw.append(bs)
        views = np.array(curve_points(base, args.resources, track_model))
        textures, batches, _ = street_atlas.build_local(ex.sources, raw, view_points=views, **NEAR_ATLAS)
        records, stats_all = [], []
        for n, ((side, g), bs, tex) in enumerate(zip(ordered, batches, textures)):
            bs = [street_atlas.rebase_large_uvs(b, tex) for b in bs]
            used = sorted({b.texture for b in bs if b.texture is not None})
            remap = {old: new for new, old in enumerate(used)}
            bs = [ttm.Batch(remap[b.texture] if b.texture is not None else None, b.blend, b.two_sided, b.tint,
                            b.layer, b.verts, b.indices) for b in bs]
            stats = ttm.write(str(args.out / f'town_{key}_{n}.ttm'), [tex[j] for j in used], bs)
            if stats['uv_clamped']:
                raise ValueError(f'town {key} group {n}: UV overflow')
            bounds = g.getBounds()
            c = bounds.getCenter()
            g_mat = g.getMat(town)
            c = g_mat.xformPoint(c)
            records.append((SIDES.index(side), c.x, c.y, c.z, float(bounds.getRadius()), stats['triangles']))
            stats_all.append(dict(stats, group=g.getName()))
        counts = [sum(1 for r in records if r[0] == i) for i in range(4)]
        with open(args.out / f'town_{key}.trt', 'wb') as f:
            f.write(HEADER.pack(b'TRT1', 1, 0, *counts, 0))
            for side, x, y, z, radius, tris in records:
                f.write(GROUP.pack(x, y, z, radius, tris, 0))
        worst = max(stats_all, key=lambda s: s['texture_bytes'])
        report[key] = {'groups': len(records), 'counts': counts,
                       'triangles': sum(s['triangles'] for s in stats_all),
                       'max_group_triangles': max(s['triangles'] for s in stats_all),
                       'max_group_batches': max(s['batches'] for s in stats_all),
                       'max_group_geometry': max(s['geometry_bytes'] for s in stats_all),
                       'max_group_textures': worst['texture_bytes'],
                       'textures': sum(s['texture_bytes'] for s in stats_all)}
        print(key, json.dumps(report[key]), flush=True)
    return report


if __name__ == '__main__':
    main()
