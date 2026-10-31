#!/usr/bin/env python3
"""Bake Tutorial Tom's lesson cards (phase_3.5/models/gui/tutorial_gui) into one atlas.

quest_assign_101 LOADs the five cards (suits, toon_buildings, suit_buildings, squirt1, squirt2)
and parents each to the camera while Tom talks about it. The 3DS shows the card under his
speech on the bottom screen (world/tutorial.* tracks which card the script has up). Each card is
rendered offscreen into its cell, aspect preserved and centred (ui_atlas.py).

    romfs/baked/tutorial_gui.ttm       one 512x256 RGBA8 texture, a placeholder quad
    source/ui/tutorial_gui_atlas.h     the card rectangles (pixels, top-left origin)

    .venv/bin/python -I tools/pipeline/bake_tutorial_gui.py [--res ref/resources] [--png atlas.png]
"""
import argparse
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from panda3d.core import loadPrcFileData  # noqa: E402

loadPrcFileData('', 'window-type offscreen\nwin-size 512 512\naudio-library-name null\n'
                    'textures-power-2 none\nframebuffer-alpha 1')

import ui_atlas  # noqa: E402
from ui_atlas import Part  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'romfs' / 'baked' / 'tutorial_gui.ttm'
HEADER = ROOT / 'source' / 'ui' / 'tutorial_gui_atlas.h'
ATLAS_W, ATLAS_H = 512, 256
MODEL = 'phase_3.5/models/gui/tutorial_gui'
# Part names match quest::ScriptVar (the script's LOAD variable for each card).
PARTS = [
    Part('Cogs', MODEL, ['suits'], (0, 0, 160, 128)),
    Part('ToonBuilding', MODEL, ['toon_buildings'], (160, 0, 160, 128)),
    Part('CogBuilding', MODEL, ['suit_buildings'], (320, 0, 160, 128)),
    Part('Squirt1', MODEL, ['squirt1'], (0, 128, 160, 128)),
    Part('Squirt2', MODEL, ['squirt2'], (160, 128, 160, 128)),
]


def main():
    ap = argparse.ArgumentParser()
    default = ROOT / 'ref' / 'resources'
    if not (default / 'phase_3.5').is_dir():
        default = ROOT / 'ref' / 'ref' / 'resources'
    ap.add_argument('--res', type=Path, default=default)
    ap.add_argument('--png', type=Path, help='also write the atlas as a PNG (for review)')
    args = ap.parse_args()
    rows = ui_atlas.bake(args.res, PARTS, ATLAS_W, ATLAS_H, OUT, 'tutorial_gui', args.png)
    ui_atlas.write_header(HEADER, 'tools/pipeline/bake_tutorial_gui.py', 'tt::ui', 'kTutorialGuiAtlasW',
                          'kTutorialGuiAtlasH', ATLAS_W, ATLAS_H, 'TutorialCard', 'TutorialCardRect', rows,
                          'the card in the atlas (pixels, top-left origin)')


if __name__ == '__main__':
    main()
