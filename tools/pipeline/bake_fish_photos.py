#!/usr/bin/env python3
"""Bake FishPhoto's picture of every fish genus into one atlas.

Upstream shows a caught fish (DistributedFishingSpot's FishPanel) and the Shtiker Book's fish
album through FishPhoto: the genus actor from FishGlobals.FishFileDict in its own display
region, under a 40 x 40 degree lens at the origin with no lights, two-sided, centred on its
tight bounds (or the dict's position), scaled, turned by H, pitched by P and pushed 2 forward.
This replays that frame offline at the clip frame where the fish is largest inside the region (the photo plays 'intro', the
same file, then loops it) and keeps the fish over a clear background, so the runtime fills the
region's colour (FishPanel's swim colour, the album's Fish_BG).

The cell has the catch panel region's aspect: setSwimBounds(-0.3, 0.3, -0.235, 0.25) in aspect2d
on a 4:3 window is 180 x 145.5 pixels, and the square lens is stretched over it as in the game.

The other catches' pictures (the jellybean jar, the old boot, the quest package) share the atlas.

    romfs/baked/fish_photos.ttm       one 512 x 512 RGBA4 texture, a placeholder quad
    source/ui/fish_photo_atlas.h      the cell of each genus, the item pictures and their boxes

    .venv/bin/python -I tools/pipeline/bake_fish_photos.py --res <resources> --src <open-toontown>
"""
import argparse
import ast
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

CELL_W, CELL_H, SS = 124, 100, 4
FRAME_SAMPLES = 24

from panda3d.core import loadPrcFileData  # noqa: E402

loadPrcFileData('', f'window-type offscreen\nwin-size {CELL_W * SS} {CELL_H * SS}\naudio-library-name null\n'
                    'textures-power-2 none\nframebuffer-alpha 1\nframebuffer-multisample 0\n')

import numpy as np  # noqa: E402
from direct.actor.Actor import Actor  # noqa: E402
from direct.showbase.ShowBase import ShowBase  # noqa: E402
from panda3d.core import NodePath, OrthographicLens, PerspectiveLens, PNMImage, TransparencyAttrib  # noqa: E402

import ttm  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'romfs' / 'baked' / 'fish_photos.ttm'
HEADER = ROOT / 'source' / 'ui' / 'fish_photo_atlas.h'
ATLAS = 512
COLS = ATLAS // CELL_W
# The other catches' pictures (DistributedFishingSpot.__makeGui: itemJellybean jar_gui at 0.5,
# itemBoot fishing_boot at 0.2, itemPackage stickerbook_gui's package at 0.25), in 84 x 68 cells
# after the last fish row (the window's aspect).
ITEM_W, ITEM_H = 84, 68
ITEMS = [('Jar', 'phase_3.5/models/gui/jar_gui', None, 0.5),
         ('Boot', 'phase_4/models/gui/fishing_boot', None, 0.2),
         ('Package', 'phase_3.5/models/gui/stickerbook_gui', 'package', 0.25)]


