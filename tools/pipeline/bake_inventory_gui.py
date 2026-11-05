#!/usr/bin/env python3
"""Bake the battle inventory's art (InventoryNew + TownBattle) into one two-texture RomFS atlas.

InventoryNew draws each gag track as an InventoryRow tinted with the track colour, a rounded
InventoryButton per gag (Up / Down / Rollover / Flat, tinted PressableImageColor blue) carrying
the gag's inventory_icons geom, all inside battle_gui's red BATTLE_Menu frame. TownBattleToonPanel
puts each toon's laff meter on ToonBtl_Status_BG. Every piece here is the original model, rendered
offscreen (aspect preserved, centred in its cell):

    texture 0  256x256 RGBA8  the 49 gag icons, cell (level * 36, track * 36, 36, 36)
    texture 1  256x256 RGBA8  InventoryRow, the four InventoryButton states, BATTLE_Menu,
                             ToonBtl_Status_BG and the stock dialog card used by the battle hint

    romfs/baked/inventory_gui.ttm      the two textures and a placeholder quad
    source/ui/inventory_gui_atlas.h    the rectangles (pixels, top-left origin)

Icons are framed by their own tight bounds so every gag reads at the same size (the old 28px
SD atlas did the same at a size too small for the bottom screen). The grey pieces stay grey:
the runtime tints them multiplicatively, like DirectGui's image_color.

    .venv/bin/python -I tools/pipeline/bake_inventory_gui.py [--res ref/resources] [--up ref/open-toontown] [--png DIR]
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
OUT = ROOT / 'romfs' / 'baked' / 'inventory_gui.ttm'
HEADER = ROOT / 'source' / 'ui' / 'inventory_gui_atlas.h'
INV = 'phase_3.5/models/gui/inventory_gui'
BAT = 'phase_3.5/models/gui/battle_gui'
ICONS = 'phase_3.5/models/gui/inventory_icons'
DIALOG = 'phase_3/models/gui/dialog_box_gui'
ICON_CELL = 36
# (name, model, node, cell (x, y, w, h) in texture 1)
GUI = [
    ('Row', INV, 'InventoryRow', (0, 0, 256, 18)),
    ('ButtonUp', INV, 'InventoryButtonUp', (0, 20, 64, 41)),
    ('ButtonDown', INV, 'InventoryButtonDown', (64, 20, 64, 41)),
    ('ButtonRollover', INV, 'InventoryButtonRollover', (128, 20, 64, 41)),
    ('ButtonFlat', INV, 'InventoryButtonFlat', (192, 20, 64, 41)),
    ('Menu', BAT, 'BATTLE_Menu', (0, 62, 134, 64)),
    ('ToonStatus', BAT, 'ToonBtl_Status_BG', (136, 62, 100, 63)),
    ('Hint', DIALOG, 'g1', (0, 128, 128, 124)),
]


class Renderer:
    def __init__(self, res):
        self.res = Path(res)
        self.base = ShowBase()
        self.base.setBackgroundColor(0, 0, 0, 0)
        self.lens = OrthographicLens()
        self.lens.setNearFar(-100, 100)
        self.base.cam.node().setLens(self.lens)
        self.models = {}

    def node(self, model, name):
        if model not in self.models:
            self.models[model] = self.base.loader.loadModel(str(self.res / (model + '.bam')))
        gui = self.models[model]
        f = gui.find('**/' + name)
        if f.isEmpty():
            raise SystemExit(f'{model} is missing {name}')
        return gui, f

    def render(self, model, name, sheet, cell, square=False):
        """Draws the node into `cell` of `sheet`; returns its rectangle (x, y, w, h) in pixels."""
        gui, f = self.node(model, name)
        holder = NodePath('part')
        holder.reparentTo(self.base.render)
        c = f.copyTo(holder)
        c.setTransform(f.getTransform(gui))
        holder.setTransparency(TransparencyAttrib.MAlpha)
        cx0, cy0, cw, ch = cell
        lo, hi = holder.getTightBounds()
        bw, bh = hi[0] - lo[0], hi[2] - lo[2]
        if square:
            bw = bh = max(bw, bh)
        aspect = cw / ch
        fw = max(bw, bh * aspect) * 1.02
        fh = fw / aspect
        self.lens.setFilmSize(fw, fh)
        mx, mz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
        self.base.camera.setPos(mx, -50, mz)
        self.base.camera.lookAt(mx, 0, mz)
        self.base.graphicsEngine.renderFrame()
        self.base.graphicsEngine.renderFrame()
        shot = PNMImage()
        self.base.win.getScreenshot().store(shot)
        img = PNMImage(cw, ch, 4)
        img.gaussianFilterFrom(1.0, shot)
        sheet.copySubImage(img, cx0, cy0)
        holder.removeNode()
        px_w, px_h = bw / fw * cw, bh / fh * ch
        return (cx0 + (cw - px_w) / 2, cy0 + (ch - px_h) / 2, px_w, px_h)


def literal_table(up, path, name):
    import ast
    tree = ast.parse((Path(up) / path).read_text())
    return ast.literal_eval(next(n.value for n in tree.body if isinstance(n, ast.Assign)
                                 and any(isinstance(t, ast.Name) and t.id == name for t in n.targets)))


def new_sheet(w, h):
    s = PNMImage(w, h, 4)
    s.fill(0, 0, 0)
    s.alphaFill(0)
    return s


def to_rgba(sheet):
    w, h = sheet.getXSize(), sheet.getYSize()
    px = np.empty((h, w, 4), dtype=np.uint8)
    for y in range(h):
        for x in range(w):
            c = sheet.getXelA(x, y)
            px[y, x] = [round(c[0] * 255), round(c[1] * 255), round(c[2] * 255), round(c[3] * 255)]
    return px


def main():
    ap = argparse.ArgumentParser()
    default = ROOT / 'ref' / 'resources'
    if not (default / 'phase_3.5').is_dir():
        default = ROOT / 'ref' / 'ref' / 'resources'
    ap.add_argument('--res', type=Path, default=default)
    ap.add_argument('--up', type=Path, default=default.parent / 'open-toontown', help='open-toontown checkout')
    ap.add_argument('--png', type=Path, help='also write both textures as PNGs into this directory')
    args = ap.parse_args()

    r = Renderer(args.res)
    props = literal_table(args.up, 'toontown/toonbase/ToontownBattleGlobals.py', 'AvPropsNew')
    assert len(props) == 7 and all(len(row) == 7 for row in props)
    icons = new_sheet(256, 256)
    for t, row in enumerate(props):
        for lv, name in enumerate(row):
            # One pixel of clear border so linear filtering never bleeds a neighbour in.
            r.render(ICONS, name, icons, (lv * ICON_CELL + 1, t * ICON_CELL + 1, ICON_CELL - 2, ICON_CELL - 2),
                     square=True)
    gui = new_sheet(256, 256)
    rows = [(name, *r.render(model, node, gui, cell)) for name, model, node, cell in GUI]
    if args.png:
        args.png.mkdir(parents=True, exist_ok=True)
        icons.write(str(args.png / 'inventory_icons.png'))
        gui.write(str(args.png / 'inventory_gui.png'))

    textures = [ttm.Texture('inventory_icons', to_rgba(icons), ttm.GPU_RGBA8, ttm.GPU_CLAMP_TO_EDGE,
                            ttm.GPU_CLAMP_TO_EDGE, mipmaps=False),
                ttm.Texture('inventory_gui', to_rgba(gui), ttm.GPU_RGBA8, ttm.GPU_CLAMP_TO_EDGE,
                            ttm.GPU_CLAMP_TO_EDGE, mipmaps=False)]
    quad = np.array([[-1, 0, -1, 0, 0, 1, 1, 1, 1], [1, 0, -1, 1, 0, 1, 1, 1, 1],
                     [1, 0, 1, 1, 1, 1, 1, 1, 1], [-1, 0, 1, 0, 1, 1, 1, 1, 1]], dtype=float)
    batch = ttm.Batch(0, ttm.BLEND_ALPHA, True, 0, 0, quad, np.array([0, 1, 2, 0, 2, 3]))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    stats = ttm.write(OUT, textures, [batch])
    print(f'{OUT}: {stats["file_bytes"]} bytes')

    lines = ['// GENERATED by tools/pipeline/bake_inventory_gui.py. Do not edit.', '#pragma once', '',
             'namespace tt::ui {', '',
             '// Texture 0: the gag icons, cell (level * kInvIconCell, track * kInvIconCell), one clear pixel',
             '// of border. Texture 1: the pieces below.',
             'constexpr int kInvIconTexW = 256, kInvIconTexH = 256, kInvIconCell = %d;' % ICON_CELL,
             'constexpr int kInvGuiTexW = 256, kInvGuiTexH = 256;',
             'enum class InvArt : int { ' + ', '.join(row[0] for row in rows) + ', Count };',
             'struct InvArtRect {',
             '    float x, y, w, h; // the piece in texture 1 (pixels, top-left origin)',
             '};',
             f'constexpr InvArtRect kInvArtRects[{len(rows)}] = {{']
    for name, x, y, w, h in rows:
        lines.append(f'    {{{x:.2f}f, {y:.2f}f, {w:.2f}f, {h:.2f}f}}, // {name}')
    lines += ['};', '', '} // namespace tt::ui', '']
    HEADER.write_text('\n'.join(lines))
    print(HEADER)


if __name__ == '__main__':
    main()
