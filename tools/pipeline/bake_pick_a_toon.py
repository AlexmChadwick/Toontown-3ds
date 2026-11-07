#!/usr/bin/env python3
"""Bake the Pick-A-Toon screen (AvatarChooser / AvatarChoice) for the bottom screen.

Upstream fills the screen with tt_m_gui_pat_mainGui's tt_t_gui_pat_background (the blue
starburst) and puts one AvatarChoice panel per slot at AvatarChooser.POSITIONS, each a
coloured square (tt_t_gui_pat_square{Red,Green,Purple,Blue,Pink,Yellow}, scale 1.01). The
4:3 aspect2d frame maps exactly onto the 320 x 240 bottom screen (120 px per unit), so the
background and the squares are composited at rest into one screen image. The trashcan_gui
delete button (closed / open) is a separate atlas.

    romfs/baked/pick_a_toon.ttm     texture 0: 512x256 RGB565, the 320x240 screen at the top left
                                    texture 1: 128x64 RGBA8, TrashCan_CLSD | TrashCan_OPEN
    source/ui/pick_a_toon_atlas.h   square / name / delete geometry in screen pixels

    .venv/bin/python -I tools/pipeline/bake_pick_a_toon.py [--res ref/resources] [--png out.png]
"""
import argparse
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from panda3d.core import loadPrcFileData  # noqa: E402

loadPrcFileData('', 'window-type offscreen\nwin-size 1280 960\naudio-library-name null\n'
                    'textures-power-2 none\nframebuffer-alpha 1')

import numpy as np  # noqa: E402
from direct.showbase.ShowBase import ShowBase  # noqa: E402
from panda3d.core import NodePath, OrthographicLens, PNMImage, TransparencyAttrib  # noqa: E402

import ttm  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'romfs' / 'baked' / 'pick_a_toon.ttm'
HEADER = ROOT / 'source' / 'ui' / 'pick_a_toon_atlas.h'
SCREEN_W, SCREEN_H, PX = 320, 240, 120.0  # aspect2d units -> bottom-screen pixels
GUI = 'phase_3/models/gui/tt_m_gui_pat_mainGui'
TRASH = 'phase_3/models/gui/trashcan_gui'

# AvatarChooser.POSITIONS (x, z) and AvatarChoice's per-slot squares / NAME_* / DELETE_POSITIONS.
POSITIONS = [(-0.840167, 0.359333), (0.00933349, 0.306533), (0.862, 0.3293),
             (-0.863554, -0.445659), (0.00999999, -0.5181), (0.864907, -0.445659)]
SQUARES = ['Red', 'Green', 'Purple', 'Blue', 'Pink', 'Yellow']
PANEL_SCALE = 1.01
NAME_ROTATIONS = [7, -11, 1, -5, 3.5, -5]
NAME_POSITIONS = [(0, 0.26), (-0.03, 0.25), (0, 0.27), (-0.03, 0.25), (0.03, 0.26), (0, 0.26)]
DELETE_POSITIONS = [(0.187, -0.26), (0.31, -0.167), (0.231, -0.241), (0.314, -0.186), (0.243, -0.233),
                    (0.28, -0.207)]
DELETE_SCALE = 0.45


def to_px(x, z):
    return (x + SCREEN_W / PX / 2) * PX, (1 - z) * PX


def grab(base, w, h):
    base.graphicsEngine.renderFrame()
    base.graphicsEngine.renderFrame()
    shot = PNMImage()
    base.win.getScreenshot().store(shot)
    out = PNMImage(w, h, 4)
    out.gaussianFilterFrom(1.0, shot)
    return out


def pixels(img, w, h):
    px = np.empty((h, w, 4), dtype=np.uint8)
    for y in range(h):
        for x in range(w):
            c = img.getXelA(x, y)
            px[y, x] = [round(c[0] * 255), round(c[1] * 255), round(c[2] * 255), round(c[3] * 255)]
    return px


