#!/usr/bin/env python3
"""Generate source/world/cog_attack_tables.h, reference vectors, and docs/design/cog_attacks.md
from upstream MovieSuitAttacks.py and SuitBattleGlobals.py.

    .venv/bin/python -I tools/gen_cog_attacks.py
    .venv/bin/python -I tools/gen_cog_attacks.py --check
"""
import sys
sys.dont_write_bytecode = True
import argparse
import ast
import json
import os
import re
from pathlib import Path
from unittest.mock import MagicMock

import random
random.seed(0)

ROOT = Path(__file__).resolve().parent.parent
# The main checkout has ref/<submodule>; linked worktrees reach it through ref/ref.
REF = ROOT / 'ref' if (ROOT / 'ref/open-toontown/toontown').is_dir() else ROOT / 'ref/ref'
RES = (REF / 'resources').resolve()
UPSTREAM = (REF / 'open-toontown').resolve()
OUT_HEADER = ROOT / 'source/world/cog_attack_tables.h'
OUT_JSON = ROOT / 'tools/cog_attacks_test/reference_vectors.json'
OUT_DOC = ROOT / 'docs/design/cog_attacks.md'

sys.path.insert(0, str(UPSTREAM))
sys.path.insert(0, str(ROOT / 'tools/pipeline'))

from panda3d.core import (
    loadPrcFileData, Point3, Vec3, VBase3, NodePath, NodePathCollection
)

loadPrcFileData('', f'window-type none\naudio-library-name null\ndefault-model-extension .bam\nmodel-path {RES}')
from direct.showbase.ShowBase import ShowBase
from direct.actor.Actor import Actor

base = ShowBase()
import builtins
builtins.base = base
builtins.CFSpeech = 1
builtins.CFTimeout = 2

sys.modules['panda3d.otp'] = MagicMock()
sys.modules['panda3d.toontown'] = MagicMock()

import toontown.battle.BattleParticles as BP

class MockParticle(NodePath):
    def __init__(self, name):
        super().__init__('particle')
        self.name = name
    def start(self, *a, **k): pass
    def stop(self, *a, **k): pass
    def cleanup(self, *a, **k): pass
    def setEffectTexture(self, *a, **k): pass
    def getParticlesNamed(self, *a, **k):
        m = MagicMock()
        m.renderer = MagicMock()
        return m

recorded_particles = []
def mock_createParticleEffect(*a, **k):
    name = a[0] if a else k.get('file', k.get('name', 'unknown'))
    recorded_particles.append(str(name))
    return MockParticle(name)

BP.createParticleEffect = mock_createParticleEffect
BP.setEffectTexture = lambda *a, **k: None

import toontown.battle.MovieSuitAttacks as MSA
from toontown.battle.SuitBattleGlobals import SuitAttacks, SuitAttributes, ATK_TGT_GROUP, ATK_TGT_SINGLE
from toontown.battle import MovieUtil

