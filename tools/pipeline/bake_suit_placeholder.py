#!/usr/bin/env python3
"""Untextured stand-in for the cog models: romfs/baked/suit_placeholder.ttm.

    .venv/bin/python -I tools/pipeline/bake_suit_placeholder.py [out.ttm]

A unit-height body box (footprint 1 x 1, z 0..1, centred on the origin) with a head cube on
top and a nose on the +Y side so the heading shows. Vertex colours carry a fixed top/side
shading; the runtime (world/suits.cpp) scales it to the cog's height and tints it with the
department colour through Renderer::setBaseTint. Replaced by the real baked cog models.
"""
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ttm  # noqa: E402

ROOT = HERE.parent.parent


def box(lo, hi):
    """Outward-facing CCW box: returns (verts[n,9], indices) with per-face shading."""
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    # (normal, shade, 4 corners CCW seen from outside)
    faces = [
        ((0, 0, 1), 1.00, [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
        ((0, 0, -1), 0.45, [(x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0)]),
        ((0, 1, 0), 0.85, [(x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1)]),
        ((0, -1, 0), 0.70, [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
        ((1, 0, 0), 0.60, [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)]),
        ((-1, 0, 0), 0.60, [(x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1)]),
    ]
    verts, idx = [], []
    for _, shade, corners in faces:
        base = len(verts)
        for c in corners:
            verts.append([*c, 0.0, 0.0, shade, shade, shade, 1.0])
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return verts, idx


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'romfs' / 'baked' / 'suit_placeholder.ttm'
    verts, idx = [], []
    for lo, hi in (((-0.5, -0.5, 0.0), (0.5, 0.5, 0.78)),      # body
                   ((-0.3, -0.3, 0.78), (0.3, 0.3, 1.0)),       # head
                   ((-0.12, 0.3, 0.84), (0.12, 0.55, 0.94))):   # nose, +Y = forward
        v, i = box(lo, hi)
        idx += [k + len(verts) for k in i]
        verts += v
    batch = ttm.Batch(None, ttm.BLEND_OPAQUE, False, 0, 0, np.array(verts, dtype=np.float64),
                      np.array(idx, dtype=np.int64))
    stats = ttm.write(str(out), [], [batch])
    print(f'{out}: {stats["triangles"]} triangles, {stats["file_bytes"]} bytes')


if __name__ == '__main__':
    main()
