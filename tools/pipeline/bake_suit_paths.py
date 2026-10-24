#!/usr/bin/env python3
"""Street DNA -> suit path graph (.tsp), the data behind DistributedSuit walking.

    .venv/bin/python -I tools/pipeline/bake_suit_paths.py                 # all 17 streets
    .venv/bin/python -I tools/pipeline/bake_suit_paths.py --zone 2100,2200
    .venv/bin/python -I tools/pipeline/bake_suit_paths.py --check         # re-bake, compare bytes

Output: sdmc-out/3ds/toontown3ds/data/zones/<branch>.tsp  (next to <branch>.tz).

Suits never broadcast positions. The client rebuilds each cog's path from the street's
DNA graph (store_suit_point / suit_edge), exactly as DNAStorage does on the AI, so this
file must reproduce the storage the stock libtoontown builds when it loads the street DNA
(docs/design/combat.md section 3 and "Client implementation status"):

* points are kept in file order, keyed by their DNA index (the indices are sparse);
* edges are kept in the order DNAStorage::store_suit_edge accepted them: edges whose end
  points do not exist are dropped, degenerate edges (start == end) are dropped, and a
  repeated (start, end) pair keeps its first occurrence (and zone). The relative order
  matters: getSuitPath's breadth-first search expands edges in this order, and the AI
  and the client must pick the same path;
* an edge's zone is the visgroup it appears in (name before ':'), i.e. the zone id the
  AI puts the suit in while it walks that edge.

TSP1 format (little endian, no alignment padding beyond what is listed):

    Header (16 bytes)
      char[4] magic "TSP1"; u16 version (1); u16 branch zone (2100, ...)
      u16 numPoints; u16 numEdges; u16 numCells; u16 reserved (0)
    Point (20 bytes) x numPoints, DNA file order
      u16 index (DNA suit point index); u8 type (0 STREET_POINT, 1 FRONT_DOOR_POINT,
      2 SIDE_DOOR_POINT, 3 COGHQ_IN_POINT, 4 COGHQ_OUT_POINT); u8 pad
      f32 x, y, z; u16 landmarkBlock (lb_index: building block of a door point, else 0); u16 pad
    Edge (6 bytes) x numEdges, accepted order
      u16 start point index; u16 end point index; u16 zone (visgroup)
    Battle cell (16 bytes) x numCells, file order
      u16 zone (visgroup); u8 width; u8 height; f32 x, y, z

The runtime mirror is source/world/suit_path.cpp (SuitGraph::load).
"""
import argparse
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

import dna  # noqa: E402
import street_catalog  # noqa: E402

MAGIC = b'TSP1'
VERSION = 1
POINT_TYPES = {'STREET_POINT': 0, 'FRONT_DOOR_POINT': 1, 'SIDE_DOOR_POINT': 2,
               'COGHQ_IN_POINT': 3, 'COGHQ_OUT_POINT': 4}
STREETS = (2100, 2200, 2300, 1100, 1200, 1300, 3100, 3200, 3300, 4100, 4200, 4300,
           5100, 5200, 5300, 9100, 9200)


def extract(root):
    """DNA tree -> (points, edges, cells) with DNAStorage's acceptance rules."""
    points, by_index = [], {}
    for n in root.walk():
        if n.kind != 'store_suit_point':
            continue
        v = n.values
        if len(v) < 5:
            raise ValueError(f'bad store_suit_point {v}')
        idx = int(v[0])
        ptype = POINT_TYPES[v[1]] if isinstance(v[1], str) else int(v[1])
        block = int(v[5]) if len(v) > 5 else 0
        p = (idx, ptype, float(v[2]), float(v[3]), float(v[4]), block)
        points.append(p)
        by_index[idx] = p  # _suit_point_map[index] = point: the last one wins

    edges, seen, cells = [], set(), []
    for vg in root.walk():
        if vg.kind != 'visgroup':
            continue
        zone = int(str(vg.name).split(':')[0])
        for c in vg.children:
            if c.kind == 'suit_edge':
                a, b = int(c.values[0]), int(c.values[1])
                if a not in by_index or b not in by_index or a == b:
                    continue
                if (a, b) in seen:
                    continue
                seen.add((a, b))
                edges.append((a, b, zone))
            elif c.kind == 'battle_cell':
                w, h, x, y, z = (float(t) for t in c.values[:5])
                cells.append((zone, int(w), int(h), x, y, z))
    return points, edges, cells


def pack(branch, points, edges, cells):
    assert len(points) < 65536 and len(edges) < 65536 and len(cells) < 65536
    out = bytearray(struct.pack('<4sHHHHHH', MAGIC, VERSION, branch, len(points), len(edges),
                                len(cells), 0))
    for idx, ptype, x, y, z, block in points:
        out += struct.pack('<HBB3fHH', idx, ptype, 0, x, y, z, block, 0)
    for a, b, zone in edges:
        out += struct.pack('<HHH', a, b, zone)
    for zone, w, h, x, y, z in cells:
        out += struct.pack('<HBB3f', zone, w, h, x, y, z)
    return bytes(out)


def bake(zone, resources):
    rel = street_catalog.paths(zone)[2]
    root = dna.parse_file(os.path.join(resources, rel))
    points, edges, cells = extract(root)
    return pack(zone, points, edges, cells), (len(points), len(edges), len(cells), rel)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--zone', default='all', help='all or comma-separated street branch zones')
    ap.add_argument('--resources', default=str(ROOT / 'ref/resources'))
    ap.add_argument('--out', default=str(ROOT / 'sdmc-out/3ds/toontown3ds/data/zones'))
    ap.add_argument('--check', action='store_true', help='compare with the existing files instead of writing')
    args = ap.parse_args()
    zones = STREETS if args.zone == 'all' else tuple(int(z) for z in args.zone.split(','))
    os.makedirs(args.out, exist_ok=True)
    bad = 0
    for z in zones:
        data, (np_, ne, nc, rel) = bake(z, args.resources)
        path = os.path.join(args.out, f'{z}.tsp')
        if args.check:
            same = os.path.exists(path) and open(path, 'rb').read() == data
            bad += not same
            print(f'{z}: {"ok" if same else "DIFFERS"}')
            continue
        with open(path, 'wb') as f:
            f.write(data)
        print(f'{z}: {np_} points, {ne} edges, {nc} battle cells, {len(data)} bytes <- {rel}')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
