#!/usr/bin/env python3
"""Bake DistributedFishingSpot's cast interface (phase_4/models/gui/fishingGui) into an atlas.

Upstream's __makeGui lays these parts over the pond view: the dock boards along the bottom, the
jellybean jar (with its label display) on the left, the fish bucket (with its display) on the
right, the round cast button (white, tinted red / green / yellow / pink per state), the cast
arrow's tip and stretchable tail, and the exit button. Each part is rendered offscreen into its
own atlas cell, aspect preserved and centred; the runtime (source/ui/fishing_panel.cpp) places
them from the gui-space bounds written alongside.

    romfs/baked/fishing_gui.ttm       one 256x256 RGBA8 texture, a placeholder quad
    source/ui/fishing_gui_atlas.h     cell rectangles and gui-space bounds per part

    .venv/bin/python -I tools/pipeline/bake_fishing_gui.py [--res ref/resources]
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
OUT = ROOT / 'romfs' / 'baked' / 'fishing_gui.ttm'
HEADER = ROOT / 'source' / 'ui' / 'fishing_gui_atlas.h'
ATLAS = 256

# (enum name, node names drawn together, cell x, y, w, h). The jar and bucket include their
# label displays (__makeGui reparents display_* under them).
PARTS = [
    ('Boards', ['boards'], 0, 0, 256, 32),
    ('Jar', ['jar', 'display_jar'], 0, 32, 64, 72),
    ('Bucket', ['bucket', 'display_bucket'], 64, 32, 64, 72),
    ('CastButton', ['castButton'], 128, 32, 64, 64),
    ('ArrowTip', ['arrowTip'], 192, 32, 64, 32),
    ('ArrowTail', ['arrowTail'], 192, 64, 64, 32),
    ('ExitUp', ['exit_buttonUp'], 0, 104, 48, 48),
    ('ExitDown', ['exit_buttonDown'], 48, 104, 48, 48),
    ('DisplayJar', ['display_jar'], 96, 104, 64, 40),
    ('DisplayBucket', ['display_bucket'], 160, 104, 64, 48),
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
    gui = base.loader.loadModel(str(args.res / 'phase_4/models/gui/fishingGui.bam'))
    lens = OrthographicLens()
    lens.setNearFar(-100, 100)
    base.cam.node().setLens(lens)

    sheet = PNMImage(ATLAS, ATLAS, 4)
    sheet.fill(0, 0, 0)
    sheet.alphaFill(0)
    rows = []
    for name, nodes, cx0, cy0, cw, ch in PARTS:
        found = [gui.find('**/' + n) for n in nodes]
        if any(f.isEmpty() for f in found):
            raise SystemExit(f'fishingGui is missing {nodes}')
        holder = NodePath('part')
        holder.reparentTo(base.render)
        for f in found:
            c = f.copyTo(holder)
            c.setTransform(f.getTransform(gui))  # gui-space placement
        holder.setTransparency(TransparencyAttrib.MAlpha)
        if name == 'CastButton':
            holder.setColor(1, 1, 1, 1)  # image*_color tints it at runtime
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
        rows.append((name, ix, iy, px_w, px_h, lo[0], lo[2], hi[0], hi[2]))

    px = np.empty((ATLAS, ATLAS, 4), dtype=np.uint8)
    for y in range(ATLAS):
        for x in range(ATLAS):
            c = sheet.getXelA(x, y)
            px[y, x] = [round(c[0] * 255), round(c[1] * 255), round(c[2] * 255), round(c[3] * 255)]
    texture = ttm.Texture('fishing_gui', px, ttm.GPU_RGBA8, ttm.GPU_CLAMP_TO_EDGE, ttm.GPU_CLAMP_TO_EDGE,
                          mipmaps=False)
    quad = np.array([[-1, 0, -1, 0, 0, 1, 1, 1, 1], [1, 0, -1, 1, 0, 1, 1, 1, 1],
                     [1, 0, 1, 1, 1, 1, 1, 1, 1], [-1, 0, 1, 0, 1, 1, 1, 1, 1]], dtype=float)
    batch = ttm.Batch(0, ttm.BLEND_ALPHA, True, 0, 0, quad, np.array([0, 1, 2, 0, 2, 3]))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    stats = ttm.write(OUT, [texture], [batch])
    print(f'{OUT}: {stats["file_bytes"]} bytes')

    lines = ['// GENERATED by tools/pipeline/bake_fishing_gui.py. Do not edit.', '#pragma once', '',
             'namespace tt::ui {', '',
             f'constexpr int kFishGuiAtlas = {ATLAS};',
             'enum FishGuiPart : int { ' + ', '.join(f'kFishGui{r[0]}' for r in rows) + ', kFishGuiPartCount };',
             'struct FishGuiRect {',
             '    float x, y, w, h;           // the part in the atlas (pixels, top-left origin)',
             '    float minX, minZ, maxX, maxZ; // its box in fishingGui space (aspect2d units, +Z up)',
             '};',
             'constexpr FishGuiRect kFishGuiRects[kFishGuiPartCount] = {']
    for name, ix, iy, w, h, x0, z0, x1, z1 in rows:
        lines.append(f'    {{{ix:.2f}f, {iy:.2f}f, {w:.2f}f, {h:.2f}f, {x0:.4f}f, {z0:.4f}f, {x1:.4f}f, {z1:.4f}f}}, // {name}')
    lines += ['};', '', '} // namespace tt::ui', '']
    HEADER.write_text('\n'.join(lines))
    print(HEADER)


if __name__ == '__main__':
    main()