def fish_file_dict(src):
    """FishGlobals.FishFileDict, read as a literal (importing FishGlobals pulls in the game)."""
    tree = ast.parse((src / 'toontown/fishing/FishGlobals.py').read_text())
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], 'id', '') == 'FishFileDict':
            return ast.literal_eval(node.value)
    raise SystemExit('FishGlobals has no FishFileDict')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--res', type=Path, default=ROOT / 'ref' / 'resources')
    ap.add_argument('--src', type=Path, default=ROOT / 'ref' / 'open-toontown')
    ap.add_argument('--preview', type=Path, help='also write the atlas as a PNG')
    args = ap.parse_args()

    files = fish_file_dict(args.src)
    genera = sorted(g for g in files if g >= 0)
    if len(genera) > COLS * (ATLAS // CELL_H):
        raise SystemExit('too many genera for the atlas')

    base = ShowBase()
    base.setBackgroundColor(0, 0, 0, 0)
    lens = PerspectiveLens()
    lens.setFov(40, 40)
    lens.setNear(0.1)
    lens.setFar(100.0)
    base.cam.node().setLens(lens)
    base.camera.setPosHpr(0, 0, 0, 0, 0, 0)

    sheet = PNMImage(ATLAS, ATLAS, 4)
    sheet.fill(0, 0, 0)
    sheet.alphaFill(0)
    cells = []
    for i, genus in enumerate(genera):
        phase, model, intro, swim, _, pos, scale, h, p = files[genus]
        prefix = args.res / f'phase_{phase}/models/char'
        actor = Actor(str(prefix / f'{model}.bam'), {'intro': str(prefix / f'{intro}.bam')})
        actor.setTwoSided(1)
        actor.setDepthTest(1)
        actor.setDepthWrite(1)
        frame = NodePath('fishSwimRender')
        frame.reparentTo(base.render)
        pitch = frame.attachNewNode('pitch')
        rotate = pitch.attachNewNode('rotate')
        scale_np = rotate.attachNewNode('scale')
        actor.reparentTo(scale_np)
        lo, hi = actor.getTightBounds()
        centre = (lo + hi) / 2.0
        actor.setPos(-centre[0], -centre[1], -centre[2])
        if pos:
            actor.setPos(*pos)
        scale_np.setScale(scale)
        rotate.setH(h)
        pitch.setP(p)
        pitch.setY(2)
        # The clip swims the fish around the region (the mackerel comes toward the lens from a
        # dot): keep a frame where it is nearly its largest without leaving the region.
        tries = []
        n = actor.getNumFrames('intro')
        for f in range(0, n, max(1, n // FRAME_SAMPLES)):
            actor.pose('intro', f)
            base.graphicsEngine.renderFrame()
            base.graphicsEngine.renderFrame()
            shot = PNMImage()
            base.win.getScreenshot().store(shot)
            cell = PNMImage(CELL_W, CELL_H, 4)
            cell.gaussianFilterFrom(1.0, shot)
            alpha = np.array([[cell.getAlpha(x, y) for x in range(CELL_W)] for y in range(CELL_H)])
            edge = alpha[0].sum() + alpha[-1].sum() + alpha[:, 0].sum() + alpha[:, -1].sum()
            tries.append((f, alpha.sum(), edge < 0.5, cell))
        inside = [t for t in tries if t[2]] or tries
        top = max(t[1] for t in inside)
        # The earliest frame near full size: the fish arriving, facing the lens.
        frame_no, cover, _, cell = next(t for t in inside if t[1] >= 0.7 * top)
        best = ((True, cover),)
        print(f'{model}: frame {frame_no} of {n}, coverage {best[0][1] / (CELL_W * CELL_H):.2f}')
        cx, cy = (i % COLS) * CELL_W, (i // COLS) * CELL_H
        sheet.copySubImage(cell, cx, cy)
        cells.append((genus, model, cx, cy))
        frame.removeNode()
        actor.cleanup()

    # Items: flat gui art under an orthographic lens, framed with a 2% margin.
    ortho = OrthographicLens()
    ortho.setNearFar(-100, 100)
    base.cam.node().setLens(ortho)
    item_x0 = (len(genera) % COLS) * CELL_W
    item_y0 = (len(genera) // COLS) * CELL_H
    if len(genera) % COLS == 0 or item_x0 + len(ITEMS) * ITEM_W > ATLAS:
        raise SystemExit('the item pictures do not fit after the fish')
    items = []
    for i, (name, path, node, scale) in enumerate(ITEMS):
        model = base.loader.loadModel(str(args.res / (path + '.bam')))
        if node:
            model = model.find('**/' + node)
            if model.isEmpty():
                raise SystemExit(f'{path} has no {node}')
        holder = NodePath('item')
        holder.reparentTo(base.render)
        art = model.copyTo(holder)
        art.setPos(0, 0, 0)
        art.setScale(scale)
        holder.setTransparency(TransparencyAttrib.MAlpha)
        lo, hi = holder.getTightBounds()
        bw, bh = hi[0] - lo[0], hi[2] - lo[2]
        aspect = ITEM_W / ITEM_H
        fw = max(bw, bh * aspect) * 1.02
        fh = fw / aspect
        ortho.setFilmSize(fw, fh)
        mx, mz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
        base.camera.setPos(mx, -50, mz)
        base.camera.lookAt(mx, 0, mz)
        base.graphicsEngine.renderFrame()
        base.graphicsEngine.renderFrame()
        shot = PNMImage()
        base.win.getScreenshot().store(shot)
        cell = PNMImage(ITEM_W, ITEM_H, 4)
        cell.gaussianFilterFrom(1.0, shot)
        cx, cy = item_x0 + i * ITEM_W, item_y0
        sheet.copySubImage(cell, cx, cy)
        holder.removeNode()
        pw, ph = bw / fw * ITEM_W, bh / fh * ITEM_H
        items.append((name, cx + (ITEM_W - pw) / 2, cy + (ITEM_H - ph) / 2, pw, ph, lo[0], lo[2], hi[0], hi[2]))

    px = np.empty((ATLAS, ATLAS, 4), dtype=np.uint8)
    for y in range(ATLAS):
        for x in range(ATLAS):
            c = sheet.getXelA(x, y)
            px[y, x] = [round(c[0] * 255), round(c[1] * 255), round(c[2] * 255), round(c[3] * 255)]
    # The render is straight alpha over a clear background: colour is black where alpha fades,
    # so un-premultiply edge texels (the runtime blends straight alpha).
    a = px[..., 3:4].astype(np.float32) / 255.0
    rgb = np.where(a > 0, np.clip(px[..., :3] / np.maximum(a, 1e-6), 0, 255), 0)
    px[..., :3] = np.round(rgb).astype(np.uint8)
    if args.preview:
        prev = PNMImage(ATLAS, ATLAS, 4)
        for y in range(ATLAS):
            for x in range(ATLAS):
                r, g, b, al = (int(v) for v in px[y, x])
                prev.setXelA(x, y, r / 255, g / 255, b / 255, al / 255)
        prev.write(str(args.preview))

    texture = ttm.Texture('fish_photos', px, ttm.GPU_RGBA4, ttm.GPU_CLAMP_TO_EDGE, ttm.GPU_CLAMP_TO_EDGE,
                          mipmaps=False)
    quad = np.array([[-1, 0, -1, 0, 0, 1, 1, 1, 1], [1, 0, -1, 1, 0, 1, 1, 1, 1],
                     [1, 0, 1, 1, 1, 1, 1, 1, 1], [-1, 0, 1, 0, 1, 1, 1, 1, 1]], dtype=float)
    batch = ttm.Batch(0, ttm.BLEND_ALPHA, True, 0, 0, quad, np.array([0, 1, 2, 0, 2, 3]))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    stats = ttm.write(OUT, [texture], [batch])
    print(f'{OUT}: {stats["file_bytes"]} bytes')

    lines = ['// GENERATED by tools/pipeline/bake_fish_photos.py. Do not edit.', '#pragma once', '',
             '#include <cstdint>', '',
             'namespace tt::ui {', '',
             f'constexpr int kFishPhotoAtlas = {ATLAS};',
             f'constexpr float kFishPhotoW = {CELL_W}.0f, kFishPhotoH = {CELL_H}.0f; // FishPanel region aspect',
             'struct FishPhotoCell {',
             '    uint8_t genus;',
             '    uint16_t x, y; // top-left of the cell in the atlas (pixels)',
             '};',
             f'constexpr int kFishPhotoCount = {len(cells)};',
             'constexpr FishPhotoCell kFishPhotoCells[kFishPhotoCount] = {']
    for genus, model, cx, cy in cells:
        lines.append(f'    {{{genus}, {cx}, {cy}}}, // {model}')
    lines += ['};', '',
              '// The other catches, in itemFrame space (aspect2d units, +Z up, the DirectFrame scale applied).',
              'enum FishItemArt : int { ' + ', '.join(f'kFishItem{r[0]}' for r in items) + ', kFishItemCount };',
              'struct FishItemRect {',
              '    float x, y, w, h;             // the art in the atlas (pixels, top-left origin)',
              '    float minX, minZ, maxX, maxZ; // its box in itemFrame space',
              '};',
              'constexpr FishItemRect kFishItemRects[kFishItemCount] = {']
    for name, ix, iy, w, h, x0, z0, x1, z1 in items:
        lines.append(f'    {{{ix:.2f}f, {iy:.2f}f, {w:.2f}f, {h:.2f}f, {x0:.4f}f, {z0:.4f}f, {x1:.4f}f, {z1:.4f}f}}, // {name}')
    lines += ['};', '', '} // namespace tt::ui', '']
    HEADER.write_text('\n'.join(lines))
    print(HEADER)


if __name__ == '__main__':
    main()