def extract_attacks():
    random.seed(0)
    # Parse MovieCamera.py for openShotDuration
    mc_text = (UPSTREAM / 'toontown/battle/MovieCamera.py').read_text()
    open_shot_map = {}
    for m in re.finditer(r'elif name == ([A-Z0-9_]+):\s+camTrack\.append\(defaultCamera\(openShotDuration=([0-9.]+)\)\)', mc_text):
        open_shot_map[m.group(1)] = float(m.group(2))
    # Load anim lists from Suit.py
    suit_py = (UPSTREAM / 'toontown/suit/Suit.py').read_text()
    tree = ast.parse(suit_py)
    anim_lists = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            target_name = node.targets[0].id
            if isinstance(node.value, ast.Tuple):
                try: anim_lists[target_name] = ast.literal_eval(node.value)
                except: pass

    model_prefixes = {'a': 'suitA', 'b': 'suitB', 'c': 'suitC'}
    mod_phases = {'a': 3.5, 'b': 3.5, 'c': 3.5}

    suit_actors = {}
    for body in 'abc':
        prefix = model_prefixes[body]
        mod = f'phase_{mod_phases[body]:g}/models/char/{prefix}-mod'
        anim_dict = {}
        for lst_name, lst in anim_lists.items():
            for item in lst:
                if len(item) == 3:
                    name, suffix, phase = item
                    anim_file = f'phase_{phase:g}/models/char/{prefix}-{suffix}'
                    if (RES / (anim_file + '.bam')).is_file():
                        anim_dict[name] = anim_file
        act = Actor(mod, anim_dict)
        act.rightHand = act.attachNewNode('rightHand')
        act.leftHand = act.attachNewNode('leftHand')
        act.getRightHand = lambda act=act: act.rightHand
        act.getLeftHand = lambda act=act: act.leftHand
        act.setChatAbsolute = lambda *a: None
        act.clearChat = lambda *a: None
        act.battleTrapProp = None
        act.height = 8.0
        act.getHeight = lambda: 8.0
        act.getStyleName = lambda: 'f'
        suit_actors[body] = act

    # Toon actor
    toon_anims = {
        'cringe': 'phase_3.5/models/char/tt_a_chr_dgm_shorts_legs_cringe',
        'conked': 'phase_3.5/models/char/tt_a_chr_dgm_shorts_legs_conked',
        'slip-forward': 'phase_4/models/char/tt_a_chr_dgm_shorts_legs_slip-forward',
        'slip-backward': 'phase_4/models/char/tt_a_chr_dgm_shorts_legs_slip-backward',
        'duck': 'phase_3.5/models/char/tt_a_chr_dgm_shorts_legs_duck',
        'sidestep-left': 'phase_3.5/models/char/tt_a_chr_dgm_shorts_legs_sidestep-left',
        'sidestep-right': 'phase_3.5/models/char/tt_a_chr_dgm_shorts_legs_sidestep-right',
        'jump': 'phase_3.5/models/char/tt_a_chr_dgm_shorts_legs_jump',
        'juggle': 'phase_5/models/char/tt_a_chr_dgm_shorts_legs_juggle',
        'struggle': 'phase_5/models/char/tt_a_chr_dgm_shorts_legs_struggle',
        'spit': 'phase_5/models/char/tt_a_chr_dgm_shorts_legs_spit',
    }
    valid_toon_anims = {k: v for k, v in toon_anims.items() if (RES / (v + '.bam')).is_file()}
    toon_actor = Actor('phase_3/models/char/tt_a_chr_dgm_shorts_legs_1000', valid_toon_anims)
    toon_actor.height = 3.5
    toon_actor.shoulderHeight = 3.0
    toon_actor.dropShadow = toon_actor.attachNewNode('dropShadow')
    toon_actor.arms = toon_actor.attachNewNode('arms')
    toon_actor.sleeves = toon_actor.attachNewNode('sleeves')
    toon_actor.hands = toon_actor.attachNewNode('hands')
    toon_actor.style = MagicMock()
    toon_actor.style.getArmColor = lambda: Vec3(1, 1, 1)
    toon_actor.style.getHeadColor = lambda: Vec3(1, 1, 1)
    toon_actor.style.getLegColor = lambda: Vec3(1, 1, 1)
    toon_actor.style.getAnimal = lambda: 'dog'
    toon_actor.style.head = 'dls'
    toon_actor.style.torso = 'ms'
    toon_actor.style.legs = 'm'

    def makeNPC(name, count=3):
        npc = NodePathCollection()
        for i in range(count):
            npc.addPath(base.render.attachNewNode(f'{name}_{i}'))
        return npc

    toon_actor.getHeadParts = lambda: makeNPC('head', 3)
    toon_actor.getHipsParts = lambda: makeNPC('hips', 3)
    toon_actor.getTorsoParts = lambda: makeNPC('torso', 3)
    toon_actor.getLegsParts = lambda: makeNPC('legs', 3)
    toon_actor.getHeight = lambda: 3.5

    class MockBattle(NodePath):
        def __init__(self):
            super().__init__('battle')
            self.movie = MagicMock()
        def getActorPosHpr(self, a):
            return (Point3(0, 10, 0), VBase3(0, 0, 0))
        def isSuitLured(self, s):
            return False

    battle = MockBattle()
    battle.reparentTo(base.render)

    current_hit = {}
    current_dodge = {}
    current_props = []
    current_sounds = []

    orig_getToonTakeDamageTrack = MSA.getToonTakeDamageTrack
    def intercepted_getToonTakeDamageTrack(toon, died, dmg, delay, damageAnimNames = None, splicedDamageAnims = None, showDamageExtraTime = 0.01):
        current_hit['delay'] = delay + showDamageExtraTime
        current_hit['anims'] = list(damageAnimNames) if damageAnimNames else [s[0] for s in splicedDamageAnims] if splicedDamageAnims else ['cringe']
        return orig_getToonTakeDamageTrack(toon, died, dmg, delay, damageAnimNames, splicedDamageAnims, showDamageExtraTime)

    orig_getToonDodgeTrack = MSA.getToonDodgeTrack
    def intercepted_getToonDodgeTrack(target, dodgeDelay, dodgeAnimNames, splicedDodgeAnims, showMissedExtraTime):
        current_dodge['delay'] = dodgeDelay
        current_dodge['anims'] = list(dodgeAnimNames) if dodgeAnimNames else [s[0] for s in splicedDodgeAnims] if splicedDodgeAnims else ['sidestep']
        return orig_getToonDodgeTrack(target, dodgeDelay, dodgeAnimNames, splicedDodgeAnims, showMissedExtraTime)

    orig_getSoundTrack = MSA.getSoundTrack
    def intercepted_getSoundTrack(fileName, delay = 0.01, duration = None, node = None):
        current_sounds.append({'file': fileName, 'delay': delay})
        return orig_getSoundTrack(fileName, delay, duration, node)

    orig_getPropAppearTrack = MSA.getPropAppearTrack
    def intercepted_getPropAppearTrack(*args, **kwargs):
        prop = args[0]
        parent = args[1]
        parent_name = parent.getName() if hasattr(parent, 'getName') else str(parent)
        delay = args[3] if len(args) > 3 else kwargs.get('appearDelay', 0)
        current_props.append({'prop': prop.getName(), 'parent': parent_name, 'delay': delay, 'throw': False})
        return orig_getPropAppearTrack(*args, **kwargs)

    orig_getPropThrowTrack = MSA.getPropThrowTrack
    def intercepted_getPropThrowTrack(*args, **kwargs):
        prop = args[1] if len(args) > 1 else kwargs.get('prop')
        current_props.append({'prop': prop.getName() if prop else 'prop', 'throw': True})
        return orig_getPropThrowTrack(*args, **kwargs)

    MSA.getToonTakeDamageTrack = intercepted_getToonTakeDamageTrack
    MSA.getToonDodgeTrack = intercepted_getToonDodgeTrack
    MSA.getSoundTrack = intercepted_getSoundTrack
    MSA.getPropAppearTrack = intercepted_getPropAppearTrack
    MSA.getPropThrowTrack = intercepted_getPropThrowTrack

    # Known actor props
    actor_props = {'calculator', 'clip-on-tie', 'shredder-paper', 'smile', 'stormcloud', 'teeth'}

    attacks = []
    for name, (anim, tgt) in SuitAttacks.items():
        attack_id = list(SuitAttacks.keys()).index(name)
        group = tgt == ATK_TGT_GROUP
        fn_name = 'do' + name
        fn = getattr(MSA, fn_name, None)
        if fn is None:
            fn = MSA.doDefault

        current_hit.clear()
        current_dodge.clear()
        current_props.clear()
        current_sounds.clear()
        recorded_particles.clear()

        suit_a = suit_actors['a']
        suit_a.reparentTo(battle)
        toon_actor.reparentTo(battle)
        target = [{'toon': toon_actor, 'hp': 10, 'died': 0, 'leftToons': [], 'rightToons': []}] if group else {'toon': toon_actor, 'hp': 10, 'died': 0, 'leftToons': [], 'rightToons': []}
        attack = {
            'suit': suit_a,
            'suitName': 'bs',
            'battle': battle,
            'target': target,
            'id': attack_id,
            'name': name,
            'animName': anim,
            'group': tgt,
            'taunt': 0,
        }
        track = fn(attack)
        dur_a = track.getDuration()
        hit_info = dict(current_hit)
        props_hit = list(current_props)
        sounds_hit = list(current_sounds)
        particles_hit = sorted(list(set(recorded_particles)))

        # Check miss
        current_hit.clear()
        current_dodge.clear()
        current_props.clear()
        current_sounds.clear()
        recorded_particles.clear()
        target_miss = [{'toon': toon_actor, 'hp': 0, 'died': 0, 'leftToons': [], 'rightToons': []}] if group else {'toon': toon_actor, 'hp': 0, 'died': 0, 'leftToons': [], 'rightToons': []}
        attack['target'] = target_miss
        track_miss = fn(attack)
        dodge_info = dict(current_dodge)

        # Body b & c
        dur_b = dur_a
        dur_c = dur_a
        try:
            suit_b = suit_actors['b']
            attack['suit'] = suit_b
            attack['suitName'] = 'b'
            attack['target'] = target
            track_b = fn(attack)
            dur_b = track_b.getDuration()
        except: pass

        try:
            suit_c = suit_actors['c']
            attack['suit'] = suit_c
            attack['suitName'] = 'f'
            attack['target'] = target
            track_c = fn(attack)
            dur_c = track_c.getDuration()
        except: pass

        primary_prop = ''
        prop_anim = ''
        hold_left = False
        is_throw = False
        for p in props_hit:
            p_name = p.get('prop', '')
            if p_name and not primary_prop:
                primary_prop = p_name
                if primary_prop in actor_props:
                    prop_anim = primary_prop
                if 'left' in p.get('parent', '').lower():
                    hold_left = True
            if p.get('throw'):
                is_throw = True

        sound_file = ''
        sound_delay = 0.0
        if sounds_hit:
            sound_file = sounds_hit[0]['file']
            sound_delay = sounds_hit[0]['delay']

        const_name = re.sub(r'(?<!^)(?=[A-Z])', '_', name).upper()
        if const_name == 'CLIP_ON_TIE': const_name = 'CLIPON_TIE'
        open_shot = open_shot_map.get(const_name, 3.5)

        hit_anim = hit_info.get('anims', ['cringe'])[0]
        miss_anim = dodge_info.get('anims', ['sidestep'])[0]
        if miss_anim == 'sidestep':
            miss_anim = 'sidestep-left'

        attacks.append({
            'id': attack_id,
            'name': name,
            'anim': anim,
            'group': group,
            'dur_a_ms': int(round(dur_a * 1000)),
            'dur_b_ms': int(round(dur_b * 1000)),
            'dur_c_ms': int(round(dur_c * 1000)),
            'hit_delay_ms': int(round(hit_info.get('delay', 1.5) * 1000)),
            'dodge_delay_ms': int(round(dodge_info.get('delay', 1.2) * 1000)),
            'hit_anim': hit_anim,
            'miss_anim': miss_anim,
            'prop_name': primary_prop,
            'prop_anim': prop_anim,
            'is_throw': is_throw,
            'hold_left': hold_left,
            'sound_file': sound_file,
            'sound_delay_ms': int(round(sound_delay * 1000)),
            'open_shot_sec': open_shot,
            'particles': particles_hit,
        })

    # Cog mappings (32 cogs x 6 attacks)
    dna_tree = ast.parse((UPSTREAM / 'toontown/suit/SuitDNA.py').read_text())
    codes = None
    for n in dna_tree.body:
        if isinstance(n, ast.Assign) and getattr(n.targets[0], 'id', None) == 'suitHeadTypes':
            codes = ast.literal_eval(n.value)
            break

    attrs_tree = ast.parse((UPSTREAM / 'toontown/battle/SuitBattleGlobals.py').read_text())
    cog_attacks_map = {}
    for n in attrs_tree.body:
        if isinstance(n, ast.Assign) and getattr(n.targets[0], 'id', None) == 'SuitAttributes':
            for k, v in zip(n.value.keys, n.value.values):
                code = k.value
                for dict_k, dict_v in zip(v.keys, v.values):
                    if dict_k.value == 'attacks':
                        cog_attacks_map[code] = [elt.elts[0].value for elt in dict_v.elts]

    return attacks, codes, cog_attacks_map


