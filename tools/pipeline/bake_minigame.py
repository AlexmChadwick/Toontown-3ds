#!/usr/bin/env python3
"""Bake the minigame scenes, props and sounds the 3DS client needs (docs/design/trolley.md).

    .venv/bin/python -I tools/pipeline/bake_minigame.py [--resources ref/resources] \
        [--out sdmc-out/3ds/toontown3ds/data/minigames] [--only catch|cannon|tug|ring|audio]
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

from panda3d.core import Filename, NodePath, loadPrcFileData  # noqa: E402

loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                'textures-power-2 none')
from direct.showbase.ShowBase import ShowBase  # noqa: E402

import bake_audio  # noqa: E402
import extract  # noqa: E402
import ttm  # noqa: E402

MG = 'phase_4/models/minigames/'
# name -> (bam, subnode, hpr, scale, pos)
MODELS = {
    # Catching
    'catch_ground_2': (MG + 'treehouse_2players', None, (180, -90, 0), 1.0, (0, 0, 0)),
    'catch_ground_3': (MG + 'treehouse_3players', None, (180, -90, 0), 1.0, (0, 0, 0)),
    'catch_ground_4': (MG + 'treehouse_4players', None, (180, -90, 0), 1.0, (0, 0, 0)),
    'catch_apple': (MG + 'apple', None, (0, 0, 0), 0.7, (0, 0, 0)),
    'catch_orange': (MG + 'orange', None, (0, 0, 0), 0.7, (0, 0, 0)),
    'catch_pear': (MG + 'pear', None, (0, 0, 0), 0.5, (0, 0, -0.6)),
    'catch_coconut': (MG + 'coconut', None, (0, 180, 0), 0.7, (0, 0, 0)),
    'catch_watermelon': (MG + 'watermelon', None, (135, 0, 0), 0.6, (0, 0, -0.5)),
    'catch_pineapple': (MG + 'pineapple', None, (0, 0, 0), 0.45, (0, 0, -1.7)),
    'catch_anvil': ('phase_4/models/props/anvil-mod', None, (0, 0, 0), 1.0, (0, 0, -1.0)),

    # Cannon
    'cannon_gameground': (MG + 'toon_cannon_gameground', None, (0, 0, 0), 2.8, (0, 0, 0)),
    'cannon_tower': (MG + 'toon_cannon_water_tower', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'cannon_hill': (MG + 'cannon_hill', None, (0, 0, 0), 1.0, (0, -127.67, 0)),
    'cannon_base': (MG + 'toon_cannon', '**/cannon_base', (0, 0, 0), 1.0, (0, 0, 0)),
    'cannon_barrel': (MG + 'toon_cannon', '**/cannon', (0, 0, 0), 1.0, (0, 0, -1.89703)),

    # Tug of War
    'tug_dock': (MG + 'tug_of_war_dock', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'tug_rope': (MG + 'tug_of_war_rope', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Ring
    'ring_game': (MG + 'swimming_game', None, (0, 0, 0), 1.0, (0, 75.0, -14.5)),
    'ring_ring': (MG + 'swimming_game_ring', None, (0, 0, 0), 0.75, (0, 0, 0)),
}

# (output name, upstream path, kind, loops)
AUDIO = [
    # Trolley
    ('trolley_bell', 'phase_4/audio/sfx/SZ_trolley_bell.ogg', 'sfx', False),
    ('trolley_away', 'phase_4/audio/sfx/SZ_trolley_away.ogg', 'sfx', False),
    # Shared / Catching
    ('mg_toontag', 'phase_4/audio/bgm/MG_toontag.ogg', 'music', False),
    ('mg_oof', 'phase_4/audio/sfx/MG_cannon_hit_dirt.ogg', 'sfx', False),
    ('mg_anvil_land', 'phase_4/audio/sfx/AA_drop_anvil_miss.ogg', 'sfx', False),
    ('mg_perfect', 'phase_4/audio/sfx/ring_perfect.ogg', 'sfx', False),
    ('mg_win', 'phase_4/audio/sfx/MG_win.ogg', 'sfx', False),
    ('mg_lose', 'phase_4/audio/sfx/MG_lose.ogg', 'sfx', False),
    # Cannon
    ('mg_cannon_game', 'phase_4/audio/bgm/MG_cannon_game.ogg', 'music', False),
    ('mg_cannon_adjust', 'phase_4/audio/sfx/MG_cannon_adjust.ogg', 'sfx', False),
    ('mg_cannon_fire', 'phase_4/audio/sfx/MG_cannon_fire_alt.ogg', 'sfx', False),
    ('mg_cannon_hit_tower', 'phase_4/audio/sfx/MG_cannon_hit_tower.ogg', 'sfx', False),
    ('mg_cannon_splash', 'phase_4/audio/sfx/MG_cannon_splash.ogg', 'sfx', False),
    ('mg_cannon_whizz', 'phase_4/audio/sfx/MG_cannon_whizz.ogg', 'sfx', False),
    # Tug of War
    ('mg_tug_o_war', 'phase_4/audio/bgm/MG_tug_o_war.ogg', 'music', False),
    ('mg_whistle', 'phase_4/audio/sfx/AA_sound_whistle.ogg', 'sfx', False),
    # Ring
    ('mg_ambient_water', 'phase_4/audio/sfx/AV_ambient_water.ogg', 'sfx', False),
    ('ring_get', 'phase_4/audio/sfx/ring_get.ogg', 'sfx', False),
    ('ring_miss', 'phase_4/audio/sfx/ring_miss.ogg', 'sfx', False),
]


def bake_model(base, resources, name, spec, out_dir, max_tex):
    bam, subnode, hpr, scale, pos = spec
    raw_model = base.loader.loadModel(Filename.fromOsSpecific(os.path.join(resources, bam + '.bam')))
    if subnode:
        model = raw_model.find(subnode)
    else:
        model = raw_model
    root = NodePath('root')
    model.reparentTo(root)
    model.setScale(scale)
    model.setHpr(*hpr)
    model.setPos(*pos)
    ex = extract.Extractor(max_tex=max_tex)
    # The anvil is a one-joint Character: apply its (bind pose) skinning.
    ex.add(root, animated=model.find('**/+Character').isEmpty() is False)
    path = os.path.join(out_dir, name + '.ttm')
    stats = ttm.write(path, ex.textures, ex.batches())
    stats['name'] = name
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--resources', default=os.path.join(ROOT, 'ref', 'resources'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'sdmc-out', '3ds', 'toontown3ds', 'data', 'minigames'))
    ap.add_argument('--only', choices=('catch', 'cannon', 'tug', 'ring', 'audio'), help='filter bake targets')
    ap.add_argument('--max-tex', type=int, default=128)
    args = ap.parse_args()

    # Fallback for resources path
    if not os.path.exists(os.path.join(args.resources, 'phase_4')):
        for cand in [os.path.join(ROOT, 'ref', 'ref', 'resources'), '/Users/alexchadwick/toontown-3ds/ref/resources']:
            if os.path.exists(os.path.join(cand, 'phase_4')):
                args.resources = cand
                break

    os.makedirs(os.path.join(args.out, 'audio'), exist_ok=True)

    report = {'models': [], 'audio': []}
    if args.only != 'audio':
        base = ShowBase()
        for name, spec in MODELS.items():
            if args.only and not name.startswith(args.only):
                continue
            stats = bake_model(base, args.resources, name, spec, args.out, args.max_tex)
            report['models'].append(stats)
            print(json.dumps(stats))

    if args.only in (None, 'audio'):
        for name, rel, kind, loops in AUDIO:
            dst = os.path.join(args.out, 'audio', name + '.tsnd')
            if os.path.exists(dst):
                # Don't re-encode audio if already present
                continue
            src = os.path.join(args.resources, rel)
            fn = bake_audio.bake_music if kind == 'music' else bake_audio.bake_sfx
            size, n, rate, ch = fn(src, dst, loops)[:4]
            report['audio'].append({'name': name, 'bytes': size, 'seconds': round(n / rate, 2)})
            print(f'{name:16} {kind:5} {size / 1024:8.1f} KB {n / rate:6.1f} s <- {rel}')

    total = sum(os.path.getsize(os.path.join(args.out, f)) for f in os.listdir(args.out)
                if os.path.isfile(os.path.join(args.out, f)))
    total += sum(os.path.getsize(os.path.join(args.out, 'audio', f)) for f in os.listdir(os.path.join(args.out, 'audio')))
    print(f'minigames set: {total / 1024:.0f} KB in {args.out}')


if __name__ == '__main__':
    main()
