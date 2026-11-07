#!/usr/bin/env python3
"""Bake ToontownLoadingScreen's art for the front end's title / connecting / heading-to screens.

ToontownLoadingScreen draws phase_3/models/gui/progress-background (`bg`: the radial blue card
behind everything, `logo`: the Toontown Online logo, already baked by bake_title_logo.py) and
toon_council's `scroll` at scale 0.4, which holds the TOON TIP text.

    romfs/baked/loading_bg.ttm      `bg` as a real 2.67 x 2 quad (XZ plane, facing -Y) with its
                                    64x64 texture in RGB565 (ETC1 blocks band a smooth gradient);
                                    the front end draws it far behind the Toon in the 3D pass
    romfs/baked/loading_gui.ttm     the scroll, one 256x128 RGBA8 cell (ui_atlas.py)
    source/ui/loading_atlas.h       the scroll's rectangle in that cell

    .venv/bin/python -I tools/pipeline/bake_loading_screen.py [--res ref/resources]
"""
import argparse
import builtins
import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from panda3d.core import Filename, NodePath, loadPrcFileData  # noqa: E402

loadPrcFileData('', 'window-type offscreen\nwin-size 512 512\naudio-library-name null\n'
                    'textures-power-2 none\nframebuffer-alpha 1\nnotify-level warning')

import extract  # noqa: E402
import ttm  # noqa: E402
import ui_atlas  # noqa: E402
from ui_atlas import Part  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
BG_OUT = ROOT / 'romfs' / 'baked' / 'loading_bg.ttm'
GUI_OUT = ROOT / 'romfs' / 'baked' / 'loading_gui.ttm'
HEADER = ROOT / 'source' / 'ui' / 'loading_atlas.h'
PARTS = [Part('Scroll', 'phase_3/models/gui/toon_council', ['scroll'], (0, 0, 256, 128))]


def main():
    ap = argparse.ArgumentParser()
    default = ROOT / 'ref' / 'resources'
    if not (default / 'phase_3').is_dir():
        default = ROOT / 'ref' / 'ref' / 'resources'
    ap.add_argument('--res', type=Path, default=default)
    ap.add_argument('--png', type=Path, help='also write the scroll atlas as a PNG (for review)')
    args = ap.parse_args()
    rows = ui_atlas.bake(args.res, PARTS, 256, 128, GUI_OUT, 'loading_gui', args.png)
    ui_atlas.write_header(HEADER, 'tools/pipeline/bake_loading_screen.py', 'tt::ui', 'kLoadingAtlasW',
                          'kLoadingAtlasH', 256, 128, 'LoadingArt', 'LoadingArtRect', rows,
                          'the part in the atlas (pixels, top-left origin)')

    base = builtins.base  # the ShowBase ui_atlas.bake made
    path = (args.res / 'phase_3' / 'models' / 'gui' / 'progress-background.bam').resolve()
    gui = base.loader.loadModel(Filename.fromOsSpecific(str(path)))
    bg = gui.find('**/bg')
    if bg.isEmpty():
        sys.exit(f'{path} is missing bg')
    scene = NodePath('loading_bg')
    copy = bg.copyTo(scene)
    copy.setTransform(bg.getTransform(gui))
    ex = extract.Extractor(max_tex=64, etc1=False)
    ex.add(scene)
    stats = ttm.write(str(BG_OUT), ex.textures, ex.batches())
    print(f'{BG_OUT}: {json.dumps(stats)}')


if __name__ == '__main__':
    main()
