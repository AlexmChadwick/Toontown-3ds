#!/usr/bin/env python3
"""Bake the laff meter (toontown/toon/LaffMeter.py) into a 512x256 RGBA4 atlas.

Every part of phase_3/models/gui/laff_o_meter is rendered offscreen into the same 64x64 frame
(one orthographic view around the union of all parts), so the runtime draws them on top of
each other exactly as the DirectFrames overlay upstream:

    9 species heads (rendered white: the runtime multiplies them by the toon's head colour,
    LaffMeter image_color), frown, smile, eyes, open_smile, tooth_1 .. tooth_6.

Outputs romfs/baked/laff_meter.ttm (one texture, a placeholder quad) and
source/ui/laff_meter_atlas.h (sub-texture rectangles plus where the hp / max labels sit).

    .venv/bin/python -I tools/pipeline/bake_laff_meter.py
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from panda3d.core import loadPrcFileData  # noqa: E402

loadPrcFileData('', 'window-type offscreen\nwin-size 256 256\naudio-library-name null\n'
                    'textures-power-2 none\nframebuffer-alpha 1')

import numpy as np  # noqa: E402
from direct.showbase.ShowBase import ShowBase  # noqa: E402
from panda3d.core import NodePath, OrthographicLens, PNMImage, Point3, TransparencyAttrib  # noqa: E402

import ttm  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
RES = ROOT / 'ref' / 'resources'
OUT = ROOT / 'romfs' / 'baked' / 'laff_meter.ttm'
HEADER = ROOT / 'source' / 'ui' / 'laff_meter_atlas.h'

# LaffMeter.load: the head model per ToonDNA head type (index = ToonDNA species order:
# dog, cat, horse, mouse, rabbit, duck, monkey, bear, pig).
HEADS = ['doghead', 'cathead', 'horsehead', 'mousehead', 'bunnyhead', 'duckhead', 'monkeyhead',
         'bearhead', 'pighead']
PARTS = ['frown', 'smile', 'eyes', 'open_smile', 'tooth_1', 'tooth_2', 'tooth_3', 'tooth_4',
         'tooth_5', 'tooth_6']
CELL = 64
ATLAS_W = 512
ATLAS_H = 256
COLUMNS = ATLAS_W // CELL
RENDER = 256


def main():
    base = ShowBase()
    base.setBackgroundColor(0, 0, 0, 0)
    gui = base.loader.loadModel(str(RES / 'phase_3/models/gui/laff_o_meter.bam'))
    names = HEADS + PARTS
    nodes = [gui.find('**/' + n) for n in names]
    missing = [n for n, np_ in zip(names, nodes) if np_.isEmpty()]
    if missing:
        raise SystemExit(f'missing laff meter parts: {missing}')

    # One frame for all parts: the union of their bounds, square, so overlays line up.
    lo, hi = Point3(1e9, 1e9, 1e9), Point3(-1e9, -1e9, -1e9)
    for np_ in nodes:
        a, b = np_.getTightBounds(gui)
        lo = Point3(min(lo[0], a[0]), min(lo[1], a[1]), min(lo[2], a[2]))
        hi = Point3(max(hi[0], b[0]), max(hi[1], b[1]), max(hi[2], b[2]))
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    size = max(hi[0] - lo[0], hi[2] - lo[2]) * 1.02

    lens = OrthographicLens()
    lens.setFilmSize(size, size)
    lens.setNearFar(-100, 100)
    base.cam.node().setLens(lens)
    base.camera.setPos(cx, -50, cz)
    base.camera.lookAt(cx, 0, cz)

    sheet = PNMImage(ATLAS_W, ATLAS_H, 4)
    sheet.fill(0, 0, 0)
    sheet.alphaFill(0)
    rects = []
    for i, (name, np_) in enumerate(zip(names, nodes)):
        part = np_.copyTo(base.render)
        part.setPos(gui, 0, 0, 0)
        part.setTransparency(TransparencyAttrib.MAlpha)
        if name in HEADS:
            part.setColor(1, 1, 1, 1)  # tinted at runtime (image_color)
        base.graphicsEngine.renderFrame()
        base.graphicsEngine.renderFrame()
        shot = PNMImage()
        base.win.getScreenshot().store(shot)
        cell = PNMImage(CELL, CELL, 4)
        cell.gaussianFilterFrom(1.0, shot)
        x0, y0 = (i % COLUMNS) * CELL, (i // COLUMNS) * CELL
        sheet.copySubImage(cell, x0, y0)
        part.removeNode()
        rects.append((name, x0, y0))

    px = np.empty((ATLAS_H, ATLAS_W, 4), dtype=np.uint8)
    for y in range(ATLAS_H):
        for x in range(ATLAS_W):
            c = sheet.getXelA(x, y)
            px[y, x] = [round(c[0] * 255), round(c[1] * 255), round(c[2] * 255), round(c[3] * 255)]
    texture = ttm.Texture('laff_meter', px, ttm.GPU_RGBA4, ttm.GPU_CLAMP_TO_EDGE, ttm.GPU_CLAMP_TO_EDGE,
                          mipmaps=False)
    quad = np.array([[-1, 0, -1, 0, 0, 1, 1, 1, 1], [1, 0, -1, 1, 0, 1, 1, 1, 1],
                     [1, 0, 1, 1, 1, 1, 1, 1, 1], [-1, 0, 1, 0, 1, 1, 1, 1, 1]], dtype=float)
    batch = ttm.Batch(0, ttm.BLEND_ALPHA, True, 0, 0, quad, np.array([0, 1, 2, 0, 2, 3]))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    stats = ttm.write(OUT, [texture], [batch])
    sheet.write(str(ROOT / 'sdmc-out' / 'laff_meter_preview.png'))
    print(f'{OUT}: {stats["file_bytes"]} bytes')

    # LaffMeter: hpLabel / maxLabel at (-0.398, 0.051) / (0.442, 0.051) under the eyes frame,
    # text_scale 0.4; as fractions of the cell (0 = left / top).
    eyes = nodes[names.index('eyes')]
    ex, ez = eyes.getPos(gui)[0], eyes.getPos(gui)[2]

    def cell_frac(x, z):
        return (x - (cx - size / 2)) / size, ((cz + size / 2) - z) / size

    hp_u, hp_v = cell_frac(ex - 0.398, ez + 0.051)
    max_u, max_v = cell_frac(ex + 0.442, ez + 0.051)
    lines = ['// GENERATED by tools/pipeline/bake_laff_meter.py. Do not edit.', '#pragma once', '',
             'namespace tt::ui {', '',
             f'constexpr int kLaffCell = {CELL};', f'constexpr int kLaffAtlasW = {ATLAS_W};', f'constexpr int kLaffAtlasH = {ATLAS_H};',
             '// Atlas cell origins (pixels, top-left) in this order:']
    lines.append('enum LaffPart : int { ' + ', '.join(
        f'kLaff{"".join(w.capitalize() for w in n.split("_"))}' for n in names) + ', kLaffPartCount };')
    lines.append('constexpr short kLaffCellXY[kLaffPartCount][2] = {')
    for name, x0, y0 in rects:
        lines.append(f'    {{{x0}, {y0}}}, // {name}')
    lines.append('};')
    lines.append('// Label centres as fractions of the cell (LaffMeter hpLabel / maxLabel).')
    lines.append(f'constexpr float kLaffHpLabel[2] = {{{hp_u:.4f}f, {hp_v:.4f}f}};')
    lines.append(f'constexpr float kLaffMaxLabel[2] = {{{max_u:.4f}f, {max_v:.4f}f}};')
    lines.append(f'// Label height as a fraction of the cell (text_scale 0.4 in the eyes frame).')
    lines.append(f'constexpr float kLaffLabelHeight = {0.4 / size:.4f}f;')
    lines += ['', '} // namespace tt::ui', '']
    HEADER.write_text('\n'.join(lines))
    print(f'{HEADER}')


if __name__ == '__main__':
    main()
