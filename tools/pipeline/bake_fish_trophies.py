#!/usr/bin/env python3
"""Bake the Shtiker Book's seven fishing trophies (FishPage.FishingTrophy) into one atlas.

FishingTrophy.setLevel assembles each level from phase_3.5/models/gui/fishingTrophy (scale 0.1:
the column stretched taller per level, the top raised to match, bronze / silver / gold colour
scales on the top) or fishingTrophyBowl (scale 2: the grey bowl at 1.75 / 2 / 2.25 for levels 5-7,
its base widened 1.25), the base tinted (1, 1, 0.8). Each level is rendered flat on the same
orthographic frame, so the trophies keep their relative heights, into a 48 x 56 cell.

    romfs/baked/fish_trophies.ttm      one 512 x 64 RGBA4 texture, a placeholder quad
    source/ui/fish_trophy_atlas.h      the cell of each level and the frame it covers

    .venv/bin/python -I tools/pipeline/bake_fish_trophies.py --res <ref/resources>
"""
import argparse
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

CELL_W, CELL_H, SS = 48, 56, 4
# The FishingTrophy frame each cell shows (DirectFrame units): every level fits.
FRAME_X, FRAME_Z0, FRAME_Z1 = 0.45, -0.03, 1.02  # the gold bowl is 0.82 wide

from panda3d.core import loadPrcFileData  # noqa: E402

loadPrcFileData('', f'window-type offscreen\nwin-size {CELL_W * SS} {CELL_H * SS}\naudio-library-name null\n'
                    'textures-power-2 none\nframebuffer-alpha 1\nframebuffer-multisample 0\n')

import numpy as np  # noqa: E402
from direct.showbase.ShowBase import ShowBase  # noqa: E402
from panda3d.core import NodePath, OrthographicLens, PNMImage, TransparencyAttrib  # noqa: E402

import ttm  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'romfs' / 'baked' / 'fish_trophies.ttm'
HEADER = ROOT / 'source' / 'ui' / 'fish_trophy_atlas.h'
ATLAS_W, ATLAS_H = 512, 64
BRONZE, SILVER, GOLD = (0.9, 0.6, 0.33, 1), (0.9, 0.9, 1, 1), (1, 0.95, 0.1, 1)
# FishingTrophy.setLevel: (column z scale, top z, colour) for the trophy, (bowl scale, colour) for 5-7.
TROPHY = {0: (1.11878, -1, BRONZE), 1: (1.61878, -0.5, BRONZE), 2: (2.11878, 0, SILVER),
          3: (2.61878, 0.5, SILVER), 4: (3.11878, 1, GOLD)}
BOWL = {5: (1.75, BRONZE), 6: (2.0, SILVER), 7: (2.25, GOLD)}
LEVELS = 7  # FishGlobals.TrophyDict 0..6; level 7 (>= 7) is never awarded


