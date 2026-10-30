#!/usr/bin/env python3
"""Bake Make-A-Toon's room (phase_3/models/makeatoon/tt_m_ara_mat_room) into one .ttm per shop.

MakeAToon.load reparents the room's `floor` and `background` to render and drops one shop's
walls + props at a time (genderWalls / genderProps, bodyWalls / ..., clothWalls / clothProps,
nameWalls / nameProps) onto roomAnim_model's scalingJoint / propJoint. Both joints are identity
at rest (pose 0 of roomSquish / propSquish), so every shop is baked in room coordinates:
floor + background + that shop's walls + props, one file each, so the front end keeps only the
current shop resident. The translucent `spotlight` cone (colour 1,1,1,0.3 on spotlightJoint,
setPos(1.18, -1.27, 0.41), scale 2.6; the Name shop moves it to (2, -1.95, 0.41), scale 2.3) is
baked into each shop at that shop's placement.

Textures: max 256 (the room fills the whole top screen), ETC1 by the usual rules. All shops draw
from the same 15 makeatoon palettes, so the five models are pooled (TTM v2 + one textures.ttp,
tools/pipeline/ttm_pool.py): each payload is stored once, and a shop loads only the ones it uses.

    .venv/bin/python -I tools/pipeline/bake_mat_room.py [--res ref/resources] [--out romfs/baked/mat_room]
"""
import argparse
import json
import os
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from panda3d.core import Filename, NodePath, TransparencyAttrib, loadPrcFileData  # noqa: E402

loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                'textures-power-2 none')
from direct.showbase.ShowBase import ShowBase  # noqa: E402

import extract  # noqa: E402
import ttm  # noqa: E402
import ttm_pool  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
# (output name, walls node, props node, spotlight offset from spotlightJoint, spotlight scale)
SHOPS = (
    ('gender', 'genderWalls', 'genderProps', (1.18, -1.27, 0.41), 2.6),
    ('body', 'bodyWalls', 'bodyProps', (1.18, -1.27, 0.41), 2.6),
    ('color', 'colorWalls', 'colorProps', (1.18, -1.27, 0.41), 2.6),
    ('clothes', 'clothWalls', 'clothProps', (1.18, -1.27, 0.41), 2.6),
    ('name', 'nameWalls', 'nameProps', (2.0, -1.95, 0.41), 2.3),
)
# roomAnim_model's spotlightJoint at rest (roomAnim_spotlightShake frame 0): a pure translation.
SPOTLIGHT_JOINT = (-4.41766, -6.02209, 1.99801)
MAX_TEX = 256


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--res', default=str(ROOT / 'ref' / 'resources'))
    ap.add_argument('--out', default=str(ROOT / 'romfs' / 'baked' / 'mat_room'))
    args = ap.parse_args()
    base = ShowBase()
    path = Path(args.res) / 'phase_3' / 'models' / 'makeatoon' / 'tt_m_ara_mat_room.bam'
    room = base.loader.loadModel(Filename.fromOsSpecific(str(path.resolve())))
    for name, walls, props, spot_pos, spot_scale in SHOPS:
        scene = NodePath('mat_' + name)
        for node in ('floor', 'background', walls, props):
            found = room.find('**/' + node)
            if found.isEmpty():
                sys.exit(f'{path} is missing {node}')
            copy = found.copyTo(scene)
            copy.setTransform(found.getTransform(room))
        spot = room.find('**/spotlight')
        if spot.isEmpty():
            sys.exit(f'{path} is missing spotlight')
        spot = spot.copyTo(scene)
        spot.setPosHprScale(SPOTLIGHT_JOINT[0] + spot_pos[0], SPOTLIGHT_JOINT[1] + spot_pos[1],
                            SPOTLIGHT_JOINT[2] + spot_pos[2], 0, 0, 0, spot_scale, spot_scale, spot_scale)
        spot.setColor(1, 1, 1, 0.3)
        spot.setTransparency(TransparencyAttrib.MAlpha)
        ex = extract.Extractor(max_tex=MAX_TEX)
        ex.add(scene)
        out = Path(args.out) / f'mat_room_{name}.ttm'
        out.parent.mkdir(parents=True, exist_ok=True)
        stats = ttm.write(str(out), ex.textures, ex.batches())
        print(f'{out}: {json.dumps(stats)}')
    ttm_pool.run(Path(args.out))
    total = sum(f.stat().st_size for f in Path(args.out).iterdir())
    print(f'{args.out}: {total} bytes pooled')


if __name__ == '__main__':
    main()
