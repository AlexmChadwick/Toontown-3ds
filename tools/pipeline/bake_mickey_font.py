#!/usr/bin/env python3
"""Bake Toontown's sign font (MickeyFont) into romfs/baked/fonts/mickey.bcfnt.

Upstream loads phase_3/models/fonts/MickeyFont.bam as a StaticTextFont
(ToontownGlobals.getSignFont). The bam has A-Z, digits and a few punctuation
marks, and no lowercase. TextNode falls back to the uppercase glyph, so the
HQ title "Toon Platoon" is drawn as TOON PLATOON. This file does the same:
a-z map onto A-Z.

The sheet is A4. Fill and outline are both opaque, so a citro2d tint
(the title is 0.5, 0.75, 0.7) colours the whole Mickey silhouette. The
stock two-tone stroke is the texture's luminance, which an A4 font cannot
carry separately.

Requires panda3d (the same 1.10.15 the other bakers use) and the resources
tree, or --bam / --palette pointing at a checkout.

    python3 -I tools/pipeline/bake_mickey_font.py
"""
import argparse
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
BAM = ROOT / 'ref' / 'resources' / 'phase_3' / 'models' / 'fonts' / 'MickeyFont.bam'
PALETTE = ROOT / 'ref' / 'resources' / 'phase_3' / 'maps' / 'phase_3_palette_2tmlc_1.rgb'
OUT = ROOT / 'romfs' / 'baked' / 'fonts' / 'mickey.bcfnt'

# Match impress.bcfnt so citro2d's 30/cellHeight scale and the desk's 30 px
# line land on the same baseline.
CELL_W = 31
CELL_H = 31
BASELINE = 23
LINE_FEED = 30
PX_PER_UNIT = 26.0
A4 = 11
BUDGET = 96 * 1024


def morton(x, y):
    m = 0
    for b in range(3):
        m |= ((x >> b) & 1) << (2 * b) | ((y >> b) & 1) << (2 * b + 1)
    return m


