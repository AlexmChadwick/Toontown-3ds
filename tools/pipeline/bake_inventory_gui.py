#!/usr/bin/env python3
"""Bake InventoryNew's gag grid art (phase_3.5/models/gui/inventory_gui) into one atlas.

The battle gag picker, the Shtiker Book's Gags page and the gag purchase screens are all
InventoryNew: a track row (InventoryRow, image_color = the track colour) holding a gag button per
level (InventoryButtonUp / Down / Rollover / Flat, image_color = the button state's colour, e.g.
PressableImageColor (0, 0.6, 1)). The art is grey, so it is stored LA8 and tinted at draw time.

    romfs/baked/inventory_gui.ttm      one 512x128 LA8 texture, a placeholder quad
    source/ui/inventory_gui_atlas.h    the part rectangles (pixels, top-left origin)

    .venv/bin/python -I tools/pipeline/bake_inventory_gui.py [--res ref/resources] [--png atlas.png]
"""
import argparse
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from panda3d.core import loadPrcFileData  # noqa: E402

loadPrcFileData('', 'window-type offscreen\nwin-size 1024 1024\naudio-library-name null\n'
                    'textures-power-2 none\nframebuffer-alpha 1')

import ttm  # noqa: E402
import ui_atlas  # noqa: E402
from ui_atlas import Part  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'romfs' / 'baked' / 'inventory_gui.ttm'
HEADER = ROOT / 'source' / 'ui' / 'inventory_gui_atlas.h'
ATLAS_W, ATLAS_H = 512, 128
MODEL = 'phase_3.5/models/gui/inventory_gui'
# InventoryRow is 1.667 x 0.114 (drawn x1.1 tall); the buttons 0.18 x 0.114. The row is drawn
# about 316 px wide on the bottom screen, so it keeps 512 px; a button about 35 x 18 px.
PARTS = [
    Part('Row', MODEL, ['InventoryRow'], (0, 0, 512, 40)),
    Part('Up', MODEL, ['InventoryButtonUp'], (0, 48, 64, 40)),
    Part('Down', MODEL, ['InventoryButtonDown'], (64, 48, 64, 40)),
    Part('Rollover', MODEL, ['InventoryButtonRollover'], (128, 48, 64, 40)),
    Part('Flat', MODEL, ['InventoryButtonFlat'], (192, 48, 64, 40)),
]


def main():
    ap = argparse.ArgumentParser()
    default = ROOT / 'ref' / 'resources'
    if not (default / 'phase_3.5').is_dir():
        default = ROOT / 'ref' / 'ref' / 'resources'
    ap.add_argument('--res', type=Path, default=default)
    ap.add_argument('--png', type=Path, help='also write the atlas as a PNG (for review)')
    args = ap.parse_args()
    rows = ui_atlas.bake(args.res, PARTS, ATLAS_W, ATLAS_H, OUT, 'inventory_gui', args.png, fmt=ttm.GPU_LA8)
    ui_atlas.write_header(HEADER, 'tools/pipeline/bake_inventory_gui.py', 'tt::ui', 'kInventoryGuiAtlasW',
                          'kInventoryGuiAtlasH', ATLAS_W, ATLAS_H, 'InventoryArt', 'InventoryArtRect', rows,
                          'the part in the atlas (pixels, top-left origin)')


if __name__ == '__main__':
    main()
