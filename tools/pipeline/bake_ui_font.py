#!/usr/bin/env python3
"""Bake Toontown's interface font (ImpressBT) into romfs/baked/fonts/impress.bcfnt.

Upstream draws nearly all of its 2D interface (SpeedChat, the Shtiker Book, nametags,
chat balloons, shop and battle panels) in phase_3/models/fonts/ImpressBT.ttf
(ToontownGlobals.getInterfaceFont). The 3DS client uses the same face on the bottom
screen through citro2d.

devkitPro's mkbcfnt converts the TTF, but it always writes one 1024x1024 A4 sheet
(512 KiB of linear memory for 95 glyphs). This tool runs mkbcfnt and then repacks the
glyph cells into the smallest power-of-two A4 sheet that holds them; glyph indices,
widths and the character map are unchanged, so libctru's glyph lookup is unaffected.

Every UI string in the client is ASCII (checked by this tool's caller list in
docs/design/ui_theme.md), so only U+0020..U+007E are baked; anything else falls back
to the font's replacement glyph.

    python3 -I tools/pipeline/bake_ui_font.py [--ttf path/to/ImpressBT.ttf] [--mkbcfnt path]

Deterministic: the same TTF and mkbcfnt give byte-identical output.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
TTF = ROOT / 'ref' / 'resources' / 'phase_3' / 'models' / 'fonts' / 'ImpressBT.ttf'
OUT = ROOT / 'romfs' / 'baked' / 'fonts' / 'impress.bcfnt'
# Point size handed to mkbcfnt: line height ~30 px, the same as the 3DS system font, so a
# C2D text scale means the same on-screen size with either font.
POINT_SIZE = 18
A4 = 11
BUDGET = 96 * 1024  # bytes of linear memory for the whole file (UI + font atlas budget: 2 MiB)


def find_mkbcfnt(explicit):
    if explicit:
        return explicit
    for base in (os.environ.get('DEVKITPRO'), '/opt/devkitpro'):
        if base and Path(base, 'tools', 'bin', 'mkbcfnt').exists():
            return str(Path(base, 'tools', 'bin', 'mkbcfnt'))
    found = shutil.which('mkbcfnt')
    if not found:
        sys.exit('mkbcfnt not found: set DEVKITPRO or pass --mkbcfnt')
    return found


# PICA textures are stored in 8x8 tiles, tiles in row order, pixels inside a tile in
# Morton (Z) order; rows run top to bottom in memory (libctru's font code samples
# t = 1 - y / height).
def morton(x, y):
    m = 0
    for b in range(3):
        m |= ((x >> b) & 1) << (2 * b) | ((y >> b) & 1) << (2 * b + 1)
    return m


def untile_a4(data, w, h):
    px = [[0] * w for _ in range(h)]
    i = 0
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for m in range(64):
                x = tx + sum(((m >> (2 * b)) & 1) << b for b in range(3))
                y = ty + sum(((m >> (2 * b + 1)) & 1) << b for b in range(3))
                byte = data[(i + m) >> 1]
                px[y][x] = (byte >> 4) if (m & 1) else (byte & 15)
            i += 64
    return px


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


def repack(src):
    """Return a copy of the bcfnt `src` with its glyph sheets packed tightly."""
    if src[:4] != b'CFNT' and src[:4] != b'CFNU':
        sys.exit('not a bcfnt file')
    header_size, = struct.unpack_from('<H', src, 6)
    n_blocks, = struct.unpack_from('<I', src, 16)
    finf = header_size
    if src[finf:finf + 4] != b'FINF':
        sys.exit('FINF block expected after the header')
    tglp_ptr, cwdh_ptr, cmap_ptr = struct.unpack_from('<III', src, finf + 16)
    tglp = tglp_ptr - 8
    if src[tglp:tglp + 4] != b'TGLP':
        sys.exit('TGLP pointer does not point at TGLP')
    (cw, ch, base, maxw, sheet_size, n_sheets, fmt, cols, lines, sw, sh,
     data_ptr) = struct.unpack_from('<BBBBIHHHHHHI', src, tglp + 8)
    if fmt != A4:
        sys.exit(f'sheet format {fmt}, expected A4 ({A4})')
    rest = min(p - 8 for p in (cwdh_ptr, cmap_ptr))
    if rest < data_ptr + sheet_size * n_sheets:
        sys.exit('CWDH/CMAP must follow the sheet data')

    # Glyph count = the highest index any CMAP maps (mkbcfnt numbers glyphs densely).
    n_glyphs = 0
    p = cmap_ptr
    while p:
        b = p - 8
        code_begin, code_end, method, _, nxt = struct.unpack_from('<HHHHI', src, b + 8)
        if method == 0:
            first, = struct.unpack_from('<H', src, b + 20)
            n_glyphs = max(n_glyphs, first + code_end - code_begin + 1)
        elif method == 1:
            for k in range(code_end - code_begin + 1):
                g, = struct.unpack_from('<H', src, b + 20 + 2 * k)
                if g != 0xFFFF:
                    n_glyphs = max(n_glyphs, g + 1)
        else:
            count, = struct.unpack_from('<H', src, b + 20)
            for k in range(count):
                _, g = struct.unpack_from('<HH', src, b + 22 + 4 * k)
                n_glyphs = max(n_glyphs, g + 1)
        p = nxt

    per_sheet = cols * lines
    sheets = [untile_a4(src[data_ptr + s * sheet_size:data_ptr + (s + 1) * sheet_size], sw, sh)
              for s in range(n_sheets)]
    cells = []
    for g in range(n_glyphs):
        s, k = divmod(g, per_sheet)
        r, c = divmod(k, cols)
        # libctru samples a glyph from x = c*(cw+1)+1 and y = r*(ch+1)+2, cw x ch pixels.
        x0, y0 = c * (cw + 1) + 1, r * (ch + 1) + 2
        cells.append([row[x0:x0 + cw] for row in sheets[s][y0:y0 + ch]])

    # Smallest power-of-two sheet (width <= 256, one sheet) that holds every cell.
    best = None
    for w in (64, 128, 256):
        ncols = (w - 1) // (cw + 1)
        need_lines = -(-n_glyphs // ncols)
        h = 8
        while need_lines * (ch + 1) + 2 > h:
            h *= 2
        if h <= 1024 and (best is None or w * h < best[0] * best[1]):
            best = (w, h, ncols)
    nw, nh, ncols = best
    nlines = (nh - 2) // (ch + 1)
    px = [[0] * nw for _ in range(nh)]
    for g, cell in enumerate(cells):
        r, c = divmod(g, ncols)
        x0, y0 = c * (cw + 1) + 1, r * (ch + 1) + 2
        for y, row in enumerate(cell):
            px[y0 + y][x0:x0 + cw] = row
    sheet = tile_a4(px, nw, nh)

    new_data = 0x80 * -(-(tglp + 0x20) // 0x80)
    after = new_data + len(sheet)
    after = 4 * -(-after // 4)
    delta = after - rest

    out = bytearray(src[:tglp + 0x20])
    out += b'\0' * (new_data - len(out))
    out += sheet
    out += b'\0' * (after - len(out))
    out += src[rest:]
    struct.pack_into('<BBBBIHHHHHHI', out, tglp + 8, cw, ch, base, maxw, len(sheet), 1, fmt,
                     ncols, nlines, nw, nh, new_data)
    struct.pack_into('<I', out, tglp + 4, new_data - tglp + len(sheet))
    struct.pack_into('<III', out, finf + 16, tglp_ptr, cwdh_ptr + delta, cmap_ptr + delta)
    # Relocate the CWDH / CMAP chains (their `next` pointers are absolute).
    for head, off in ((cwdh_ptr + delta, 12), (cmap_ptr + delta, 16)):
        p = head
        while p:
            b = p - 8
            nxt, = struct.unpack_from('<I', out, b + off)
            if nxt:
                nxt += delta
                struct.pack_into('<I', out, b + off, nxt)
            p = nxt
    struct.pack_into('<I', out, 12, len(out))
    return bytes(out), (nw, nh, ncols, nlines, n_glyphs, cw, ch, base), n_blocks


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--ttf', default=str(TTF))
    ap.add_argument('--mkbcfnt')
    ap.add_argument('--out', default=str(OUT))
    args = ap.parse_args()
    tool = find_mkbcfnt(args.mkbcfnt)
    with tempfile.TemporaryDirectory() as tmp:
        wl = Path(tmp, 'ascii.txt')
        wl.write_text(' '.join(str(c) for c in range(0x20, 0x7F)))
        raw = Path(tmp, 'raw.bcfnt')
        subprocess.run([tool, '-s', str(POINT_SIZE), '-w', str(wl), '-o', str(raw), args.ttf],
                       check=True, stdout=subprocess.DEVNULL)
        src = raw.read_bytes()
    data, (w, h, cols, lines, glyphs, cw, ch, base), _ = repack(src)
    if len(data) > BUDGET:
        sys.exit(f'font is {len(data)} bytes, over the {BUDGET}-byte budget')
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print(f'{out}: {len(data)} bytes ({len(src)} from mkbcfnt), {glyphs} glyphs, '
          f'cell {cw}x{ch} baseline {base}, sheet {w}x{h} A4 ({cols}x{lines})')


if __name__ == '__main__':
    main()