def main():
    ap = argparse.ArgumentParser()
    default = ROOT / 'ref' / 'resources'
    if not (default / 'phase_3').is_dir():
        default = ROOT / 'ref' / 'ref' / 'resources'
    ap.add_argument('--res', type=Path, default=default)
    ap.add_argument('--png', type=Path, help='also write the screen image as a PNG (for review)')
    args = ap.parse_args()

    base = ShowBase()
    base.setBackgroundColor(0, 0, 0, 0)
    lens = OrthographicLens()
    lens.setNearFar(-100, 100)
    base.cam.node().setLens(lens)
    gui = base.loader.loadModel(str(args.res / (GUI + '.bam')))

    # The screen: background, then the six squares over it (aspect2d is x right, z up).
    scene = NodePath('pick')
    scene.reparentTo(base.render)
    bg = gui.find('**/tt_t_gui_pat_background')
    if bg.isEmpty():
        raise SystemExit(f'{GUI} has no tt_t_gui_pat_background')
    bg.copyTo(scene).setTransform(bg.getTransform(gui))
    for i, colour in enumerate(SQUARES):
        sq = gui.find('**/tt_t_gui_pat_square' + colour)
        if sq.isEmpty():
            raise SystemExit(f'{GUI} has no square {colour}')
        c = sq.copyTo(scene)
        c.setPos(POSITIONS[i][0], -1 - i * 0.01, POSITIONS[i][1])
        c.setScale(PANEL_SCALE)
    scene.setTransparency(TransparencyAttrib.MAlpha)
    lens.setFilmSize(SCREEN_W / PX, SCREEN_H / PX)
    base.camera.setPos(0, -50, 0)
    base.camera.lookAt(0, 0, 0)
    screen = grab(base, SCREEN_W, SCREEN_H)
    scene.removeNode()
    if args.png:
        screen.write(str(args.png))
    tex = np.zeros((256, 512, 4), dtype=np.uint8)
    tex[:, :, 3] = 255
    tex[:SCREEN_H, :SCREEN_W] = pixels(screen, SCREEN_W, SCREEN_H)
    tex[:SCREEN_H, :SCREEN_W, 3] = 255
    background = ttm.Texture('pick_a_toon', tex, ttm.GPU_RGB565, ttm.GPU_CLAMP_TO_EDGE, ttm.GPU_CLAMP_TO_EDGE,
                             mipmaps=False)

    # The trashcan's closed / open faces, each in a 64 x 64 cell (aspect kept, centred).
    trash = base.loader.loadModel(str(args.res / (TRASH + '.bam')))
    sheet = np.zeros((64, 128, 4), dtype=np.uint8)
    trash_rows = []
    for k, name in enumerate(['TrashCan_CLSD', 'TrashCan_OPEN']):
        part = trash.find('**/' + name)
        if part.isEmpty():
            raise SystemExit(f'{TRASH} has no {name}')
        holder = NodePath('t')
        holder.reparentTo(base.render)
        part.copyTo(holder)
        holder.setTransparency(TransparencyAttrib.MAlpha)
        lo, hi = holder.getTightBounds()
        bw, bh = hi[0] - lo[0], hi[2] - lo[2]
        f = max(bw, bh) * 1.02
        lens.setFilmSize(f * SCREEN_W / SCREEN_H, f)
        base.camera.setPos((lo[0] + hi[0]) / 2, -50, (lo[2] + hi[2]) / 2)
        cell = grab(base, 85, 64)  # the 4:3 window -> 85 x 64, then the square centre is kept
        px = pixels(cell, 85, 64)
        sheet[:, k * 64:(k + 1) * 64] = px[:, 10:74]
        holder.removeNode()
        trash_rows.append((name, k * 64 + (64 - bw / f * 64) / 2, (64 - bh / f * 64) / 2, bw / f * 64, bh / f * 64,
                           bw, bh))
    trashcan = ttm.Texture('trashcan', sheet, ttm.GPU_RGBA8, ttm.GPU_CLAMP_TO_EDGE, ttm.GPU_CLAMP_TO_EDGE,
                           mipmaps=False)

    quad = np.array([[-1, 0, -1, 0, 0, 1, 1, 1, 1], [1, 0, -1, 1, 0, 1, 1, 1, 1],
                     [1, 0, 1, 1, 1, 1, 1, 1, 1], [-1, 0, 1, 0, 1, 1, 1, 1, 1]], dtype=float)
    batches = [ttm.Batch(i, ttm.BLEND_ALPHA, True, 0, 0, quad, np.array([0, 1, 2, 0, 2, 3])) for i in range(2)]
    OUT.parent.mkdir(parents=True, exist_ok=True)
    stats = ttm.write(OUT, [background, trashcan], batches)
    print(f'{OUT}: {stats["file_bytes"]} bytes')

    half = 0.395833 * PANEL_SCALE * PX
    lines = ['// GENERATED by tools/pipeline/bake_pick_a_toon.py. Do not edit.', '#pragma once', '',
             'namespace tt::ui::pat {', '',
             '// Bottom-screen pixels (320 x 240, top-left origin) of AvatarChooser.POSITIONS and the',
             '// AvatarChoice parts placed relative to each panel.',
             f'constexpr float kSquareHalf = {half:.2f}f; // a square\'s half side (0.3958 x 1.01)',
             'struct Slot {',
             '    float cx, cy;        // the square\'s centre',
             '    float nameX, nameY;  // NAME_POSITIONS: the name label\'s centre',
             '    float nameRot;       // NAME_ROTATIONS, degrees (positive turns counter-clockwise)',
             '    float delX, delY;    // DELETE_POSITIONS: the trashcan\'s centre',
             '};',
             'constexpr Slot kSlots[6] = {']
    for i in range(6):
        cx, cy = to_px(*POSITIONS[i])
        nx, ny = to_px(POSITIONS[i][0] + NAME_POSITIONS[i][0] * PANEL_SCALE,
                       POSITIONS[i][1] + NAME_POSITIONS[i][1] * PANEL_SCALE)
        dx, dy = to_px(POSITIONS[i][0] + DELETE_POSITIONS[i][0] * PANEL_SCALE,
                       POSITIONS[i][1] + DELETE_POSITIONS[i][1] * PANEL_SCALE)
        lines.append(f'    {{{cx:.2f}f, {cy:.2f}f, {nx:.2f}f, {ny:.2f}f, {NAME_ROTATIONS[i]:.1f}f, {dx:.2f}f, '
                     f'{dy:.2f}f}}, // {SQUARES[i]}')
    lines += ['};', '',
              'constexpr int kTexW = 512, kTexH = 256, kScreenW = 320, kScreenH = 240; // texture 0',
              'constexpr int kTrashAtlasW = 128, kTrashAtlasH = 64;                    // texture 1',
              'struct TrashRect {',
              '    float x, y, w, h;   // in the trashcan atlas (pixels, top-left origin)',
              '    float pxW, pxH;     // upstream on-screen size (scale 0.45 x 1.01), pixels',
              '};',
              'constexpr TrashRect kTrash[2] = { // closed, open']
    for name, x, y, w, h, bw, bh in trash_rows:
        s = DELETE_SCALE * PANEL_SCALE * PX
        lines.append(f'    {{{x:.2f}f, {y:.2f}f, {w:.2f}f, {h:.2f}f, {bw * s:.2f}f, {bh * s:.2f}f}}, // {name}')
    lines += ['};', '', '} // namespace tt::ui::pat', '']
    HEADER.write_text('\n'.join(lines))
    print(HEADER)


if __name__ == '__main__':
    main()
