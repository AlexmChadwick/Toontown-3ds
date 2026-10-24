#!/usr/bin/env python3
"""Convert classic-character walk graphs (<char>_<zone>.paths JSON, written by
bake_char.py) into the binary .tcp files the runtime reads (source/world/cchar.cpp).

TCP1 v1, little endian:
    header  char magic[4] "TCP1", u16 version, u16 numNodes, u16 numEdges, u16 numPoints,
            f32 speed, char start[8]
    nodes   numNodes x { char name[8]; f32 x, y, z }
    edges   numEdges x { u8 src, u8 dst, u8 raycast, u8 numPoints; u16 firstPoint, u16 pad }
            (points ordered src -> dst, endpoints included; adjacency is directed)
    points  numPoints x { f32 x, y, z }

    .venv/bin/python -I tools/pipeline/char_paths_bin.py sdmc-out/3ds/toontown3ds/data/chars
"""
import glob
import json
import os
import struct
import sys

VERSION = 1


def name8(s):
    b = s.encode()
    if len(b) >= 8:
        raise SystemExit(f'node name too long: {s!r}')
    return b.ljust(8, b'\0')


def convert(src, dst):
    d = json.load(open(src))
    if d.get('magic') != 'CCHAR_PATHS' or d.get('version') != 1:
        raise SystemExit(f'{src}: not a v1 CCHAR_PATHS file')
    names = sorted(d['nodes'])
    index = {n: i for i, n in enumerate(names)}
    nodes = b''.join(name8(n) + struct.pack('<3f', *d['nodes'][n]['position']) for n in names)
    edges, points = b'', b''
    count = 0
    for e in d['edges']:
        pts = e['points']
        edges += struct.pack('<4B2H', index[e['src']], index[e['dst']], int(e['raycast']), len(pts), count, 0)
        points += b''.join(struct.pack('<3f', *p) for p in pts)
        count += len(pts)
    if len(names) > 255 or count > 65535:
        raise SystemExit(f'{src}: graph too large')
    header = b'TCP1' + struct.pack('<4Hf', VERSION, len(names), len(d['edges']), count, float(d['speed'])) + \
        name8(d['start'])
    with open(dst, 'wb') as f:
        f.write(header + nodes + edges + points)
    return len(names), len(d['edges'])


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'sdmc-out/3ds/toontown3ds/data/chars'
    for src in sorted(glob.glob(os.path.join(out, '*.paths'))):
        n, e = convert(src, src[:-len('.paths')] + '.tcp')
        print(f'{os.path.basename(src)}: {n} nodes, {e} edges')


if __name__ == '__main__':
    main()
