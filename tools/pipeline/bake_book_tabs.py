#!/usr/bin/env python3
"""Bake the Shtiker Book's page tab icons (ShtikerBook.addPageTab) into one atlas.

Upstream gives every page tab an icon: the cream pie (inventory_icons) for Gags, the quest card
for ToonTasks, sos_textures' gear / disguise / fish / kart / switch / teleport icons for the Cog
Gallery, Cog Suits (geom_color 0.7), Fish, Trophies & Records (the kart page's icon), Options and
Map, and the playing card back with the Toontown logo for SOS Toons. Each is rendered offscreen
into a 48 x 48 cell, aspect preserved and centred.

    romfs/baked/book_tabs.ttm       one 256x128 RGBA8 texture, a placeholder quad
    source/ui/book_tabs_atlas.h     cell rectangles per icon, in Book::PageIndex order

    .venv/bin/python -I tools/pipeline/bake_book_tabs.py [--res ref/resources]
"""
import argparse
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from panda3d.core import loadPrcFileData  # noqa: E402

loadPrcFileData('', 'window-type offscreen\nwin-size 512 512\naudio-library-name null\n'
                    'textures-power-2 none\nframebuffer-alpha 1')

import numpy as np  # noqa: E402
from direct.showbase.ShowBase import ShowBase  # noqa: E402
from panda3d.core import NodePath, OrthographicLens, PNMImage, TransparencyAttrib  # noqa: E402

import ttm  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'romfs' / 'baked' / 'book_tabs.ttm'
HEADER = ROOT / 'source' / 'ui' / 'book_tabs_atlas.h'
ATLAS_W, ATLAS_H, CELL = 256, 128, 48

# (enum name, model, [(node, colour)]), in Book::PageIndex order.
SOS = 'phase_3.5/models/gui/sos_textures'
WHITE, GREY = (1, 1, 1, 1), (0.7, 0.7, 0.7, 1)
PARTS = [
    ('Gags', 'phase_3.5/models/gui/inventory_icons', [('inventory_tart', WHITE)]),
    ('Tasks', 'phase_3.5/models/gui/stickerbook_gui', [('questCard', WHITE)]),
    ('Cogs', SOS, [('gui_gear', WHITE)]),
    ('Suits', SOS, [('disguise2', GREY)]),
    ('Fish', SOS, [('fish', WHITE)]),
    ('Trophies', SOS, [('kartIcon', WHITE)]),
    ('Sos', 'phase_3.5/models/gui/playingCard', [('card_back', WHITE), ('logo', WHITE)]),
    ('Options', SOS, [('switch', WHITE)]),
    ('Map', SOS, [('teleportIcon', WHITE)]),
]


def main():
    ap = argparse.ArgumentParser()
    default = ROOT / 'ref' / 'resources'
    if not (default / 'phase_4').is_dir():
        default = ROOT / 'ref' / 'ref' / 'resources'
    ap.add_argument('--res', type=Path, default=default)
    args = ap.parse_args()

    base = ShowBase()
    base.setBackgroundColor(0, 0, 0, 0)
    lens = OrthographicLens()
    lens.setNearFar(-100, 100)
    base.cam.node().setLens(lens)

    sheet = PNMImage(ATLAS_W, ATLAS_H, 4)
    sheet.fill(0, 0, 0)
    sheet.alphaFill(0)
    rows = []
    cols = ATLAS_W // CELL
    for index, (name, model, nodes) in enumerate(PARTS):
        cx0, cy0, cw, ch = (index % cols) * CELL, (index // cols) * CELL, CELL, CELL
        gui = base.loader.loadModel(str(args.res / (model + '.bam')))
        found = [(gui.find('**/' + n), color) for n, color in nodes]
        if any(f.isEmpty() for f, _ in found):
            raise SystemExit(f'{model} is missing {nodes}')
        holder = NodePath('part')
        holder.reparentTo(base.render)
        for f, color in found:
            c = f.copyTo(holder)
            c.setTransform(f.getTransform(gui))
            c.setColorScale(*color)
        holder.setTransparency(TransparencyAttrib.MAlpha)
        lo, hi = holder.getTightBounds()
        bw, bh = hi[0] - lo[0], hi[2] - lo[2]
        aspect = cw / ch
        fw = max(bw, bh * aspect) * 1.02
        fh = fw / aspect
        lens.setFilmSize(fw, fh)
        mx, mz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
        base.camera.setPos(mx, -50, mz)
        base.camera.lookAt(mx, 0, mz)
        base.graphicsEngine.renderFrame()
        base.graphicsEngine.renderFrame()
        shot = PNMImage()
        base.win.getScreenshot().store(shot)
        cell = PNMImage(cw, ch, 4)
        cell.gaussianFilterFrom(1.0, shot)
        sheet.copySubImage(cell, cx0, cy0)
        holder.removeNode()
        # The content's rectangle inside the cell (pixels) and its gui-space box.
        px_w, px_h = bw / fw * cw, bh / fh * ch
        ix, iy = cx0 + (cw - px_w) / 2, cy0 + (ch - px_h) / 2
        rows.append((name, ix, iy, px_w, px_h))

    px = np.empty((ATLAS_H, ATLAS_W, 4), dtype=np.uint8)
    for y in range(ATLAS_H):
        for x in range(ATLAS_W):
            c = sheet.getXelA(x, y)
            px[y, x] = [round(c[0] * 255), round(c[1] * 255), round(c[2] * 255), round(c[3] * 255)]
    texture = ttm.Texture('book_tabs', px, ttm.GPU_RGBA8, ttm.GPU_CLAMP_TO_EDGE, ttm.GPU_CLAMP_TO_EDGE,
                          mipmaps=False)
    quad = np.array([[-1, 0, -1, 0, 0, 1, 1, 1, 1], [1, 0, -1, 1, 0, 1, 1, 1, 1],
                     [1, 0, 1, 1, 1, 1, 1, 1, 1], [-1, 0, 1, 0, 1, 1, 1, 1, 1]], dtype=float)
    batch = ttm.Batch(0, ttm.BLEND_ALPHA, True, 0, 0, quad, np.array([0, 1, 2, 0, 2, 3]))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    stats = ttm.write(OUT, [texture], [batch])
    print(f'{OUT}: {stats["file_bytes"]} bytes')

    lines = ['// GENERATED by tools/pipeline/bake_book_tabs.py. Do not edit.', '#pragma once', '',
             'namespace tt::ui {', '',
             f'constexpr int kBookTabAtlasW = {ATLAS_W}, kBookTabAtlasH = {ATLAS_H};',
             f'constexpr int kBookTabIcons = {len(rows)}; // Book::PageIndex order',
             'struct BookTabRect {',
             '    float x, y, w, h; // the icon in the atlas (pixels, top-left origin)',
             '};',
             'constexpr BookTabRect kBookTabRects[kBookTabIcons] = {']
    for name, ix, iy, w, h in rows:
        lines.append(f'    {{{ix:.2f}f, {iy:.2f}f, {w:.2f}f, {h:.2f}f}}, // {name}')
    lines += ['};', '', '} // namespace tt::ui', '']
    HEADER.write_text('\n'.join(lines))
    print(HEADER)


if __name__ == '__main__':
    main()