def build(base, res, level):
    holder = NodePath('FishingTrophy')
    if level in TROPHY:
        zscale, top_z, colour = TROPHY[level]
        trophy = base.loader.loadModel(str(res / 'phase_3.5/models/gui/fishingTrophy.bam'))
        trophy.reparentTo(holder)
        trophy.setPos(0, 1, 0)
        trophy.setScale(0.1)
        trophy.find('**/trophyBase').setColorScale(1, 1, 0.8, 1)
        trophy.find('**/trophyColumn').setScale(1.3229, 1.26468, zscale)
        top = trophy.find('**/trophyTop')
        top.setPos(0, 0, top_z)
        top.setColorScale(*colour)
    else:
        scale, colour = BOWL[level]
        bowl = base.loader.loadModel(str(res / 'phase_3.5/models/gui/fishingTrophyBowl.bam'))
        bowl.reparentTo(holder)
        bowl.setPos(0, 1, 0)
        bowl.setScale(2.0)
        top = bowl.find('**/fishingTrophyGreyBowl')
        top.setScale(scale)
        top.setColorScale(*colour)
        bowl_base = bowl.find('**/fishingTrophyBase')
        bowl_base.setScale(1.25, 1, 1)
        bowl_base.setColorScale(1, 1, 0.8, 1)
    holder.setTransparency(TransparencyAttrib.MAlpha)
    holder.setTwoSided(True)
    return holder


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--res', type=Path, default=ROOT / 'ref' / 'resources')
    ap.add_argument('--preview', type=Path)
    args = ap.parse_args()

    base = ShowBase()
    base.setBackgroundColor(0, 0, 0, 0)
    lens = OrthographicLens()
    lens.setFilmSize(2 * FRAME_X, FRAME_Z1 - FRAME_Z0)
    lens.setNearFar(-100, 100)
    base.cam.node().setLens(lens)
    base.camera.setPos(0, -50, (FRAME_Z0 + FRAME_Z1) / 2)
    base.camera.lookAt(0, 0, (FRAME_Z0 + FRAME_Z1) / 2)

    sheet = PNMImage(ATLAS_W, ATLAS_H, 4)
    sheet.fill(0, 0, 0)
    sheet.alphaFill(0)
    cells = []
    for level in range(LEVELS):
        holder = build(base, args.res, level)
        holder.reparentTo(base.render)
        base.graphicsEngine.renderFrame()
        base.graphicsEngine.renderFrame()
        shot = PNMImage()
        base.win.getScreenshot().store(shot)
        cell = PNMImage(CELL_W, CELL_H, 4)
        cell.gaussianFilterFrom(1.0, shot)
        cx = level * CELL_W
        sheet.copySubImage(cell, cx, 0)
        cells.append(cx)
        holder.removeNode()

    px = np.empty((ATLAS_H, ATLAS_W, 4), dtype=np.uint8)
    for y in range(ATLAS_H):
        for x in range(ATLAS_W):
            c = sheet.getXelA(x, y)
            px[y, x] = [round(c[0] * 255), round(c[1] * 255), round(c[2] * 255), round(c[3] * 255)]
    a = px[..., 3:4].astype(np.float32) / 255.0  # straight alpha over the clear background
    px[..., :3] = np.round(np.where(a > 0, np.clip(px[..., :3] / np.maximum(a, 1e-6), 0, 255), 0)).astype(np.uint8)
    if args.preview:
        sheet.write(str(args.preview))
    texture = ttm.Texture('fish_trophies', px, ttm.GPU_RGBA4, ttm.GPU_CLAMP_TO_EDGE, ttm.GPU_CLAMP_TO_EDGE,
                          mipmaps=False)
    quad = np.array([[-1, 0, -1, 0, 0, 1, 1, 1, 1], [1, 0, -1, 1, 0, 1, 1, 1, 1],
                     [1, 0, 1, 1, 1, 1, 1, 1, 1], [-1, 0, 1, 0, 1, 1, 1, 1, 1]], dtype=float)
    batch = ttm.Batch(0, ttm.BLEND_ALPHA, True, 0, 0, quad, np.array([0, 1, 2, 0, 2, 3]))
    stats = ttm.write(OUT, [texture], [batch])
    print(f'{OUT}: {stats["file_bytes"]} bytes')

    lines = ['// GENERATED by tools/pipeline/bake_fish_trophies.py. Do not edit.', '#pragma once', '',
             'namespace tt::ui {', '',
             f'constexpr int kFishTrophyAtlasW = {ATLAS_W}, kFishTrophyAtlasH = {ATLAS_H};',
             f'constexpr float kFishTrophyCellW = {CELL_W}.0f, kFishTrophyCellH = {CELL_H}.0f;',
             f'// Each cell shows FishingTrophy x -{FRAME_X}..{FRAME_X}, z {FRAME_Z0}..{FRAME_Z1}; z 0 is the base.',
             f'constexpr float kFishTrophyBaseFrac = {(FRAME_Z1 - 0.0) / (FRAME_Z1 - FRAME_Z0):.4f}f; // base line, from the cell top',
             f'constexpr int kFishTrophyLevels = {LEVELS};',
             'constexpr float kFishTrophyCellX[kFishTrophyLevels] = {' + ', '.join(f'{x}.0f' for x in cells) + '};',
             '', '} // namespace tt::ui', '']
    HEADER.write_text('\n'.join(lines))
    print(HEADER)


if __name__ == '__main__':
    main()
