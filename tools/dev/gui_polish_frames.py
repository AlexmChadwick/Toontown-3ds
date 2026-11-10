#!/usr/bin/env python3
"""Before/after bottom screens for the SpeedChat rows, the map card and the Map tab.

Befores are the Azahar Old 3DS captures at 5f761f2 (320x240). Afters composite
those pixels into the layout in source/ui/panel_geometry.h: four whole SpeedChat
rows, a cream map card with a red border and a short shadow, and the enlarged
Map tab glyph. This is not an emulator run.

The script also rewrites only the Map cell of romfs/baked/book_tabs.ttm.
"""
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'pipeline'))
import ttm  # noqa: E402
from book_tab_glyph import paint_teleport_icon  # noqa: E402

OUT = ROOT / 'docs' / 'shots' / 'gui-polish'
ATLAS = ROOT / 'romfs' / 'baked' / 'book_tabs.ttm'
CAPTURES = {
    'book_map': Path('/home/ubuntu/.cursor/projects/agent-repos-Toontown-3ds/assets/a_book_map_bottom.png'),
    'pocket_map': Path('/home/ubuntu/.cursor/projects/agent-repos-Toontown-3ds/assets/f_pocket_laff_rest_bottom.png'),
    'speedchat': Path('/home/ubuntu/.cursor/projects/agent-repos-Toontown-3ds/assets/b_speedchat_menu17_bottom.png'),
}

# layoutMapCard(4, 27, 312, 117, 1, true) and layoutMapCard(4, 4, 312, 140, 1, true).
BOOK = dict(x=66.5, y=27.0, w=187.0, h=117.0, shadow=3.0, map=(71.5, 32.0, 107.0, 107.0), legend=(182.5, 32.0))
POCKET = dict(x=55.0, y=4.0, w=210.0, h=140.0, shadow=3.0, map=(60.0, 9.0, 130.0, 130.0), legend=(194.0, 9.0))
PAGE = (250, 246, 238)
PAPER = (255, 248, 228)
BINDER = (122, 30, 24)
BINDER_DARK = (82, 16, 12)
SHADE = np.array([241, 230, 200])
FRAME = (242, 242, 255)
GREEN = (135, 230, 135)
ARROW = (128, 128, 255)
GOLD = (245, 184, 26)