def generate_header(attacks, codes, cog_attacks_map):
    lines = [
        "// GENERATED by tools/gen_cog_attacks.py from MovieSuitAttacks.py & SuitBattleGlobals.py.",
        "// Do not edit directly.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <cstring>",
        "",
        "namespace tt::world {",
        "",
        "struct CogAttackInfo {",
        "    uint8_t id;",
        "    const char* name;",
        "    const char* animName;",
        "    bool isGroup;",
        "    uint16_t durationMs[3]; // body 'a'=0, 'b'=1, 'c'=2",
        "    int16_t hitDelayMs;",
        "    int16_t dodgeDelayMs;",
        "    const char* toonHitAnim;",
        "    const char* toonMissAnim;",
        "    const char* propName;",
        "    const char* propAnim;",
        "    bool isThrow;",
        "    bool holdLeftHand;",
        "    const char* soundName;",
        "    uint16_t soundDelayMs;",
        "    float openShotSec;",
        "};",
        "",
        f"constexpr int kNumCogAttacks = {len(attacks)};",
        "constexpr CogAttackInfo kCogAttacks[kNumCogAttacks] = {",
    ]

    for a in attacks:
        prop_str = f'"{a["prop_name"]}"' if a["prop_name"] else '""'
        prop_anim_str = f'"{a["prop_anim"]}"' if a["prop_anim"] else '""'
        sound_str = f'"{a["sound_file"]}"' if a["sound_file"] else '""'
        group_str = 'true' if a['group'] else 'false'
        throw_str = 'true' if a['is_throw'] else 'false'
        left_str = 'true' if a['hold_left'] else 'false'
        lines.append(
            f"    {{{a['id']}, \"{a['name']}\", \"{a['anim']}\", {group_str}, "
            f"{{{a['dur_a_ms']}, {a['dur_b_ms']}, {a['dur_c_ms']}}}, "
            f"{a['hit_delay_ms']}, {a['dodge_delay_ms']}, \"{a['hit_anim']}\", \"{a['miss_anim']}\", "
            f"{prop_str}, {prop_anim_str}, {throw_str}, {left_str}, {sound_str}, {a['sound_delay_ms']}, {a['open_shot_sec']}f}},"
        )

    lines.extend([
        "};",
        "",
        f"constexpr int kNumCogs = {len(codes)};",
        "// Mapping of (cogHeadTypeIdx 0..31, attackIdx 0..5) -> attackId (0..66, or -1 if none)",
        "constexpr int8_t kCogAttackTable[kNumCogs][6] = {",
    ])

    for code in codes:
        atk_list = cog_attacks_map.get(code, [])
        ids = []
        for i in range(6):
            if i < len(atk_list):
                atk_name = atk_list[i]
                atk_id = next((a['id'] for a in attacks if a['name'] == atk_name), -1)
                ids.append(str(atk_id))
            else:
                ids.append("-1")
        lines.append(f"    {{{', '.join(ids)}}}, // {code}")

    lines.extend([
        "};",
        "",
        "inline const CogAttackInfo* getCogAttackInfo(int attackId) {",
        "    if (attackId >= 0 && attackId < kNumCogAttacks) {",
        "        return &kCogAttacks[attackId];",
        "    }",
        "    return nullptr;",
        "}",
        "",
        "inline int getCogAttackId(int headTypeIdx, int attackIndex) {",
        "    if (headTypeIdx >= 0 && headTypeIdx < kNumCogs && attackIndex >= 0 && attackIndex < 6) {",
        "        return kCogAttackTable[headTypeIdx][attackIndex];",
        "    }",
        "    return -1;",
        "}",
        "",
        "inline int findCogHeadTypeIndex(const char* code) {",
        "    if (!code) return -1;",
        "    static const char* const kCodes[32] = {",
    ])

    for i in range(0, len(codes), 8):
        chunk = [f'"{c}"' for c in codes[i:i+8]]
        lines.append(f"        {', '.join(chunk)},")

    lines.extend([
        "    };",
        "    for (int i = 0; i < 32; ++i) {",
        "        if (!strcmp(code, kCodes[i])) return i;",
        "    }",
        "    return -1;",
        "}",
        "",
        "inline int getCogAttackId(const char* code, int attackIndex) {",
        "    const int idx = findCogHeadTypeIndex(code);",
        "    return idx >= 0 ? getCogAttackId(idx, attackIndex) : -1;",
        "}",
        "",
        "inline int findAttackIdByName(const char* name) {",
        "    if (!name) return -1;",
        "    for (int i = 0; i < kNumCogAttacks; ++i) {",
        "        if (!strcmp(kCogAttacks[i].name, name)) return i;",
        "    }",
        "    return -1;",
        "}",
        "",
        "} // namespace tt::world",
    ])
    return '\n'.join(lines) + '\n'


