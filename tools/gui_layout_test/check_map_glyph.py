#!/usr/bin/env python3
"""The Map tab cell is an arrow into a portal, not a solid well."""
import re
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


def load_atlas(data):
    w, h, fmt = struct.unpack_from('<HHB', data, struct.unpack_from('<I', data, 44)[0])
    off = struct.unpack_from('<I', data, 44)[0]
    doff, size = struct.unpack_from('<II', data, off + 8)
    if fmt != ttm.GPU_RGBA8:
        raise SystemExit(f'book_tabs texture 0 is format {fmt}, expected RGBA8')
    return untile_rgba8(data[doff:doff + size], w, h)


def main():
    data = (ROOT / 'romfs' / 'baked' / 'book_tabs.ttm').read_bytes()
    flags, = struct.unpack_from('<H', data, 6)
    if flags != 0:
        raise SystemExit(f'book_tabs flags {flags}, expected 0 (relative indices)')
    index_off, = struct.unpack_from('<I', data, 56)
    indices = data[index_off:index_off + 12]
    if indices != bytes([0, 0, 1, 0, 2, 0, 0, 0, 2, 0, 3, 0]):
        raise SystemExit('book_tabs indices are not the original relative quad')
    px = load_atlas(data)
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

    # Fitted long side. drawBookTabIcon scales by the rect's max side, so a glyph
    # whose ink fills its rect draws at the same size as the other tabs.
    header = (ROOT / 'source' / 'ui' / 'book_tabs_atlas.h').read_text()
    rects = []
    for line in header.splitlines():
        if not line.strip().startswith('{'):
            continue
        nums = [float(v) for v in re.findall(r'[-0-9.]+', line)]
        if len(nums) >= 4:
            rects.append(nums[:4])
    if len(rects) != 9:
        raise SystemExit(f'expected 9 tab rects, found {len(rects)}')

    def fitted(rect):
        x, y, w, h = rect
        x0, y0 = int(np.floor(x)), int(np.floor(y))
        x1, y1 = int(np.ceil(x + w)), int(np.ceil(y + h))
        chip = px[y0:y1, x0:x1]
        ink = chip[:, :, 3] > 30
        ys, xs = np.where(ink)
        if len(xs) == 0:
            raise SystemExit('tab icon has no ink')
        long = max(int(xs.max() - xs.min() + 1), int(ys.max() - ys.min() + 1))
        return long / max(w, h)

    ratios = [fitted(r) for r in rects]
    others = sorted(ratios[:8])
    median = 0.5 * (others[3] + others[4])
    if ratios[8] < 0.90:
        raise SystemExit(f'map tab fill {ratios[8]:.3f} is below 0.90')
    if abs(ratios[8] - median) > 0.08:
        raise SystemExit(f'map tab fill {ratios[8]:.3f} is not near the other icons ({median:.3f})')
    # At the inactive tab size (21 px) the map's long side matches the others.
    if abs(ratios[8] - median) * 21.0 > 2.0:
        raise SystemExit('map tab glyph is more than 2 px off the other icons at 21 px')
    print('check_map_glyph: ok')


if __name__ == '__main__':
    main()
