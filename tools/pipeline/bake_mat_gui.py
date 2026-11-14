#!/usr/bin/env python3
"""Bake Make-A-Toon's buttons (phase_3/models/gui/tt_m_gui_mat_mainGui) into one atlas.

MakeAToon's round glossy buttons (guiNextButton / guiLastButton: tt_t_gui_mat_nextUp, the back
button is the same image mirrored; guiOkButton: okUp), GenderShop's boy / girl buttons and the
BodyShop / ClothesGUI shuffle frames (tt_t_gui_mat_shuffleFrame) with their gold shuffle arrows
(shuffleArrowUp / Down / Disabled), NameShop's shuffle bar (shuffleUp) and the toon rotate arrow
(arrowRotateUp). Rendered offscreen
with ui_atlas.py, aspect preserved and centred in each cell.

    romfs/baked/mat_gui.ttm       one 256x256 RGBA8 texture, a placeholder quad
    source/ui/mat_gui_atlas.h     the part rectangles (pixels, top-left origin)

    .venv/bin/python -I tools/pipeline/bake_mat_gui.py [--res ref/resources] [--png atlas.png]
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
OUT = ROOT / 'romfs' / 'baked' / 'mat_gui.ttm'
HEADER = ROOT / 'source' / 'ui' / 'mat_gui_atlas.h'
ATLAS_W, ATLAS_H = 256, 256
M = 'phase_3/models/gui/tt_m_gui_mat_mainGui'
PARTS = [
    Part('NextUp', M, ['tt_t_gui_mat_nextUp'], (0, 0, 64, 64)),
    Part('NextDown', M, ['tt_t_gui_mat_nextDown'], (64, 0, 64, 64)),
    Part('OkUp', M, ['tt_t_gui_mat_okUp'], (128, 0, 64, 64)),
    Part('OkDown', M, ['tt_t_gui_mat_okDown'], (192, 0, 64, 64)),
    Part('BoyUp', M, ['tt_t_gui_mat_boyUp'], (0, 64, 64, 64)),
    Part('GirlUp', M, ['tt_t_gui_mat_girlUp'], (64, 64, 64, 64)),
    Part('ArrowUp', M, ['tt_t_gui_mat_shuffleArrowUp'], (128, 64, 32, 64)),
    Part('ArrowDown', M, ['tt_t_gui_mat_shuffleArrowDown'], (160, 64, 32, 64)),
    Part('ArrowDisabled', M, ['tt_t_gui_mat_shuffleArrowDisabled'], (192, 64, 32, 64)),
    Part('ShuffleFrame', M, ['tt_t_gui_mat_shuffleFrame'], (0, 128, 128, 64)),
    Part('ShuffleUp', M, ['tt_t_gui_mat_shuffleUp'], (128, 128, 128, 64)),
    Part('BoyDown', M, ['tt_t_gui_mat_boyDown'], (0, 192, 64, 64)),
    Part('GirlDown', M, ['tt_t_gui_mat_girlDown'], (64, 192, 64, 64)),
    Part('ShuffleDown', M, ['tt_t_gui_mat_shuffleDown'], (128, 192, 128, 64)),
    # MakeAToon's rotateLeftButton / rotateRightButton (the left one is this image mirrored).
    Part('RotateUp', M, ['tt_t_gui_mat_arrowRotateUp'], (224, 64, 32, 64)),
]


def main():
    ap = argparse.ArgumentParser()
    default = ROOT / 'ref' / 'resources'
    if not (default / 'phase_3').is_dir():
        default = ROOT / 'ref' / 'ref' / 'resources'
    ap.add_argument('--res', type=Path, default=default)
    ap.add_argument('--png', type=Path, help='also write the atlas as a PNG (for review)')
    args = ap.parse_args()
    rows = ui_atlas.bake(args.res, PARTS, ATLAS_W, ATLAS_H, OUT, 'mat_gui', args.png)
    ui_atlas.write_header(HEADER, 'tools/pipeline/bake_mat_gui.py', 'tt::ui', 'kMatGuiAtlasW', 'kMatGuiAtlasH',
                          ATLAS_W, ATLAS_H, 'MatArt', 'MatArtRect', rows,
                          'the part in the atlas (pixels, top-left origin)')


if __name__ == '__main__':
    main()
