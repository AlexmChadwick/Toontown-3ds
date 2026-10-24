#!/usr/bin/env python3
"""Bake the minigame scenes, props and sounds the 3DS client needs (docs/design/trolley.md).

    .venv/bin/python -I tools/pipeline/bake_minigame.py [--resources ref/resources] \
        [--out sdmc-out/3ds/toontown3ds/data/minigames] [--only catch]

Static models reuse tools/pipeline/extract.py + ttm.py exactly like bake_model.py; sounds reuse
bake_audio.py (music -> DSP-ADPCM, sfx -> PCM16). Output (SD set, not committed):

    minigames/catch_ground_{2,3,4}.ttm   DistributedCatchGame.load: treehouse_{2,3,4}players, rotated by
                                         ground.setHpr(180, -90, 0) so the stage is the z = 0 plane
    minigames/catch_<fruit>.ttm          apple, orange, pear, coconut, watermelon, pineapple
    minigames/catch_anvil.ttm            phase_4/models/props/anvil-mod
    minigames/audio/<name>.tsnd          trolley and Catch sounds

The Catch game roots each falling object at the sphere centre and then calls setZ every frame,
so the per-model scale and offsets of DistributedCatchGame.load (modelScales, pear setZ(-.6),
coconut setP(180), watermelon setH(135) setZ(-.5), pineapple setZ(-1.7), anvil setZ(-ObjRadius))
are baked into the vertices here (upstream's `model.flattenMedium()` does the same to the copies
it hands out). The runtime then only translates and spins them.
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
# name -> (bam, hpr, scale, pos). Transforms follow the order Panda applies setScale/setHpr/setPos.
MODELS = {
    'catch_ground_2': (MG + 'treehouse_2players', (180, -90, 0), 1.0, (0, 0, 0)),
    'catch_ground_3': (MG + 'treehouse_3players', (180, -90, 0), 1.0, (0, 0, 0)),
    'catch_ground_4': (MG + 'treehouse_4players', (180, -90, 0), 1.0, (0, 0, 0)),
    'catch_apple': (MG + 'apple', (0, 0, 0), 0.7, (0, 0, 0)),
    'catch_orange': (MG + 'orange', (0, 0, 0), 0.7, (0, 0, 0)),
    'catch_pear': (MG + 'pear', (0, 0, 0), 0.5, (0, 0, -0.6)),
    'catch_coconut': (MG + 'coconut', (0, 180, 0), 0.7, (0, 0, 0)),
    'catch_watermelon': (MG + 'watermelon', (135, 0, 0), 0.6, (0, 0, -0.5)),
    'catch_pineapple': (MG + 'pineapple', (0, 0, 0), 0.45, (0, 0, -1.7)),
    'catch_anvil': ('phase_4/models/props/anvil-mod', (0, 0, 0), 1.0, (0, 0, -1.0)),
}

# (output name, upstream path, kind, loops). Sources: DistributedTrolley.py (bell, away),
# DistributedCatchGame.py load() (music and the four sounds), MinigameRulesPanel (none).
AUDIO = [
    ('trolley_bell', 'phase_4/audio/sfx/SZ_trolley_bell.ogg', 'sfx', False),
    ('trolley_away', 'phase_4/audio/sfx/SZ_trolley_away.ogg', 'sfx', False),
    ('mg_toontag', 'phase_4/audio/bgm/MG_toontag.ogg', 'music', False),
    ('mg_oof', 'phase_4/audio/sfx/MG_cannon_hit_dirt.ogg', 'sfx', False),
    ('mg_anvil_land', 'phase_4/audio/sfx/AA_drop_anvil_miss.ogg', 'sfx', False),
    ('mg_perfect', 'phase_4/audio/sfx/ring_perfect.ogg', 'sfx', False),
    ('mg_win', 'phase_4/audio/sfx/MG_win.ogg', 'sfx', False),
    ('mg_lose', 'phase_4/audio/sfx/MG_lose.ogg', 'sfx', False),
]
# treasure_grab (SZ_DD_treasure, the good-catch sound) is already in the boot set.


def bake_model(base, resources, name, spec, out_dir, max_tex):
    bam, hpr, scale, pos = spec
    model = base.loader.loadModel(Filename.fromOsSpecific(os.path.join(resources, bam + '.bam')))
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
    ap.add_argument('--only', choices=('catch', 'audio'), help='bake only the Catch models or only the sounds')
    ap.add_argument('--max-tex', type=int, default=128)
    args = ap.parse_args()
    os.makedirs(os.path.join(args.out, 'audio'), exist_ok=True)

    report = {'models': [], 'audio': []}
    if args.only != 'audio':
        base = ShowBase()
        for name, spec in MODELS.items():
            stats = bake_model(base, args.resources, name, spec, args.out, args.max_tex)
            report['models'].append(stats)
            print(json.dumps(stats))
    if args.only != 'catch':
        for name, rel, kind, loops in AUDIO:
            dst = os.path.join(args.out, 'audio', name + '.tsnd')
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
