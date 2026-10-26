#!/usr/bin/env python3
"""Bake the minigame scenes, props and sounds the 3DS client needs (docs/design/trolley.md).

    .venv/bin/python -I tools/pipeline/bake_minigame.py [--resources ref/resources] \
        [--out sdmc-out/3ds/toontown3ds/data/minigames] [--only catch|cannon|tug|ring|audio]
"""
import argparse
import ast
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
    # DistributedCannonGame and DistributedTagGame parent TT_sky to render.
    'minigame_sky': ('phase_3.5/models/props/TT_sky', None, (0, 0, 0), 2.8, (0, 0, 0)),

    # Tug of War
    'tug_dock': (MG + 'tug_of_war_dock', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'tug_rope': (MG + 'tug_of_war_rope', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Ring
    'ring_game': (MG + 'swimming_game', None, (0, 0, 0), 1.0, (0, 75.0, -14.5)),
    'ring_ring': (MG + 'swimming_game_ring', None, (0, 0, 0), 0.75, (0, 0, 0)),

    # Maze
    'maze_1player': (MG + 'maze_1player', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'maze_treasure': ('phase_3.5/models/props/mickeySZ', '**/mickeySZ', (0, -90, 0), 1.6, (0, 0, 0)),

    # Match Minnie / Pairing
    'memory_room': (MG + 'memory_room', None, (0, 0, 0), 1.0, (0, 0, 0)),
    # Pairing cards are composed below, exactly as PairingGameCard.load.

    # Diving
    'diving_game': (MG + 'diving_game', None, (0, 0, 0), 2.8, (0, 0.5, -41.0)),
    'diving_boat': (MG + 'diving_game', '**/boat', (0, 0, 0), 2.8, (0, 3.0, 33.25)),
    'diving_chest': (MG + 'treasure_chest', None, (0, 0, 0), 1.5, (0, 0, 0)),

    # Vine
    'vine_game': (MG + 'vine_game', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'vine_shelf': (MG + 'vine_game_shelf', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Target
    'target_ground': (MG + 'slingshot_game_desert4', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'target_sling': (MG + 'slingshot_game_cacti', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'target_tramp': (MG + 'slingshot_game_tramp', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Cog Thief
    'cogthief_ground': (MG + 'cogthief_game', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'cogthief_barrel': (MG + 'cogthief_game_gagTank', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'cogthief_tank': (MG + 'cogthief_game_gagTank', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Two-D (Toon Escape / ToonBlitz)
    'twod_block00': (MG + 'toonblitz_game_block00', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_block01': (MG + 'toonblitz_game_block01', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_block02': (MG + 'toonblitz_game_block02', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_block03': (MG + 'toonblitz_game_block03', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_wall': (MG + 'toonblitz_game_wall', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_pipe': (MG + 'toonblitz_game_start', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_elevator': (MG + 'toonblitz_game_elevator', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_stomper': (MG + 'toonblitz_game_stomper', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_arrow': (MG + 'toonblitz_game_arrow', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_treasure': (MG + 'moneyIcon', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Tag
    'tag_arena': (MG + 'tag_arena', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'tag_treasure': ('phase_3.5/models/props/mickeySZ', '**/mickeySZ', (0, -90, 0), 1.6, (0, 0, 0)),

    # Pattern
    'pattern_stage': (MG + 'matching_room', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Ice Slide
    'ice_arena': (MG + 'ice_game', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'ice_tire': (MG + 'ice_game_tire', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Race
    'race_track': (MG + 'race', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Photo Fun
    'photo_viewfinder': (MG + 'photo_game_viewfinder', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'photo_scene': (MG + 'photo_game_viewfinder', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'photo_subject': (MG + 'photogame_camera', None, (0, 0, 0), 1.0, (0, 0, 0)),
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
    # Maze
    ('mg_maze_pickup', 'phase_4/audio/sfx/MG_maze_pickup.ogg', 'sfx', False),
    ('mg_tag_c', 'phase_4/audio/sfx/MG_Tag_C.ogg', 'sfx', False),
    # Pairing
    ('mg_pairing', 'phase_4/audio/bgm/MG_Pairing.ogg', 'music', False),
    ('mg_pairing_match', 'phase_4/audio/sfx/MG_pairing_match_bonus_both.ogg', 'sfx', False),
    ('mg_pairing_turn', 'phase_4/audio/sfx/MG_pairing_card_flip_face_up.ogg', 'sfx', False),
    # Diving
    ('mg_target', 'phase_4/audio/bgm/MG_Target.ogg', 'music', False),
    ('mg_diving_hit', 'phase_4/audio/sfx/diving_game_hit.ogg', 'sfx', False),
    ('mg_diving_drop', 'phase_4/audio/sfx/diving_treasure_drop_off.ogg', 'sfx', False),
    ('mg_diving_pickup', 'phase_4/audio/sfx/diving_treasure_pick_up.ogg', 'sfx', False),
    # Vine
    ('mg_vine', 'phase_4/audio/bgm/MG_Vine.ogg', 'music', False),
    ('mg_vine_bananas', 'phase_4/audio/sfx/MG_sfx_vine_game_bananas.ogg', 'sfx', False),
    ('mg_vine_fall', 'phase_4/audio/sfx/MG_sfx_vine_game_fall.ogg', 'sfx', False),
    ('mg_vine_jump', 'phase_4/audio/sfx/MG_sfx_vine_game_jump.ogg', 'sfx', False),
    # Target
    ('mg_target_launch', 'phase_4/audio/sfx/target_launch.ogg', 'sfx', False),
    ('mg_target_trampoline', 'phase_4/audio/sfx/target_trampoline_2.ogg', 'sfx', False),
    # Cog Thief
    ('mg_slice', 'phase_5/audio/sfx/AA_slice_only.ogg', 'sfx', False),
    # Two-D
    ('mg_twod_bgm', 'phase_4/audio/bgm/MG_TwoDGame.ogg', 'music', False),
    ('mg_squirt_hit', 'phase_4/audio/sfx/Seltzer_squirt_2dgame_hit.ogg', 'sfx', False),
    # Tag
    ('mg_tag_tag', 'phase_5/audio/sfx/MG_tag_1.ogg', 'sfx', False),
    # Ice Slide
    ('mg_ice_scoring', 'phase_4/audio/sfx/MG_sfx_ice_scoring_1.ogg', 'sfx', False),
    # Race
    ('mg_race_bgm', 'phase_4/audio/bgm/minigame_race.ogg', 'music', False),
    # Photo Fun
    ('photo_shutter', 'phase_4/audio/sfx/Photo_shutter.ogg', 'sfx', False),
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


def literal_assignment(path, name):
    tree = ast.parse(open(path, encoding='utf-8').read())
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return ast.literal_eval(node.value)
    raise ValueError(name)


def bake_pairing(base, resources, out_dir, max_tex):
    upstream = os.path.join(ROOT, 'ref', 'open-toontown', 'toontown')
    if not os.path.isdir(upstream):  # linked worktrees reach the submodules through ref/ref
        upstream = os.path.join(ROOT, 'ref', 'ref', 'open-toontown', 'toontown')
    props = literal_assignment(os.path.join(upstream, 'toonbase', 'ToontownBattleGlobals.py'), 'AvPropsNew')
    colors = literal_assignment(os.path.join(upstream, 'minigame', 'PairingGameCard.py'), 'CardColors')
    def load(path):
        model = base.loader.loadModel(Filename.fromOsSpecific(os.path.join(resources, path + '.bam')))
        if model.isEmpty():
            raise RuntimeError(path)
        return model
    icons = load('phase_3.5/models/gui/inventory_icons')
    reports = []
    for rank in range(13):
        root = NodePath('pairing_card')
        sign = load('phase_4/models/minigames/garden_sign_memory')
        sign.find('**/glow').removeNode()
        sign.find('**/sign1').setColorScale(*colors[rank % len(colors)])
        sign.find('**/sign2').setColorScale(.12, .35, .5, 1)
        prop = root.attachNewNode('prop')
        image = icons.find('**/' + props[rank % 7][rank // 7])
        if image.isEmpty():
            raise RuntimeError('missing card gag icon')
        image.copyTo(prop)
        prop.setScale(7)
        prop.setPos(0, 0, .08)
        prop.setP(-90)
        prop.reparentTo(sign)
        sign.reparentTo(root)
        card = load('phase_3.5/models/gui/playingCard')
        logo = card.find('**/logo')
        logo.reparentTo(root)
        logo.setScale(.45)
        logo.setP(90)
        logo.setZ(.025)
        logo.setX(-.05)
        logo.setH(180)
        card.removeNode()
        root.setScale(2.5)
        scene = NodePath('scene')
        root.reparentTo(scene)
        ex = extract.Extractor(max_tex=max_tex)
        ex.add(scene)
        path = os.path.join(out_dir, 'pairing_card_%d.ttm' % rank)
        stats = ttm.write(path, ex.textures, ex.batches())
        stats['name'] = 'pairing_card_%d' % rank
        reports.append(stats)
        print(json.dumps(stats))
        root.removeNode()
    icons.removeNode()
    return reports


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--resources', default=os.path.join(ROOT, 'ref', 'resources'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'sdmc-out', '3ds', 'toontown3ds', 'data', 'minigames'))
    ap.add_argument('--only', choices=('catch', 'cannon', 'tug', 'ring', 'maze', 'pairing', 'memory', 'playing', 'diving', 'vine', 'twod', 'photo', 'audio'), help='filter bake targets')
    ap.add_argument('--max-tex', type=int, default=128)
    args = ap.parse_args()

    # Fallback for resources path
    if not os.path.exists(os.path.join(args.resources, 'phase_4')):
        for cand in [os.path.join(ROOT, 'ref', 'ref', 'resources')]:
            if os.path.exists(os.path.join(cand, 'phase_4')):
                args.resources = cand
                break

    os.makedirs(os.path.join(args.out, 'audio'), exist_ok=True)

    report = {'models': [], 'audio': []}
    if args.only != 'audio':
        base = ShowBase()
        for name, spec in MODELS.items():
            needs_sky = args.only == 'cannon' and name == 'minigame_sky'
            if args.only and not (name.startswith(args.only) or needs_sky or args.only == 'pairing' and name == 'memory_room'):
                continue
            stats = bake_model(base, args.resources, name, spec, args.out, args.max_tex)
            report['models'].append(stats)
            print(json.dumps(stats))

        if args.only in (None, 'pairing'):
            report['models'].extend(bake_pairing(base, args.resources, args.out, args.max_tex))

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