def generate_design_doc(attacks, codes, cog_attacks_map):
    lines = [
        "# Cog Attack Movies Specification (`MovieSuitAttacks`)",
        "",
        "## 1. Overview",
        "",
        "In Toontown combat, each Cog round executes Cog attacks chosen by the AI server.",
        "The server sends `DistributedBattle.setMovie` containing `BattleSuitAttack` structures.",
        "Each suit attack specifies:",
        "- `suitIdx`: 0..3 index of the attacking Cog",
        "- `attack`: 0..5 index into the Cog species' attack table (`SuitAttributes[cog]['attacks']`)",
        "- `target`: target toon slot (-1 for group attacks, 0..3 for single target)",
        "- `hp`: array of damage dealt per toon slot (0 = miss / dodge, >0 = damage hit)",
        "",
        "Rather than playing a generic 2200 ms slot with a generic `throw-paper` clip and cringe,",
        "the native 3DS client implements the full upstream `MovieSuitAttacks` specification with table-driven",
        "timing, props, projectile paths, animations, sounds, and toon reactions.",
        "",
        "## 2. Upstream Architecture & 3DS Implementation",
        "",
        "### 2.1 Attack Table & Dispatch",
        "There are **67 total attacks** defined in `SuitBattleGlobals.SuitAttacks`.",
        "- **63 attacks** are actively used by the 32 Cog species on stock Open Toontown.",
        "- **4 attacks** are unused by any stock Cog: `FiveOClockShadow`, `Gavel`, `ReOrg`, `SandTrap`.",
        "- **7 attacks** map to `doDefault` in upstream `MovieSuitAttacks.py`: `CigarSmoke`, `FiveOClockShadow`,",
        "  `FloodTheMarket`, `Gavel`, `SandTrap`, `SongAndDance`, `ThrowBook`.",
        "",
        "### 2.2 Body Timings",
        "Suit animations run at 24 fps in Panda3D and are baked at 12 fps for 3DS morph skinning.",
        "Attacks with body-specific releases (`suitType in ('a', 'b', 'c')`) have per-body durations and delays",
        "stored in `CogAttackInfo.durationMs[3]`. The runtime selects the duration based on `body % 3`.",
        "",
        "### 2.3 Props & Trajectories",
        "Cog attack props are baked into `.ttm` static meshes or `.ttm` + `.tta` actors with ETC1 textures.",
        "- Held props attach to the Cog's `rightHand` or `leftHand` joint.",
        "- Thrown projectiles follow a parabolic trajectory computed by `ProjectileArc` from the hand release point",
        "  to the target toon's chest / face point.",
        "",
        "### 2.4 Sounds",
        "Attack sound effects (`SA_*.ogg`) are baked to DSP-ADPCM `.tsnd` files on SD and played via `BattleFx::playCue`.",
        "",
        "### 2.5 3DS Particle Simplifications",
        "Panda3D `.ptf` particle systems with runtime physics are simplified on the 3DS GPU to respect the 30 fps locked budget:",
        "- **Impact bursts & splashes** (`FountainPen`, `Demotion`, `Watercooler`, `Liquidate`): rendered using compact",
        "  quad splat/splash cards (`splat.ttm`, `splash.ttm`) with depth bias.",
        "- **Floating text / letters** (`BuzzWord`, `DoubleTalk`, `Filibuster`, `Jargon`, `Legalese`, `MumboJumbo`):",
        "  omitted on Old 3DS to avoid draw-call overhead; the animated prop, head glow, sound effect, and toon reaction carry the comedic action.",
        "- **Clouds & props** (`BrainStorm` stormcloud, `Canned`, `RedTape` tubes, `Sacked`): rendered as baked 3D meshes.",
        "",
        "## 3. Complete Attack Reference (All 67 Attacks)",
        "",
        "| ID | Attack Name | Cog Anim | Group | Dur A (ms) | Dur B (ms) | Dur C (ms) | Hit Delay (ms) | Toon Hit | Toon Miss | Prop | Sound | 3DS Treatment |",
        "|---|---|---|---|---|---|---|---|---|---|---|---|---|",
    ]

    for a in attacks:
        grp = "Yes" if a['group'] else "No"
        prop = a['prop_name'] if a['prop_name'] else "None"
        snd = a['sound_file'].replace('.ogg', '') if a['sound_file'] else "None"
        simpl = "Baked mesh" if a['prop_name'] else ("Particle omitted" if a['particles'] else "Standard")
        if a['is_throw']: simpl += ", parabolic arc"
        lines.append(
            f"| {a['id']} | **{a['name']}** | `{a['anim']}` | {grp} | "
            f"{a['dur_a_ms']} | {a['dur_b_ms']} | {a['dur_c_ms']} | {a['hit_delay_ms']} | "
            f"`{a['hit_anim']}` | `{a['miss_anim']}` | `{prop}` | `{snd}` | {simpl} |"
        )

    lines.extend([
        "",
        "## 4. Per-Cog Attack Availability",
        "",
        "Below is the mapping from each Cog species code to its attacks in `SuitAttributes`:",
        "",
        "| Code | Dept | Species | Attack 0 | Attack 1 | Attack 2 | Attack 3 | Attack 4 | Attack 5 |",
        "|---|---|---|---|---|---|---|---|---|",
    ])

    for code in codes:
        atks = cog_attacks_map.get(code, [])
        cols = [f"`{atks[i]}`" if i < len(atks) else "-" for i in range(6)]
        lines.append(f"| `{code}` | - | - | " + " | ".join(cols) + " |")

    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='Check if generated files are up to date')
    args = ap.parse_args()

    attacks, codes, cog_attacks_map = extract_attacks()
    header_content = generate_header(attacks, codes, cog_attacks_map)
    doc_content = generate_design_doc(attacks, codes, cog_attacks_map)
    json_content = json.dumps({'attacks': attacks, 'codes': codes, 'cog_attacks_map': cog_attacks_map}, indent=2)

    if args.check:
        if not OUT_HEADER.exists() or OUT_HEADER.read_text() != header_content:
            sys.exit(f"{OUT_HEADER} is stale. Run without --check to regenerate.")
        if not OUT_JSON.exists() or OUT_JSON.read_text() != json_content:
            sys.exit(f"{OUT_JSON} is stale. Run without --check to regenerate.")
        print("gen_cog_attacks: all generated files are up to date.")
        return

    OUT_HEADER.parent.mkdir(parents=True, exist_ok=True)
    OUT_HEADER.write_text(header_content)
    print(f"Wrote {OUT_HEADER}")

    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(json_content)
    print(f"Wrote {OUT_JSON}")

    OUT_DOC.parent.mkdir(parents=True, exist_ok=True)
    OUT_DOC.write_text(doc_content)
    print(f"Wrote {OUT_DOC}")


if __name__ == '__main__':
    main()
