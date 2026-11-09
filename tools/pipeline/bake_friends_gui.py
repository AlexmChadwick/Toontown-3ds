#!/usr/bin/env python3
"""Bake the stock FriendsListPanel art into a small bottom-screen atlas.

FriendsListPanel.load uses FriendsBox_Open from friendslist_gui, the horizontal and scroll
arrows from that model, and CloseBtn_UP / Frnds_Btn_UP from avatar_panel_gui. The 3DS panel
reuses those exact pieces around its compact list instead of approximating the stock parchment.

    romfs/baked/friends_gui.ttm       one 256x256 RGBA8 texture
    source/ui/friends_gui_atlas.h     atlas rectangles

The parchment gradient is finer than a 4-bit step, so this stays RGBA8. The panel
alone is 118 x 220, which already needs a 256 x 256 sheet. Scroll, Arrow and Close
are unused at runtime and do not change that size.

    .venv/bin/python -I tools/pipeline/bake_friends_gui.py --res ref/resources [--png atlas.png]
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
OUT = ROOT / 'romfs' / 'baked' / 'friends_gui.ttm'
HEADER = ROOT / 'source' / 'ui' / 'friends_gui_atlas.h'
ATLAS_W = ATLAS_H = 256
FRIENDS = 'phase_3.5/models/gui/friendslist_gui'
AVATAR = 'phase_3.5/models/gui/avatar_panel_gui'
PARTS = [
    Part('Panel', FRIENDS, ['FriendsBox_Open'], (0, 0, 192, 224)),
    Part('Scroll', FRIENDS, ['FndsLst_ScrollUp'], (192, 0, 32, 32)),
    Part('Arrow', FRIENDS, ['Horiz_Arrow_UP'], (224, 0, 32, 32)),
    Part('Close', AVATAR, ['CloseBtn_UP'], (192, 32, 32, 32)),
    Part('NewFriend', AVATAR, ['Frnds_Btn_UP'], (192, 64, 64, 40)),
]


def main():
    ap = argparse.ArgumentParser()
    default = ROOT / 'ref' / 'resources'
    if not (default / 'phase_3.5').is_dir():
        default = ROOT / 'ref' / 'ref' / 'resources'
    ap.add_argument('--res', type=Path, default=default)
    ap.add_argument('--png', type=Path, help='also write the atlas as a PNG')
    args = ap.parse_args()
    rows = ui_atlas.bake(args.res, PARTS, ATLAS_W, ATLAS_H, OUT, 'friends_gui', args.png)
    ui_atlas.write_header(HEADER, 'tools/pipeline/bake_friends_gui.py', 'tt::ui', 'kFriendsGuiAtlasW',
                          'kFriendsGuiAtlasH', ATLAS_W, ATLAS_H, 'FriendsArt', 'FriendsArtRect', rows,
                          'the part in the atlas (pixels, top-left origin)')


if __name__ == '__main__':
    main()