def tile_a4(px, w, h):
    out = bytearray(w * h // 2)
    i = 0
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for y in range(8):
                for x in range(8):
                    m = i + morton(x, y)
                    v = px[ty + y][tx + x]
                    out[m >> 1] |= (v << 4) if (m & 1) else v
            i += 64
    return bytes(out)


def sample(img, u, v):
    w, h = img.get_x_size(), img.get_y_size()
    x = min(w - 1, max(0, int(u * w)))
    # Panda v is up; the stored image is top row first.
    y = min(h - 1, max(0, int((1.0 - v) * h)))
    return img.get_alpha(x, y)


def raster(img, dim, uv):
    """Return (left, glyphWidth, charWidth is filled by the caller, pixels[row][col] of A4)."""
    left = int(round(dim[0] * PX_PER_UNIT))
    gw = max(1, int(round((dim[2] - dim[0]) * PX_PER_UNIT)))
    span_x = dim[2] - dim[0]
    span_y = dim[3] - dim[1]
    rows = []
    for py in range(CELL_H):
        row = []
        fy = (BASELINE - (py + 0.5)) / PX_PER_UNIT
        for px in range(gw):
            fx = dim[0] + (px + 0.5) / PX_PER_UNIT
            if span_x <= 0 or span_y <= 0 or fx < dim[0] or fx > dim[2] or fy < dim[1] or fy > dim[3]:
                row.append(0)
                continue
            u = uv[0] + (fx - dim[0]) / span_x * (uv[2] - uv[0])
            v = uv[1] + (fy - dim[1]) / span_y * (uv[3] - uv[1])
            a = sample(img, u, v)
            row.append(0 if a < 0.25 else min(15, int(round(a * 15))))
        rows.append(row)
    return left, gw, rows


def pack(glyphs, advances, cmap):
    """glyphs: list of (left, gw, rows[cellH][gw]). cmap: 95 u16 indices for U+0020..U+007E."""
    n = len(glyphs)
    cols = (256 - 1) // (CELL_W + 1)
    lines_need = (n + cols - 1) // cols
    sh = 8
    while lines_need * (CELL_H + 1) + 2 > sh:
        sh *= 2
    if sh > 512:
        sys.exit(f'sheet height {sh} exceeds 512')
    sw = 256
    px = [[0] * sw for _ in range(sh)]
    max_w = 0
    for g, (left, gw, rows) in enumerate(glyphs):
        max_w = max(max_w, gw, advances[g])
        line, col = divmod(g, cols)
        x0 = col * (CELL_W + 1) + 1
        y0 = line * (CELL_H + 1) + 1
        for y, row in enumerate(rows):
            for x, v in enumerate(row):
                px[y0 + y][x0 + x] = v
    sheet = tile_a4(px, sw, sh)

    # Layout mirrors impress.bcfnt: 20-byte CFNT, FINF, TGLP, sheet at 128.
    tglp_at = 52
    data_at = 128
    cwdh_at = data_at + len(sheet)
    cwdh_body = 8 + 3 * n  # start/end/next + widths, inside the block after magic+size
    # block = 8 header + 8 index header + 3*n widths, padded to 4
    cwdh_size = 16 + 3 * n
    if cwdh_size % 4:
        cwdh_size += 4 - (cwdh_size % 4)
    cmap_at = cwdh_at + cwdh_size
    cmap_size = 24 + 2 * len(cmap)  # header through next, plus the u16 table
    if cmap_size % 4:
        cmap_size += 4 - (cmap_size % 4)
    file_size = cmap_at + cmap_size

    out = bytearray(file_size)
    out[0:4] = b'CFNT'
    struct.pack_into('<HHIII', out, 4, 0xFEFF, 20, 0x03000000, file_size, 4)

    out[20:24] = b'FINF'
    struct.pack_into('<I', out, 24, 32)
    struct.pack_into('<BBH', out, 28, 1, LINE_FEED, 0)  # alter patched below
    struct.pack_into('<bBBB', out, 32, 0, 0, 0, 1)
    struct.pack_into('<III', out, 36, tglp_at + 8, cwdh_at + 8, cmap_at + 8)
    struct.pack_into('<BBBB', out, 48, CELL_H + 8, max_w, BASELINE, 0)

    out[tglp_at:tglp_at + 4] = b'TGLP'
    struct.pack_into('<I', out, tglp_at + 4, cwdh_at - tglp_at)
    nlines = (sh - 2) // (CELL_H + 1)
    struct.pack_into('<BBBBIHHHHHHI', out, tglp_at + 8,
                     CELL_W, CELL_H, BASELINE, min(255, max_w), len(sheet), 1, A4,
                     cols, nlines, sw, sh, data_at)
    out[data_at:data_at + len(sheet)] = sheet

    out[cwdh_at:cwdh_at + 4] = b'CWDH'
    struct.pack_into('<I', out, cwdh_at + 4, cwdh_size)
    struct.pack_into('<HHI', out, cwdh_at + 8, 0, n - 1, 0)
    for i, (left, gw, _) in enumerate(glyphs):
        adv = max(0, min(255, advances[i]))
        struct.pack_into('<bBB', out, cwdh_at + 16 + 3 * i, max(-128, min(127, left)), gw, adv)

    out[cmap_at:cmap_at + 4] = b'CMAP'
    struct.pack_into('<I', out, cmap_at + 4, cmap_size)
    struct.pack_into('<HHHHI', out, cmap_at + 8, 0x20, 0x7E, 1, 0, 0)
    for i, idx in enumerate(cmap):
        struct.pack_into('<H', out, cmap_at + 20 + 2 * i, idx)
    return bytes(out), (sw, sh, cols, nlines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--bam', default=str(BAM))
    ap.add_argument('--palette', default=str(PALETTE))
    ap.add_argument('--out', default=str(OUT))
    args = ap.parse_args()
    if not Path(args.bam).is_file() or not Path(args.palette).is_file():
        sys.exit(f'missing {args.bam} or {args.palette}')

    from panda3d.core import Filename, FontPool, LVecBase4, PNMImage, TexturePool

    font = FontPool.load_font(str(Path(args.bam)))
    tex = TexturePool.load_texture(Filename.from_os_specific(str(Path(args.palette).resolve())))
    img = PNMImage()
    if not tex.store(img) or img.get_num_channels() < 2:
        sys.exit('palette did not load as a luminance-alpha image')

    # Glyph 0 is '?' so a missing code has somewhere to land (alterCharIndex).
    order = ['?']
    for ch in range(0x20, 0x7F):
        c = chr(ch)
        if c == '?' or c == ' ':
            continue
        if c.islower():
            continue
        g = font.get_glyph(ord(c))
        if g.has_quad():
            order.append(c)
    # Space is a real glyph with no quad.
    order.insert(1, ' ')

    glyphs = []
    advances = []
    index = {}
    for ch in order:
        g = font.get_glyph(ord(ch))
        adv = int(round((font.get_space_advance() if ch == ' ' else g.get_advance()) * PX_PER_UNIT))
        if ch == ' ' or not g.has_quad():
            glyphs.append((0, 0, [[0] * 0 for _ in range(CELL_H)]))
        else:
            dim, uv = LVecBase4(), LVecBase4()
            if not g.get_quad(dim, uv):
                sys.exit(f'{ch!r} has no quad')
            left, gw, rows = raster(img, dim, uv)
            if gw > CELL_W:
                sys.exit(f'{ch!r} is {gw}px wide, cell is {CELL_W}')
            glyphs.append((left, gw, rows))
        advances.append(max(1, adv) if ch != ' ' else max(1, adv))
        index[ch] = len(glyphs) - 1

    q = index['?']
    cmap = []
    for code in range(0x20, 0x7F):
        c = chr(code)
        if c in index:
            cmap.append(index[c])
        elif 'a' <= c <= 'z' and c.upper() in index:
            cmap.append(index[c.upper()])
        else:
            cmap.append(q)

    # Patch alter to the '?' glyph. pack() writes 0; set it here via the returned bytes.
    data, (sw, sh, cols, lines) = pack(glyphs, advances, cmap)
    data = bytearray(data)
    struct.pack_into('<H', data, 30, q)  # FINF alterCharIndex
    if len(data) > BUDGET:
        sys.exit(f'font is {len(data)} bytes, over the {BUDGET}-byte budget')
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print(f'{out}: {len(data)} bytes, {len(glyphs)} glyphs, cell {CELL_W}x{CELL_H} '
          f'baseline {BASELINE}, sheet {sw}x{sh} A4 ({cols}x{lines}), "?" glyph {q}')


if __name__ == '__main__':
    main()
