#!/usr/bin/env python3
"""The Map tab cell is an arrow into a portal, not a solid well."""
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'pipeline'))
import ttm  # noqa: E402


def untile_rgba8(raw, w, h):
    inv = np.empty(64, np.int32)
    flat = ttm._MORTON.reshape(64)
    for i, m in enumerate(flat):
        inv[int(m)] = i
    src = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 4)
    px = np.zeros((h, w, 4), np.uint8)
    tiles_x = w // 8
    for ty in range(h // 8):
        for tx in range(tiles_x):
            base = (ty * tiles_x + tx) * 64
            block = src[base:base + 64]
            for m in range(64):
                i = int(inv[m])
                x = tx * 8 + (i % 8)
                y = ty * 8 + (i // 8)
                a, b, g, r = block[m]
                px[y, x] = (r, g, b, a)
    return px


def load_atlas(path):
    data = Path(path).read_bytes()
    w, h, fmt = struct.unpack_from('<HHB', data, struct.unpack_from('<I', data, 44)[0])
    off = struct.unpack_from('<I', data, 44)[0]
    doff, size = struct.unpack_from('<II', data, off + 8)
    if fmt != ttm.GPU_RGBA8:
        raise SystemExit(f'{path} texture 0 is format {fmt}, expected RGBA8')
    return untile_rgba8(data[doff:doff + size], w, h)


def main():
    px = load_atlas(ROOT / 'romfs' / 'baked' / 'book_tabs.ttm')
    cell = px[48:96, 144:192]
    red = (cell[:, :, 0] > 160) & (cell[:, :, 0] > cell[:, :, 1] * 2) & (cell[:, :, 0] > cell[:, :, 2] * 2)
    ys, xs = np.where(red)
    if len(xs) < 40:
        raise SystemExit('map tab has no red arrow')
    tip = ys.max()
    tip_x = xs[ys == tip].mean()
    if abs(tip_x - cell.shape[1] / 2) > 3:
        raise SystemExit(f'map arrow tip is off centre ({tip_x:.1f})')
    dark = (cell[:, :, 0] < 40) & (cell[:, :, 1] < 40) & (cell[:, :, 2] < 40) & (cell[:, :, 3] > 200)
    if dark.sum() > 180:
        raise SystemExit(f'map portal is a solid well ({int(dark.sum())} black pixels)')
    bright = (cell[:, :, 1] > 70) & (cell[:, :, 3] > 200)
    if bright.sum() < 40:
        raise SystemExit('map portal has no lighter interior')
    print('check_map_glyph: ok')


if __name__ == '__main__':
    main()