def layout_map_card(ox, oy, ow, oh, aspect, want_legend):
    """Mirror of layout::layoutMapCard. The asserts below keep it on the C++ numbers."""
    border, pad, shadow = 2.0, 3.0, 3.0
    inset = border + pad
    legend_w, gap, row, rows = 66.0, 4.0, 15.0, 7
    if aspect < 0.25:
        aspect = 0.25
    if aspect > 4.0:
        aspect = 4.0
    inner_h = oh - 2.0 * inset
    inner_w = ow - 2.0 * inset
    legend_need = rows * row
    legend = want_legend and inner_w > legend_w + gap + 48.0 and inner_h + 0.01 >= legend_need
    legend_block = legend_w + gap if legend else 0.0
    room_w = inner_w - legend_block
    map_w, map_h = 1.0, 1.0
    if room_w > 0.0 and inner_h > 0.0:
        if room_w / inner_h < aspect:
            map_w = room_w
            map_h = map_w / aspect
        else:
            map_h = inner_h
            map_w = map_h * aspect
    content_h = map_h
    if legend and legend_need > content_h:
        content_h = legend_need if legend_need < inner_h else inner_h
    content_w = map_w + legend_block
    card_w = content_w + 2.0 * inset
    card_h = content_h + 2.0 * inset
    x = ox + (ow - card_w) * 0.5
    y = oy + (oh - card_h) * 0.5
    return dict(x=x, y=y, w=card_w, h=card_h, shadow=shadow,
                map=(x + inset, y + inset + (content_h - map_h) * 0.5, map_w, map_h),
                legend=(x + inset + map_w + gap, y + inset))


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
                px[ty * 8 + i // 8, tx * 8 + i % 8] = (block[m][3], block[m][2], block[m][1], block[m][0])
    return px


def load_atlas(path):
    data = Path(path).read_bytes()
    off = struct.unpack_from('<I', data, 44)[0]
    w, h, fmt = struct.unpack_from('<HHB', data, off)
    doff, size = struct.unpack_from('<II', data, off + 8)
    raw = data[doff:doff + size]
    px = untile_rgba8(raw, w, h)
    if ttm.encode(px, ttm.GPU_RGBA8) != raw:
        raise SystemExit('RGBA8 round trip does not match the atlas')
    return px


def write_atlas(px):
    texture = ttm.Texture('book_tabs', px, ttm.GPU_RGBA8, ttm.GPU_CLAMP_TO_EDGE, ttm.GPU_CLAMP_TO_EDGE,
                          mipmaps=False)
    quad = np.array([[-1, 0, -1, 0, 0, 1, 1, 1, 1], [1, 0, -1, 1, 0, 1, 1, 1, 1],
                     [1, 0, 1, 1, 1, 1, 1, 1, 1], [-1, 0, 1, 0, 1, 1, 1, 1, 1]], dtype=float)
    batch = ttm.Batch(0, ttm.BLEND_ALPHA, True, 0, 0, quad, np.array([0, 1, 2, 0, 2, 3]))
    stats = ttm.write(ATLAS, [texture], [batch])
    data = bytearray(ATLAS.read_bytes())
    data[6] = 0
    ATLAS.write_bytes(data)
    print(f'{ATLAS}: {stats["file_bytes"]} bytes, flags 0')


def round_rect(img, x, y, w, h, radius, color):
    x0, y0 = int(np.floor(x)), int(np.floor(y))
    x1, y1 = int(np.ceil(x + w)), int(np.ceil(y + h))
    x0c, y0c = max(0, x0), max(0, y0)
    x1c, y1c = min(img.shape[1], x1), min(img.shape[0], y1)
    if x1c <= x0c or y1c <= y0c:
        return
    yy, xx = np.mgrid[y0c:y1c, x0c:x1c]
    lx = xx + 0.5 - x
    ly = yy + 0.5 - y
    inside = (lx >= 0) & (lx < w) & (ly >= 0) & (ly < h)
    r = radius
    corners = (
        ((lx < r) & (ly < r), r, r),
        ((lx >= w - r) & (ly < r), w - r, r),
        ((lx < r) & (ly >= h - r), r, h - r),
        ((lx >= w - r) & (ly >= h - r), w - r, h - r),
    )
    cut = np.zeros(inside.shape, dtype=bool)
    for box, cx, cy in corners:
        cut |= box & (((lx - cx) ** 2 + (ly - cy) ** 2) > r * r)
    mask = inside & ~cut
    view = img[y0c:y1c, x0c:x1c]
    view[mask] = color


def draw_card(img, card):
    round_rect(img, card['x'] + card['shadow'], card['y'] + card['shadow'], card['w'], card['h'], 8, BINDER_DARK)
    round_rect(img, card['x'], card['y'], card['w'], card['h'], 8, BINDER)
    round_rect(img, card['x'] + 2, card['y'] + 2, card['w'] - 4, card['h'] - 4, 6, PAPER)


def paste_map(img, src, src_box, dest):
    x0, y0, x1, y1 = src_box
    crop = src[y0:y1, x0:x1]
    if crop.shape[1] != int(dest[2]) or crop.shape[0] != int(dest[3]):
        raise SystemExit(f'map crop {crop.shape} does not match the card {dest}')
    dx, dy = int(round(dest[0])), int(round(dest[1]))
    img[dy:dy + crop.shape[0], dx:dx + crop.shape[1]] = crop


def paste_legend(img, src, box, legend_xy):
    x0, y0, x1, y1 = box
    region = src[y0:y1, x0:x1]
    dist = np.max(np.abs(region.astype(np.int16) - SHADE), axis=2)
    page = np.max(np.abs(region.astype(np.int16) - np.array(PAGE)), axis=2)
    ink = (dist >= 36) & (page >= 36)
    ys, xs = np.where(ink)
    if len(xs) < 20:
        raise SystemExit('legend ink is missing')
    shift_x = int(round(legend_xy[0])) - int(xs.min() + x0)
    shift_y = int(round(legend_xy[1])) - int(ys.min() + y0)
    dy = ys + y0 + shift_y
    dx = xs + x0 + shift_x
    keep = (dy >= 0) & (dy < img.shape[0]) & (dx >= 0) & (dx < img.shape[1])
    img[dy[keep], dx[keep]] = region[ys[keep], xs[keep]]


def sky_row(src, y):
    return src[y, 0].copy()


def book_after(src):
    img = src.copy()
    img[26:148, 3:317] = PAGE
    draw_card(img, BOOK)
    paste_map(img, src, (74, 32, 181, 139), BOOK['map'])
    paste_legend(img, src, (236, 30, 310, 128), BOOK['legend'])
    return img


def pocket_after(src):
    img = src.copy()
    for y in range(148):
        img[y, :] = sky_row(src, y)
    draw_card(img, POCKET)
    paste_map(img, src, (62, 9, 192, 139), POCKET['map'])
    paste_legend(img, src, (236, 8, 310, 110), POCKET['legend'])
    return img


def speedchat_after(src):
    img = src.copy()
    bands = [(39, 51), (68, 81), (99, 111), (129, 141)]
    strips = [src[y0:y1, 14:300].copy() for y0, y1 in bands]
    img[31:168, 14:300] = FRAME
    img[31:64, 10:302] = GREEN
    old_centers = [45.0, 74.5, 105.0, 135.0]
    for i, ((y0, y1), strip) in enumerate(zip(bands, strips)):
        new_center = 30.0 + 17.5 + i * 35.0
        dest = int(round(new_center - (old_centers[i] - y0)))
        img[dest:dest + (y1 - y0), 14:300] = strip
    img[31:168, 310:314] = FRAME
    img[30:110, 310:314] = ARROW
    fill_triangle(img, 307, 160, 317, 160, 312, 166, ARROW)
    return img


def paint_tab(img, atlas):
    sub = img[214:238, 288:316]
    r = sub[:, :, 0].astype(np.int16)
    g = sub[:, :, 1].astype(np.int16)
    b = sub[:, :, 2].astype(np.int16)
    gold = (np.abs(r - 245) < 40) & (np.abs(g - 184) < 40) & (b < 90)
    cover = (r < 150) & (g < 60) & (b < 60)
    sub[~gold & ~cover] = GOLD
    # Active tab box is 24 px. The UV rect is 47.06 of the 48 px cell.
    scaled = resize_rgba(atlas[48:96, 144:192], 24, 24)
    x, y = 290, 213
    patch = scaled.astype(np.float32)
    alpha = patch[:, :, 3:4] / 255.0
    dest = img[y:y + 24, x:x + 24].astype(np.float32)
    img[y:y + 24, x:x + 24] = (patch[:, :, :3] * alpha + dest * (1.0 - alpha)).astype(np.uint8)
    return img


def save(img, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    ppm = path.with_suffix('.ppm')
    h, w = img.shape[:2]
    ppm.write_bytes(f'P6\n{w} {h}\n255\n'.encode() + np.ascontiguousarray(img[:, :, :3]).tobytes())
    subprocess.check_call(['ffmpeg', '-y', '-i', str(ppm), str(path)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ppm.unlink()
    print(path, w, h)


def load_png(path):
    ppm = path.with_suffix('.load.ppm')
    subprocess.check_call(['ffmpeg', '-y', '-i', str(path), str(ppm)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    raw = ppm.read_bytes()
    ppm.unlink()
    header, _, rest = raw.partition(b'\n')
    if header != b'P6':
        raise SystemExit(f'{path} did not decode as P6')
    dims, _, rest = rest.partition(b'\n')
    w, h = (int(v) for v in dims.split())
    _max, _, body = rest.partition(b'\n')
    return np.frombuffer(body, dtype=np.uint8).reshape(h, w, 3).copy()


def resize_rgba(src, dw, dh):
    sh, sw = src.shape[:2]
    ys = np.minimum((np.arange(dh) * sh / dh).astype(int), sh - 1)
    xs = np.minimum((np.arange(dw) * sw / dw).astype(int), sw - 1)
    return src[ys][:, xs]


def fill_triangle(img, ax, ay, bx, by, cx, cy, color):
    y0, y1 = int(min(ay, by, cy)), int(max(ay, by, cy))
    for y in range(max(0, y0), min(img.shape[0] - 1, y1) + 1):
        xs = []
        for (y_a, x_a), (y_b, x_b) in (((ay, ax), (by, bx)), ((by, bx), (cy, cx)), ((cy, cx), (ay, ax))):
            if (y_a <= y <= y_b) or (y_b <= y <= y_a):
                if y_a == y_b:
                    xs.extend((x_a, x_b))
                else:
                    t = (y - y_a) / (y_b - y_a)
                    xs.append(x_a + t * (x_b - x_a))
        if len(xs) < 2:
            continue
        x0, x1 = int(round(min(xs))), int(round(max(xs)))
        img[y, max(0, x0):min(img.shape[1], x1 + 1)] = color


def main():
    book = layout_map_card(4, 27, 312, 117, 1, True)
    pocket = layout_map_card(4, 4, 312, 140, 1, True)
    for got, exp, name in ((book, BOOK, 'book'), (pocket, POCKET, 'pocket')):
        for key in ('x', 'y', 'w', 'h'):
            if abs(got[key] - exp[key]) > 0.05:
                raise SystemExit(f'{name} card {key} {got[key]} != {exp[key]}')
        if any(abs(a - b) > 0.05 for a, b in zip(got['map'], exp['map'])):
            raise SystemExit(f'{name} map {got["map"]} != {exp["map"]}')
    OUT.mkdir(parents=True, exist_ok=True)
    for name, src in CAPTURES.items():
        dest = OUT / f'{name}_before.png'
        if not dest.exists():
            if not src.exists():
                raise SystemExit(f'missing capture {src}')
            shutil.copyfile(src, dest)
    before = {name: load_png(OUT / f'{name}_before.png') for name in CAPTURES}
    for name, img in before.items():
        if img.shape != (240, 320, 3):
            raise SystemExit(f'{name} before is {img.shape}, expected 320x240')

    atlas = load_atlas(ATLAS)
    painted = atlas.copy()
    paint_teleport_icon(painted, 144, 48, 48, 48)
    mask = np.ones(painted.shape[:2], dtype=bool)
    mask[48:96, 144:192] = False
    if not np.array_equal(painted[mask], atlas[mask]):
        raise SystemExit('paint leaked outside the Map cell')
    write_atlas(painted)

    book_img = paint_tab(book_after(before['book_map']), painted)
    pocket_img = pocket_after(before['pocket_map'])
    chat_img = speedchat_after(before['speedchat'])
    for img, name in ((book_img, 'book_map'), (pocket_img, 'pocket_map'), (chat_img, 'speedchat')):
        if img.shape != (240, 320, 3):
            raise SystemExit(f'{name} after is {img.shape}')
        # The card shadow stays on the 320x240 screen and above the hood row.
        if name != 'speedchat':
            card = BOOK if name == 'book_map' else POCKET
            right = card['x'] + card['w'] + card['shadow']
            bottom = card['y'] + card['h'] + card['shadow']
            if right > 320 or bottom > 240 or bottom > 148:
                raise SystemExit(f'{name} card shadow leaves the screen ({right}, {bottom})')
        save(img, OUT / f'{name}_after.png')


if __name__ == '__main__':
    main()
