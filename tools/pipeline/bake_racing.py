#!/usr/bin/env python3
"""Bake original racing geometry, real track collision, and versioned curve metadata."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import struct
import sys

os.environ['TT_ETC1_THREADS'] = '3'
os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
# The main checkout has ref/<submodule>; linked worktrees reach it through ref/ref.
REF = ROOT / 'ref' if (ROOT / 'ref/open-toontown/toontown').is_dir() else ROOT / 'ref/ref'
sys.path.insert(0, str(HERE))
IDS = (0, 1, 20, 21, 40, 41, 60, 61, 62, 63, 64, 65)
NAMES = ('screwball_stadium', 'screwball_stadium_rev', 'rustic_raceway', 'rustic_raceway_rev',
         'city_circuit', 'city_circuit_rev', 'corkscrew_coliseum', 'corkscrew_coliseum_rev',
         'airborne_acres', 'airborne_acres_rev', 'blizzard_boulevard', 'blizzard_boulevard_rev')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--resources', type=Path, default=REF / 'resources')
    ap.add_argument('--out', type=Path, default=ROOT / 'sdmc-out/3ds/toontown3ds/data/racing')
    ap.add_argument('--only', type=int, choices=IDS, nargs='+')
    args = ap.parse_args()
    from panda3d.core import (BamCache, NodePath, Point3, CollisionNode, CollisionRay, CollisionHandlerQueue,
                              CollisionTraverser, BitMask32, loadPrcFileData)
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level error\n'
                    'textures-power-2 none\nmodel-cache-dir\nmodel-path ' + str(args.resources))
    from direct.showbase.ShowBase import ShowBase
    import extract
    import ttm
    import collide
    base = ShowBase()
    BamCache.getGlobalPtr().setActive(False)
    spec = runpy.run_path(str(REF / 'open-toontown/toontown/racing/RaceGlobals.py'))
    args.out.mkdir(parents=True, exist_ok=True)
    report = {'version': 1, 'tracks': [], 'sha256': {}}
    for track_id, name in zip(IDS, NAMES):
        if args.only and track_id not in args.only:
            continue
        info = spec['TrackDict'][track_id]
        model = base.loader.loadModel(str(args.resources / (info[0] + '.bam')))
        if model.isEmpty():
            raise FileNotFoundError(info[0])
        exporter = extract.Extractor(max_tex=128)
        exporter.add(model)
        stats = ttm.write(str(args.out / (name + '.ttm')), exporter.textures, exporter.batches())
        if stats['triangles'] > 15000 or stats['batches'] > 150 or stats['geometry_bytes'] > 6*1024**2 or stats['texture_bytes'] > 8*1024**2:
            raise ValueError(f'{name} over budget: {stats}')
        # Use this track, never the kartPad. Track bounds require a coarser spatial grid.
        mesh = collide.extract(model, fallback_floor=False)
        mesh.write(str(args.out / (name + '.ttc')), cell_size=128, max_bytes=6*1024**2)
        curve = model.find('**/curve_' + ('reverse' if track_id % 2 else 'forward')).node()
        points = []
        for index in range(4000):  # DistributedRace.setupGeom uses exactly 4000 samples
            point = Point3()
            curve.getPoint(index/4000 * (curve.getMaxT()-1e-11), point)
            points.append(tuple(point))
        # Match getNearestT's legacy squared-projection interpolation.
        def nearest(point):
            pole = min(range(4000), key=lambda i: sum((point[j]-points[i][j])**2 for j in range(3)))
            a, b = points[pole], points[(pole+1)%4000]
            if sum((point[j]-a[j])*(b[j]-a[j]) for j in range(3)) <= 0:
                pole = (pole-1)%4000
                a, b = points[pole], points[(pole+1)%4000]
            dx, dy = b[0]-a[0], b[1]-a[1]
            ratio = ((point[0]-a[0])*dx+(point[1]-a[1])*dy)/(dx*dx+dy*dy)
            return (pole+ratio*ratio)/4000
        start = model.find('**/lap_start' + ('_rev' if track_id % 2 else '')).getPos()
        middle = model.find('**/lap_middle').getPos()
        # findAllMatches order is intentionally the stock setupGeom order.
        starts = [(tuple(p.getPos()), tuple(p.getHpr())) for p in model.findAllMatches('**/start_pos*')]
        if len(starts) < 4:
            raise ValueError('track has fewer than four original start poses')
        # Exact floor-ray surface tags, matching DistributedVehicle.__groundCollision.
        ray = CollisionRay(0, 0, 40000, 0, 0, -1)
        ray_node = CollisionNode('racing-surface-probe')
        ray_node.addSolid(ray)
        ray_node.setFromCollideMask(BitMask32(2))
        ray_node.setIntoCollideMask(BitMask32.allOff())
        ray_path = model.attachNewNode(ray_node)
        queue = CollisionHandlerQueue()
        traversal = CollisionTraverser()
        traversal.addCollider(ray_path, queue)
        codes = {'asphalt': 0, 'gravel': 1, 'dirt': 2, 'grass': 3, 'ice': 4}
        values = []
        for point in points:
            ray.setOrigin(point[0], point[1], 40000)
            queue.clearEntries()
            traversal.traverse(model)
            queue.sortEntries()
            surface = codes.get(queue.getEntry(0).getIntoNodePath().getNetTag('surface'), 5) if queue.getNumEntries() else 5
            values.append((*point, float(surface)))
        ray_path.removeNode()
        floor_records = []
        for node in model.findAllMatches('**/+CollisionNode'):
            if not node.node().getIntoCollideMask().getWord() & 2:
                continue
            holder = NodePath('floor-node-root')
            copy = node.copyTo(holder)
            copy.setMat(node.getNetTransform().getMat())
            part = collide.extract(holder, fallback_floor=False)
            holder.removeNode()
            code = float(codes.get(node.getNetTag('surface'), 5))
            for a, b, c, normal, flags in part.tris:
                if flags & collide.FLOOR and not flags & collide.TRIGGER:
                    floor_records.append((*a, *b, *c, code))
        if not floor_records:
            raise ValueError('track has no original floor polygons')
        gags = info[4]
        path = args.out / (name + '.trc')
        with path.open('wb') as output:
            output.write(struct.pack('<4sHHIffIII', b'TRC1', 1, track_id, 4000, nearest(start), nearest(middle), len(starts), len(gags), len(floor_records)))
            for point in values: output.write(struct.pack('<4f', *point))
            for pos, hpr in starts: output.write(struct.pack('<6f', *pos, *hpr))
            for pos in gags: output.write(struct.pack('<3f', *pos))
            for floor in floor_records: output.write(struct.pack('<10f', *floor))
        stats.update(id=track_id, name=name, curve_points=4000, collision_bytes=(args.out/(name+'.ttc')).stat().st_size)
        report['tracks'].append(stats)
        print(json.dumps(stats), flush=True)
        model.removeNode()
    for track in report['tracks']:
        for suffix in ('.ttm', '.ttc', '.trc'):
            file = args.out / (track['name'] + suffix)
            report['sha256'][file.name] = hashlib.sha256(file.read_bytes()).hexdigest()
    (args.out/'manifest.json').write_text(json.dumps(report, indent=2, sort_keys=True)+'\n')
    base.destroy()
if __name__ == '__main__':
    main()
