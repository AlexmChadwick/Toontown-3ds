#!/usr/bin/env python3
"""Decode render-target dumps written by source/core/screenshot.cpp into PNGs.

    .venv/bin/python -I tools/dev/decode_shot.py <shot.bin>... [--out-dir DIR]

The PICA color buffer is 8x8 Morton-tiled and stored rotated (240 wide, 400/320
tall). RGBA8 pixels are little-endian 0xRRGGBBAA, i.e. bytes A, B, G, R.
"""
import argparse
import os
import struct
import sys

import numpy as np
from panda3d.core import PNMImage

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pipeline'))
import ttm  # noqa: E402

GPU_RB_RGBA8 = 0


def decode(path):
    buf = open(path, 'rb').read()
    assert buf[:4] == b'TTSH', path
    w, h, fmt, _ = struct.unpack_from('<4H', buf, 4)
    if fmt != GPU_RB_RGBA8:
        raise SystemExit(f'{path}: unsupported color format {fmt}')
    raw = np.frombuffer(buf, dtype=np.uint8, count=w * h * 4, offset=12).reshape(-1, 4)
    rgba = raw[:, ::-1]  # A B G R -> R G B A
    img = np.zeros((h, w, 4), dtype=np.uint8)
    morton = ttm._MORTON.reshape(64)
    tiles_x = w // 8
    for ty in range(h // 8):
        for tx in range(tiles_x):
            base = (ty * tiles_x + tx) * 64
            img[ty * 8:(ty + 1) * 8, tx * 8:(tx + 1) * 8] = rgba[base + morton].reshape(8, 8, 4)
    # Buffer row = screen column (left to right), buffer column = screen row from the bottom.
    return np.rot90(img, k=1)


def save_png(px, path):
    h, w = px.shape[:2]
    out = PNMImage(w, h, 3)
    for y in range(h):
        for x in range(w):
            r, g, b = px[y, x, :3]
            out.setXel(x, y, r / 255.0, g / 255.0, b / 255.0)
    out.write(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('shots', nargs='+')
    ap.add_argument('--out-dir')
    args = ap.parse_args()
    for p in args.shots:
        out = os.path.join(args.out_dir or os.path.dirname(p), os.path.basename(p).replace('.bin', '.png'))
        save_png(decode(p), out)
        print('wrote', out)


if __name__ == '__main__':
    main()
