#!/usr/bin/env python3
"""Bake the kart race HUD art (RaceGUI and DistributedVehicle's speedometer) into one atlas.

RaceGUI.initRaceMode shows the gag panel (phase_6/models/karting/gag_panel) with the held gag on
it (toon_interior_textures couch, the inventory banana peel / anvil / cream pie cards scaled 7.5,
racing_textures boost_arrow), the map's race_mapspot, and one RaceHeadFrame per racer (phase_4
racing_panel: bg, plus fg tinted with the kart body colour) along the race progress line. DistributedVehicle's speedometer
stacks five unit cards of phase_6/models/karting/speedometer: gauge, spin (the needle, turned by
setR), face, tics and ring; face / tics / ring are colour-scaled from the kart colours at run time,
so every part is baked untinted. All of these are unit cards centred on their origin, so each
cell holds the whole card and the parts of one stack line up (ui_atlas.py).

    romfs/baked/race_gui.ttm       one 512x256 RGBA8 texture, a placeholder quad
    source/ui/race_gui_atlas.h     the part rectangles (pixels, top-left origin)

    .venv/bin/python -I tools/pipeline/bake_race_gui.py [--res ref/resources] [--png atlas.png]
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
OUT = ROOT / 'romfs' / 'baked' / 'race_gui.ttm'
HEADER = ROOT / 'source' / 'ui' / 'race_gui_atlas.h'
ATLAS_W, ATLAS_H = 512, 256
SPEEDO = 'phase_6/models/karting/speedometer'
ICONS = 'phase_3.5/models/gui/inventory_icons'
PARTS = [
    # Speedometer stack, drawn in this order (DistributedVehicle.__enableControlInterface).
    Part('SpeedGauge', SPEEDO, ['speedometer_gauge'], (0, 0, 128, 128)),
    Part('SpeedSpin', SPEEDO, ['speedometer_spin'], (128, 0, 128, 128)),
    Part('SpeedFace', SPEEDO, ['speedometer_face'], (256, 0, 128, 128)),
    Part('SpeedTics', SPEEDO, ['speedometer_tics'], (384, 0, 128, 128)),
    Part('SpeedRing', SPEEDO, ['speedometer_ring'], (0, 128, 128, 128)),
    Part('GagPanel', 'phase_6/models/karting/gag_panel', ['gag_panel'], (128, 128, 64, 64)),
    # RaceGUI.gagTextures, in gag index order (0, the couch, is never shown).
    Part('GagCouch', 'phase_3.5/models/modules/toon_interior_textures', ['couch'], (192, 128, 64, 64)),
    Part('GagBanana', ICONS, ['inventory_bannana_peel'], (256, 128, 64, 64)),
    Part('GagTurbo', 'phase_6/models/karting/racing_textures', ['boost_arrow'], (320, 128, 64, 64)),
    Part('GagAnvil', ICONS, ['inventory_anvil'], (384, 128, 64, 64)),
    Part('GagPie', ICONS, ['inventory_creampie'], (448, 128, 64, 64)),
    Part('PanelBg', 'phase_4/models/karting/racing_panel', ['racing_panel_bg'], (128, 192, 64, 64)),
    Part('PanelFg', 'phase_4/models/karting/racing_panel', ['racing_panel_fg'], (192, 192, 64, 64)),
    # The map's racer spot, tinted with the kart body colour (RaceGUI.racerEntered).
    Part('MapSpot', 'phase_6/models/karting/race_mapspot', ['racing_mapspot'], (256, 192, 64, 64)),
]


def main():
    ap = argparse.ArgumentParser()
    default = ROOT / 'ref' / 'resources'
    if not (default / 'phase_6').is_dir():
        default = ROOT / 'ref' / 'ref' / 'resources'
    ap.add_argument('--res', type=Path, default=default)
    ap.add_argument('--png', type=Path, help='also write the atlas as a PNG (for review)')
    args = ap.parse_args()
    rows = ui_atlas.bake(args.res, PARTS, ATLAS_W, ATLAS_H, OUT, 'race_gui', args.png)
    ui_atlas.write_header(HEADER, 'tools/pipeline/bake_race_gui.py', 'tt::ui', 'kRaceGuiAtlasW',
                          'kRaceGuiAtlasH', ATLAS_W, ATLAS_H, 'RaceArt', 'RaceArtRect', rows,
                          'the part in the atlas (pixels, top-left origin)')


if __name__ == '__main__':
    main()
