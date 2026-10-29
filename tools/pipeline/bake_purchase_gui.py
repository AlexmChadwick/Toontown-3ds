#!/usr/bin/env python3
"""Bake the gag purchase screens' art (phase_4/models/gui/gag_shop_purchase_gui) into one atlas.

PurchaseBase (the trolley's gag purchase) and ClerkPurchase (Goofy's Gag Shop) title the screen
with the wooden Goofys_Sign plank ("Goofy's Gag Shop" in the sign font) and show our jellybeans
on the Jar (pointDisplay: the count in yellow with a black shadow over the jar). InventoryPage
shows the same jar beside the book's gag grid. Each part is rendered offscreen into its cell,
aspect preserved and centred (ui_atlas.py).

    romfs/baked/purchase_gui.ttm       one 256x128 RGBA8 texture, a placeholder quad
    source/ui/purchase_gui_atlas.h     the part rectangles (pixels, top-left origin)

    .venv/bin/python -I tools/pipeline/bake_purchase_gui.py [--res ref/resources] [--png atlas.png]
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
OUT = ROOT / 'romfs' / 'baked' / 'purchase_gui.ttm'
HEADER = ROOT / 'source' / 'ui' / 'purchase_gui_atlas.h'
ATLAS_W, ATLAS_H = 256, 128
MODEL = 'phase_4/models/gui/gag_shop_purchase_gui'
PARTS = [
    Part('Sign', MODEL, ['Goofys_Sign'], (0, 0, 256, 32)),
    Part('Jar', MODEL, ['Jar'], (0, 32, 96, 96)),
]


def main():
    ap = argparse.ArgumentParser()
    default = ROOT / 'ref' / 'resources'
    if not (default / 'phase_4').is_dir():
        default = ROOT / 'ref' / 'ref' / 'resources'
    ap.add_argument('--res', type=Path, default=default)
    ap.add_argument('--png', type=Path, help='also write the atlas as a PNG (for review)')
    args = ap.parse_args()
    rows = ui_atlas.bake(args.res, PARTS, ATLAS_W, ATLAS_H, OUT, 'purchase_gui', args.png)
    ui_atlas.write_header(HEADER, 'tools/pipeline/bake_purchase_gui.py', 'tt::ui', 'kPurchaseGuiAtlasW',
                          'kPurchaseGuiAtlasH', ATLAS_W, ATLAS_H, 'PurchaseArt', 'PurchaseArtRect', rows,
                          'the part in the atlas (pixels, top-left origin)')


if __name__ == '__main__':
    main()
