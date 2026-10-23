#!/usr/bin/env python3
"""Bake a toon (head + torso + legs actors) posed at one animation frame into a .ttm.

Phase 1: static pose only. DNA colors are *not* baked; tinted parts get a tint slot
and the runtime supplies colors (see TINT_* in source/render/model.h).

    .venv/bin/python -I tools/pipeline/bake_toon.py --head dls --torso ms --legs m \
        --lod 500 --anim neutral romfs/baked/toon_dog_m.ttm
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from panda3d.core import loadPrcFileData  # noqa: E402

RESOURCES = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', 'ref', 'resources'))
loadPrcFileData('', f'window-type none\naudio-library-name null\nnotify-level warning\n'
                    f'model-path {RESOURCES}\ndefault-model-extension .bam')
from direct.actor.Actor import Actor  # noqa: E402
from direct.showbase.ShowBase import ShowBase  # noqa: E402

import extract  # noqa: E402
import ttm  # noqa: E402

# From toontown/toon/ToonHead.py and Toon.py (new-anims branch). Dog heads only for now.
HEADS = {'dls': 'phase_3/models/char/tt_a_chr_dgm_shorts_head_',
         'dss': 'phase_3/models/char/tt_a_chr_dgm_skirt_head_',
         'dsl': 'phase_3/models/char/tt_a_chr_dgs_shorts_head_',
         'dll': 'phase_3/models/char/tt_a_chr_dgl_shorts_head_'}
TORSOS = {'ss': 'phase_3/models/char/tt_a_chr_dgs_shorts_torso_',
          'ms': 'phase_3/models/char/tt_a_chr_dgm_shorts_torso_',
          'ls': 'phase_3/models/char/tt_a_chr_dgl_shorts_torso_',
          'sd': 'phase_3/models/char/tt_a_chr_dgs_skirt_torso_',
          'md': 'phase_3/models/char/tt_a_chr_dgm_skirt_torso_',
          'ld': 'phase_3/models/char/tt_a_chr_dgl_skirt_torso_'}
LEGS = {'s': 'phase_3/models/char/tt_a_chr_dgs_shorts_legs_',
        'm': 'phase_3/models/char/tt_a_chr_dgm_shorts_legs_',
        'l': 'phase_3/models/char/tt_a_chr_dgl_shorts_legs_'}

# Tint slots; keep in sync with source/render/model.h.
TINT_HEAD, TINT_ARM, TINT_GLOVE, TINT_LEG, TINT_SHIRT, TINT_SLEEVE, TINT_BOTTOM = range(1, 8)
TINT_RULES = [('head*', TINT_HEAD), ('arms', TINT_ARM), ('neck', TINT_ARM),
              ('hands', TINT_GLOVE), ('legs', TINT_LEG), ('feet', TINT_LEG),
              ('torso-top', TINT_SHIRT), ('sleeves', TINT_SLEEVE), ('torso-bot*', TINT_BOTTOM)]
# Upstream uses drawInFront('eyes*', 'head-front*') and pupils in front of eyes.
LAYER_RULES = [('*pupil*', 2), ('eyes*', 1)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--head', default='dls', choices=HEADS)
    ap.add_argument('--torso', default='ms', choices=TORSOS)
    ap.add_argument('--legs', default='m', choices=LEGS)
    ap.add_argument('--lod', default='500', choices=['1000', '500', '250'])
    ap.add_argument('--anim', default='neutral')
    ap.add_argument('--frame', type=int, default=0)
    ap.add_argument('--max-tex', type=int, default=128)
    args = ap.parse_args()

    ShowBase()
    models = {'head': HEADS[args.head] + args.lod, 'torso': TORSOS[args.torso] + args.lod,
              'legs': LEGS[args.legs] + args.lod}
    anims = {'head': {args.anim: HEADS[args.head] + args.anim},
             'torso': {args.anim: TORSOS[args.torso] + args.anim},
             'legs': {args.anim: LEGS[args.legs] + args.anim}}
    toon = Actor(models, anims)
    toon.attach('torso', 'legs', 'joint_hips')
    head_joint = 'def_head' if not toon.getPart('torso').find('**/def_head').isEmpty() else 'joint_head'
    toon.attach('head', 'torso', head_joint)
    for name in ('boots_short', 'boots_long', 'shoes'):
        toon.findAllMatches(f'**/{name}').stash()
    toon.pose(args.anim, args.frame)
    toon.update(force=True)

    ex = extract.Extractor(max_tex=args.max_tex, tint_rules=TINT_RULES, layer_rules=LAYER_RULES)
    ex.add(toon, animated=True)
    stats = ttm.write(args.out, ex.textures, ex.batches())
    print(json.dumps(stats))


if __name__ == '__main__':
    main()
