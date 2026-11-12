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

import assemble  # noqa: E402
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
    # The barrel keeps its origin at the pivot (bam pos 0, 0, 1.89703 is applied by the runtime).
    'cannon_barrel': (MG + 'toon_cannon', '**/cannon', (0, 0, 0), 1.0, (0, 0, 0)),
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

    # Diving (DistributedDivingGame.onstage: environModel 2.8, 2.8, 2.73 at (0, 0.5, -41);
    # boat at (0, 3, 40 - 6.75) scale 2.8; DivingTreasure: treasure.bam with chest-local
    # scale (1, 0.7, 1) and position (0, 0, -25), then the treasure node scales 0.04 at runtime)
    'diving_game': (MG + 'diving_game', None, (0, 0, 0), (2.8, 2.8, 2.73), (0, 0.5, -41.0)),
    'diving_boat': (MG + 'diving_game', '**/boat', (0, 0, 0), 2.8, (0, 3.0, 33.25)),
    'diving_chest': (MG + 'treasure', None, (0, 0, 0), (1.0, 0.7, 1.0), (0, 0, -25.0)),
    # Fish: the upstream DivingFishSpawn.createFish actors in their zero (bind) pose. The swim
    # animation is not baked; the runtime applies each type's heading and scale.
    'diving_fish_clown': ('phase_4/models/char/clownFish-zero', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'diving_fish_pbj': ('phase_4/models/char/PBJfish-zero', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'diving_fish_bear': ('phase_4/models/char/BearAcuda-zero', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'diving_fish_balloon': ('phase_4/models/char/balloonFish-zero', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'diving_fish_nurse': ('phase_4/models/char/nurseShark-zero', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'diving_fish_piano': ('phase_4/models/char/pianoTuna-zero', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Vine
    # vine_game.bam also holds the bats and spiders: each SequenceNode frame (VineSpider spider_2 /
    # spider_3, VineBat bat__1 / bat2 / bat3) is its own model, posed and flipped at run time.
    'vine_background': (MG + 'vine_game', '**/background', (0, 0, 0), 1.0, (0, 0, 0)),
    'vine_bananas': (MG + 'vine_game', '**/bananas', (0, 0, 0), 1.0, (0, 0, 0)),
    'vine_start': (MG + 'vine_game_shelf', '**/start1', (0, 0, 0), 1.0, (0, 0, 0)),
    'vine_end': (MG + 'vine_game_shelf', '**/end1', (0, 0, 0), 1.0, (0, 0, 0)),
    'vine_spider1': (MG + 'vine_game', '**/spider_2', (0, 0, 0), 1.0, (0, 0, 0)),
    'vine_spider2': (MG + 'vine_game', '**/spider_3', (0, 0, 0), 1.0, (0, 0, 0)),
    'vine_bat1': (MG + 'vine_game', '**/bat__1', (0, 0, 0), 1.0, (0, 0, 0)),
    'vine_bat2': (MG + 'vine_game', '**/bat2', (0, 0, 0), 1.0, (0, 0, 0)),
    'vine_bat3': (MG + 'vine_game', '**/bat3', (0, 0, 0), 1.0, (0, 0, 0)),

    # Target
    'target_ground': (MG + 'slingshot_game_desert4', None, (0, 0, 0), 1.0, (0, 0, 0)),
    # RubberBand.setup: the slingshot frame (its own drop shadow included), model z -1 at run time.
    'target_sling': (MG + 'slingshot_game_sling', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'target_tramp': (MG + 'slingshot_game_tramp', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Cog Thief
    # cogthief_game holds six coplanar hood floors; DistributedCogThiefGame.load shows one, so
    # each is baked alone as cogthief_ground_<hood> (bake_model stashes the other five).
    **{'cogthief_ground_' + h: (MG + 'cogthief_game', None, (0, 0, 0), 1.0, (0, 0, 0))
       for h in ('TT', 'DD', 'DG', 'MM', 'BR', 'DL')},
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
    # TwoDStomper moves the head (setFluidZ), scales the shaft (z) and keeps the block at 1 / scale.
    'twod_stomper_head': (MG + 'toonblitz_game_stomper', '**/head', (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_stomper_shaft': (MG + 'toonblitz_game_stomper', '**/shaft', (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_stomper_block': (MG + 'toonblitz_game_stomper', '**/block_geom', (0, 0, 0), 1.0, (0, 0, 0)),
    # ToonBlitzAssetMgr.background: the factory backdrop at the world origin.
    'twod_background': (MG + 'toonblitz_game', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_arrow': (MG + 'toonblitz_game_arrow', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'twod_treasure': (MG + 'moneyIcon', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Tag
    'tag_arena': (MG + 'tag_arena', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'tag_treasure': ('phase_3.5/models/props/mickeySZ', '**/mickeySZ', (0, -90, 0), 1.6, (0, 0, 0)),
    # DistributedTagGame.load: the IT sign drawn above the IT toon (setIt: itPointer.setZ(getHeight())).
    'tag_pointer': (MG + 'bboard-pointer', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Pattern
    'pattern_stage': (MG + 'matching_room', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Ice Slide
    # DistributedIceGame.load: gameBoard = ice_game_icerink (the rink, +-20 x +-15) with the
    # flat ice_game_2d backdrop under it. No walls: the west and east wall NodePaths are empty.
    'ice_arena': (MG + 'ice_game_icerink', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'ice_backdrop': (MG + 'ice_game_2d', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'ice_tire': (MG + 'ice_game_tire', None, (0, 0, 0), 1.0, (0, 0, 0)),
    # The score circle: setScale(0.01) then grown to each score's distance (scoreMovie).
    # IceTreasure models: DistributedIceGame.load scales the penalty TNT (0.75, 0.75, 0.7).
    'ice_barrel': (MG + 'ice_game_barrel', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'ice_tnt': (MG + 'ice_game_tnt2', None, (0, 0, 0), (0.75, 0.75, 0.7), (0, 0, 0)),
    # DistributedIceWorld.createCubicObstacle / createCircularObstacle: the hood's obstacles.
    'ice_crate': (MG + 'ice_game_crate', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'ice_tirestack': (MG + 'ice_game_tirestack', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'ice_score_circle': (MG + 'ice_game_score_circle', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Race
    'race_track': (MG + 'race', None, (0, 0, 0), 1.0, (0, 0, 0)),
    # setChancePositions' question_mark (scale 0.7 and the board placement at run time).
    'race_chance_marker': (MG + 'question_mark', None, (0, 0, 0), 1.0, (0, 0, 0)),

    # Photo Fun
    'photo_viewfinder': (MG + 'photo_game_viewfinder', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'photo_scene': (MG + 'photo_game_viewfinder', None, (0, 0, 0), 1.0, (0, 0, 0)),
    'photo_subject': (MG + 'photogame_camera', None, (0, 0, 0), 1.0, (0, 0, 0)),
}

# The game floors and terrain fill the screen: they keep up to 256 px (AGENTS.md section 4,
# terrain), props stay at --max-tex.
TERRAIN = {'catch_ground_2', 'catch_ground_3', 'catch_ground_4', 'cannon_gameground', 'cannon_hill',
           'tug_dock', 'ring_game', 'maze_1player', 'target_ground', 'tag_arena', 'pattern_stage',
           'ice_arena', 'race_track', 'diving_game', 'memory_room', 'vine_background', 'twod_background',
           *('cogthief_ground_' + h for h in ('TT', 'DD', 'DG', 'MM', 'BR', 'DL'))}

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


def add_twod_exit_sign(base, resources, elevator):
    """TwoDSectionMgr.setupEndSection: a sign_sellBotHeadHQ on the exit elevator's doorway2 at
    (0, 1.9, 15), scale 15 x 15 x 15 * 0.7227, with OnscreenText TwoDGameElevatorExit ('EXIT', the
    suit font, black, pos (0, -0.34), scale 0.15) on it. The sign is a DecalEffect: its letters are
    pulled a hair towards the viewer instead."""
    from panda3d.core import DynamicTextFont, TextNode
    sign_model = base.loader.loadModel(Filename.fromOsSpecific(os.path.join(resources, 'phase_4/models/props/sign_sellBotHeadHQ.bam')))
    door = elevator.find('**/doorway2')
    if door.isEmpty():
        raise RuntimeError('toonblitz_game_elevator: no doorway2')
    sign = sign_model.find('**/sign_sellBotHeadHQ').copyTo(door)
    sign.setPosHprScale(0, 1.9, 15, 0, 0, 0, 15, 15, 15 * 0.7227)
    font = DynamicTextFont(Filename.fromOsSpecific(os.path.join(resources, 'phase_3/models/fonts/vtRemingtonPortable.ttf')))
    font.setPixelsPerUnit(40)
    tn = TextNode('exit')
    tn.setFont(font)
    tn.setAlign(TextNode.ACenter)
    tn.setTextColor(0, 0, 0, 1)
    tn.setText('EXIT')
    letters = sign.attachNewNode(tn.generate())
    letters.setPos(0, -0.002, -0.34)
    letters.setScale(0.15, 1, 0.15)


def bake_model(base, resources, name, spec, out_dir, max_tex):
    bam, subnode, hpr, scale, pos = spec
    raw_model = base.loader.loadModel(Filename.fromOsSpecific(os.path.join(resources, bam + '.bam')))
    if subnode:
        model = raw_model.find(subnode)
    else:
        model = raw_model
    root = NodePath('root')
    model.reparentTo(root)
    if isinstance(scale, tuple):
        model.setScale(*scale)
    else:
        model.setScale(scale)
    model.setHpr(*hpr)
    model.setPos(*pos)
    if name.startswith('cogthief_ground_'):
        for floor in model.findAllMatches('**/floors/floor_*'):
            if floor.getName() != 'floor_' + name[-2:]:
                floor.stash()
    if name == 'twod_elevator':
        add_twod_exit_sign(base, resources, model)
    # BillboardEffect sprites (the tug-of-war palm tops, the ring game's kelp, the tag arena's
    # IT pointer) would bake frozen at their modelled heading, edge-on from most cameras.
    assemble.cross_billboards(root)
    ex = extract.Extractor(max_tex=max_tex, decals=True)  # race board track/lettering are decals
    # The anvil is a one-joint Character: apply its (bind pose) skinning.
    ex.add(root, animated=model.find('**/+Character').isEmpty() is False)
    path = os.path.join(out_dir, name + '.ttm')
    stats = ttm.write(path, ex.textures, ex.batches())
    stats['name'] = name
    return stats


def bake_vine_parts(base, resources, out_dir, max_tex):
    """SwingVine's drawn parts with vine_game's vine1 texture: one cable segment (a 1-wide card
    from z 0 down to z -1 facing -y, v 0..0.5 as UVDistance at texScale 0.15 over a 20 / 6 unit
    segment) and setupStaticPart's card (-0.5..0.5 x -0.1..8) above the pivot."""
    from panda3d.core import CardMaker
    assets = base.loader.loadModel(Filename.fromOsSpecific(os.path.join(resources, MG + 'vine_game.bam')))
    tex = assets.find('**/vine1').findTexture('*')
    out = []
    for name, frame, uv in (('vine_segment', (-0.5, 0.5, -1.0, 0.0), (0.0, 1.0, 0.0, 0.5)),
                            ('vine_static', (-0.5, 0.5, -0.1, 8.0), (0.0, 1.0, 0.0, 1.0))):
        cm = CardMaker(name)
        cm.setFrame(*frame)
        cm.setUvRange((uv[0], uv[2]), (uv[1], uv[3]))
        root = NodePath('root')
        card = root.attachNewNode(cm.generate())
        card.setTexture(tex)
        card.setTransparency(1)
        card.setTwoSided(True)
        ex = extract.Extractor(max_tex=max_tex)
        ex.add(root)
        stats = ttm.write(os.path.join(out_dir, name + '.ttm'), ex.textures, ex.batches())
        stats['name'] = name
        out.append(stats)
    return out


def bake_target_props(base, resources, out_dir, max_tex):
    """DistributedTargetGame's umbrella in each UMBRELLA_TEXTURE_LIST colour (setTexture over the
    model's own, open_umbrella and closed_umbrella apart in their node frames: the game poses them
    on the Toon's right hand), and RubberBand's band as a white two-sided unit card (x 0..1,
    z -0.5..0.5) that the runtime stretches and tints per segment."""
    from panda3d.core import CardMaker
    out = []
    model = base.loader.loadModel(Filename.fromOsSpecific(os.path.join(resources, MG + 'slingshot_game_umbrellas.bam')))
    for i, colour in enumerate(('blue', 'purple', 'red', 'yellow')):
        tex = base.loader.loadTexture(Filename.fromOsSpecific(
            os.path.join(resources, 'phase_4/maps/mg_slingshot_umbrella_%s.jpg' % colour)))
        for part in ('open', 'closed'):
            root = NodePath('root')
            node = model.find('**/%s_umbrella' % part).copyTo(root)
            node.clearTransform()
            node.setTexture(tex, 1)
            ex = extract.Extractor(max_tex=max_tex)
            ex.add(root)
            name = 'target_umbrella_%s_%d' % (part, i)
            stats = ttm.write(os.path.join(out_dir, name + '.ttm'), ex.textures, ex.batches())
            stats['name'] = name
            out.append(stats)
    cm = CardMaker('band')
    cm.setFrame(0.0, 1.0, -0.5, 0.5)
    root = NodePath('root')
    card = root.attachNewNode(cm.generate())
    card.setTwoSided(True)
    ex = extract.Extractor(max_tex=max_tex)
    ex.add(root)
    stats = ttm.write(os.path.join(out_dir, 'target_band.ttm'), ex.textures, ex.batches())
    stats['name'] = 'target_band'
    out.append(stats)
    return out


def bake_purchase_stages(base, resources, out_dir, max_tex):
    """Purchase.load's two sets, in world space: the reward street (enterReward: TT_A1 in front at
    (12.5, -20, -5.5) and twice behind at (-12.5 / 20, -25, -5), all h 180; the 40x40 sidewalk at
    (-20, -25, -5.5) coloured (0.9, 0.6, 0.4); door_single_square_ur_door h 180 at (0, -16.75, -5.5),
    scale (1.5, 1.5, 2), coloured (1, 0.8, 0)) and the purchase room (toon_interior at (0, 5, -1),
    its wallpaper and border wall_paper_a5 tinted (0.8, 0.67, 0.549))."""
    def load(rel):
        return base.loader.loadModel(Filename.fromOsSpecific(os.path.join(resources, rel + '.bam')))
    out = []
    root = NodePath('reward')
    fg = load('phase_3.5/models/modules/TT_A1')
    fg.reparentTo(root)
    fg.setPosHpr(12.5, -20, -5.5, 180, 0, 0)
    bl = load('phase_3.5/models/modules/TT_A1')
    bl.reparentTo(root)
    bl.setPosHpr(-12.5, -25, -5, 180, 0, 0)
    br = bl.copyTo(root)
    br.setPos(20, -25, -5)
    sidewalk = load('phase_3.5/models/modules/street_modules').find('**/street_sidewalk_40x40').copyTo(root)
    sidewalk.setPos(-20, -25, -5.5)
    sidewalk.setColor(0.9, 0.6, 0.4)
    door = load('phase_4/models/modules/doors').find('**/door_single_square_ur_door').copyTo(root)
    door.setH(180)
    door.setPos(0, -16.75, -5.5)
    door.setScale(1.5, 1.5, 2.0)
    door.setColor(1.0, 0.8, 0, 1)
    room = NodePath('room')
    bg = load('phase_3.5/models/modules/toon_interior')
    bg.reparentTo(room)
    bg.setPos(0.0, 5.0, -1.0)
    tex = base.loader.loadTexture(Filename.fromOsSpecific(os.path.join(resources, 'phase_3.5/maps/wall_paper_a5.jpg')))
    for node in ('**/random_tc1_TI_wallpaper', '**/random_tc1_TI_wallpaper_border'):
        wall = bg.find(node)
        if wall.isEmpty():
            raise RuntimeError('toon_interior: no ' + node)
        wall.setTexture(tex, 100)
        wall.setColorScale(0.8, 0.67, 0.549, 1.0)
    for name, holder in (('purchase_reward', root), ('purchase_room', room)):
        ex = extract.Extractor(max_tex=max_tex, decals=True)  # TT_A1's window panes are decals
        ex.add(holder)
        stats = ttm.write(os.path.join(out_dir, name + '.ttm'), ex.textures, ex.batches())
        stats['name'] = name
        print(json.dumps(stats))
        out.append(stats)
    return out


def literal_assignment(path, name):
    tree = ast.parse(open(path, encoding='utf-8').read())
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return ast.literal_eval(node.value)
    raise ValueError(name)


def bake_pairing(base, resources, out_dir, max_tex):
    # The open-toontown checkout next to --resources, else this tree's submodule.
    upstream = os.path.join(os.path.dirname(os.path.abspath(resources)), 'open-toontown', 'toontown')
    if not os.path.isdir(upstream):
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
        ex = extract.Extractor(max_tex=max_tex, decals=True)  # race board track/lettering are decals
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
    ap.add_argument('--only', choices=('catch', 'cannon', 'tug', 'ring', 'maze', 'pairing', 'memory', 'playing', 'diving', 'vine', 'twod', 'photo', 'tag', 'ice', 'target', 'purchase', 'race', 'audio'), help='filter bake targets')
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
            stats = bake_model(base, args.resources, name, spec, args.out,
                               max(args.max_tex, 256) if name in TERRAIN else args.max_tex)
            report['models'].append(stats)
            print(json.dumps(stats))

        if args.only in (None, 'vine'):
            report['models'].extend(bake_vine_parts(base, args.resources, args.out, args.max_tex))

        if args.only in (None, 'purchase'):
            report['models'].extend(bake_purchase_stages(base, args.resources, args.out, args.max_tex))

        if args.only in (None, 'target'):
            report['models'].extend(bake_target_props(base, args.resources, args.out, args.max_tex))

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
