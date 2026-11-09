#!/usr/bin/env python3
"""320x240 before/after frames for the Map tab glyph and the SpeedChat gap.

Also rewrites romfs/baked/book_tabs.ttm so the Map cell is the sharpened arrow.
Frames are layout composites of the real atlas pixels and the panel rectangles.
They are not Azahar captures.
"""
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'pipeline'))
import ttm  # noqa: E402
from book_tab_glyph import paint_teleport_icon  # noqa: E402

OUT = Path('/opt/cursor/artifacts/gui-r9')
ATLAS = ROOT / 'romfs' / 'baked' / 'book_tabs.ttm'


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
                x = tx * 8 + i % 8
                y = ty * 8 + i // 8
                a, b, g, r = block[m]
                px[y, x] = (r, g, b, a)
    return px


def load_atlas(path):
    data = Path(path).read_bytes()
    off = struct.unpack_from('<I', data, 44)[0]
    w, h, fmt = struct.unpack_from('<HHB', data, off)
    doff, size = struct.unpack_from('<II', data, off + 8)
    raw = data[doff:doff + size]
    px = untile_rgba8(raw, w, h)
    again = ttm.encode(px, ttm.GPU_RGBA8)
    if again != raw:
        raise SystemExit('RGBA8 round trip does not match the atlas')
    return px


def blit(dst, src, x, y, dw, dh):
    if dw < 1 or dh < 1:
        return
    sh, sw = src.shape[:2]
    ys = np.minimum((np.arange(dh) * sh / dh).astype(int), sh - 1)
    xs = np.minimum((np.arange(dw) * sw / dw).astype(int), sw - 1)
    patch = src[ys][:, xs]
    x0, y0 = int(x), int(y)
    x1, y1 = min(dst.shape[1], x0 + dw), min(dst.shape[0], y0 + dh)
    if x0 < 0 or y0 < 0 or x1 <= x0 or y1 <= y0:
        return
    patch = patch[:y1 - y0, :x1 - x0]
    if patch.shape[2] == 4:
        a = patch[:, :, 3:4].astype(np.float32) / 255.0
        dst[y0:y1, x0:x1, :3] = (patch[:, :, :3] * a + dst[y0:y1, x0:x1, :3] * (1 - a)).astype(np.uint8)
    else:
        dst[y0:y1, x0:x1, :3] = patch[:, :, :3]


def rect(img, x, y, w, h, color):
    x0, y0 = max(0, int(x)), max(0, int(y))
    x1, y1 = min(img.shape[1], int(x + w)), min(img.shape[0], int(y + h))
    if x1 > x0 and y1 > y0:
        img[y0:y1, x0:x1] = color


def save_png(img, path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    ppm = path.with_suffix('.ppm')
    h, w = img.shape[:2]
    ppm.write_bytes(f'P6\n{w} {h}\n255\n'.encode() + img[:, :, :3].tobytes())
    subprocess.check_call(['ffmpeg', '-y', '-i', str(ppm), str(path)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ppm.unlink()


def sky(img):
    top = np.array([0x6E, 0xC6, 0xF5], np.float32)
    bot = np.array([0xC4, 0xEA, 0xFF], np.float32)
    for y in range(208):
        t = y / 207.0
        img[y, :] = (top * (1 - t) + bot * t).astype(np.uint8)
    img[208:, :] = (0x52, 0x10, 0x0C)


def book_frame(atlas):
    img = np.zeros((240, 320, 3), np.uint8)
    img[:] = (0xFA, 0xF6, 0xEE)
    rect(img, 0, 212, 320, 28, (0x50, 0x0C, 0x0C))
    # Collage of the nine cells, in page order, at a size that still fits 320 px.
    cells = [(0, 0), (48, 0), (96, 0), (144, 0), (192, 0), (0, 48), (48, 48), (96, 48), (144, 48)]
    for i, (cx, cy) in enumerate(cells):
        blit(img, atlas[cy:cy + 48, cx:cx + 48], 8 + i * 34, 16, 32, 32)
    # The ninth icon, large, so the arrow or the well is obvious.
    blit(img, atlas[48:96, 144:192], 112, 70, 96, 96)
    tab_w = 320 / 9
    for i, (cx, cy) in enumerate(cells):
        tx = i * tab_w
        rect(img, tx + 1, 214, tab_w - 2, 26, (0x82, 0x1C, 0x1C))
        blit(img, atlas[cy:cy + 48, cx:cx + 48], tx + tab_w * 0.5 - 10.5, 216, 21, 21)
    return img


def chat_frame(panel_bottom):
    img = np.zeros((240, 320, 3), np.uint8)
    sky(img)
    rect(img, 0, 176, 62, 64, (0x52, 0x10, 0x0C))
    # SpeedChat frame down to panel_bottom. A long menu fills that edge.
    rect(img, 6, 28, 300, panel_bottom - 28, (0x2A, 0x2A, 0x3C))
    rect(img, 8, 30, 296, panel_bottom - 32, (0xF2, 0xF2, 0xFF))
    y = 28 + 2
    while y + 28 <= panel_bottom:
        rect(img, 16, y + 6, 180, 14, (0x2A, 0x1A, 0x0C))
        y += 30
    # Recent chat, same place either way: just above the tab bar, right of the pocket.
    rect(img, 64, 175, 252, 31, (0x80, 0x80, 0xFF))
    rect(img, 66, 177, 248, 27, (0xF2, 0xF2, 0xFF))
    rect(img, 72, 184, 90, 10, (0x2B, 0x58, 0xA8))
    rect(img, 168, 184, 120, 10, (0x2A, 0x1A, 0x0C))
    return img


def write_atlas(px):
    texture = ttm.Texture('book_tabs', px, ttm.GPU_RGBA8, ttm.GPU_CLAMP_TO_EDGE, ttm.GPU_CLAMP_TO_EDGE,
                          mipmaps=False)
    quad = np.array([[-1, 0, -1, 0, 0, 1, 1, 1, 1], [1, 0, -1, 1, 0, 1, 1, 1, 1],
                     [1, 0, 1, 1, 1, 1, 1, 1, 1], [-1, 0, 1, 0, 1, 1, 1, 1, 1]], dtype=float)
    batch = ttm.Batch(0, ttm.BLEND_ALPHA, True, 0, 0, quad, np.array([0, 1, 2, 0, 2, 3]))
    stats = ttm.write(ATLAS, [texture], [batch])
    print(f'{ATLAS}: {stats["file_bytes"]} bytes')


def main():
    before = load_atlas(ATLAS)
    save_png(book_frame(before), OUT / 'book_tabs_before.png')
    save_png(chat_frame(174), OUT / 'chat_log_before.png')
    after = before.copy()
    unchanged = after.copy()
    paint_teleport_icon(after, 144, 48, 48, 48)
    mask = np.ones(after.shape[:2], dtype=bool)
    mask[48:96, 144:192] = False
    if not np.array_equal(after[mask], unchanged[mask]):
        raise SystemExit('paint leaked outside the Map cell')
    write_atlas(after)
    save_png(book_frame(after), OUT / 'book_tabs_after.png')
    save_png(chat_frame(170), OUT / 'chat_log_after.png')
    print('frames', OUT)


if __name__ == '__main__':
    main()
