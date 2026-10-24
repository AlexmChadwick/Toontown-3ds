#!/usr/bin/env python3
"""Generate source/ui/cchar_chatter_data.{h,cpp} from upstream Open Toontown.

Usage:
    .venv/bin/python -I tools/gen_cchar_chatter.py [repo_path] [out_dir]

Sources (read with ast, never imported or vendored):
    - toontown/classicchars/CCharChatter.py
    - toontown/toonbase/TTLocalizerEnglish.py
    - toontown/toonbase/ToontownGlobals.py

Output is deterministic and uses no heap allocations at runtime.
"""
import ast
import os
import sys

# DC class IDs for classic characters
CLASS_IDS = {
    'DistributedMickey': 67,
    'DistributedVampireMickey': 68,
    'DistributedWitchMinnie': 69,
    'DistributedMinnie': 70,
    'DistributedGoofy': 71,
    'DistributedDaisy': 72,
    'DistributedSockHopDaisy': 73,
    'DistributedChip': 74,
    'DistributedPoliceChip': 75,
    'DistributedDale': 76,
    'DistributedJailbirdDale': 77,
    'DistributedDonald': 78,
    'DistributedFrankenDonald': 79,
    'DistributedDonaldDock': 80,
    'DistributedPluto': 81,
    'DistributedWesternPluto': 82,
    'DistributedGoofySpeedway': 83,
    'DistributedSuperGoofy': 84,
}

CHAR_TO_CLASSES = {
    'Mickey': [67],
    'VampireMickey': [68],
    'WitchMinnie': [69],
    'Minnie': [70],
    'Goofy': [71, 83],
    'Daisy': [72],
    'SockHopDaisy': [73],
    'Chip': [74],
    'PoliceChip': [75],
    'Dale': [76],
    'JailbirdDale': [77],
    'Donald': [78],
    'FrankenDonald': [79],
    'DonaldDock': [80],
    'Pluto': [81],
    'WesternPluto': [82],
    'SuperGoofy': [84],
}


def find_repo(hint=None):
    candidates = []
    if hint:
        candidates.append(hint)
    candidates.extend([
        'ref/open-toontown',
        'ref/ref/open-toontown',
        '../toontown-3ds/ref/open-toontown',
        '/Users/alexchadwick/toontown-3ds/ref/open-toontown',
    ])
    for c in candidates:
        if os.path.exists(os.path.join(c, 'toontown', 'classicchars', 'CCharChatter.py')):
            return c
    sys.exit(f"Could not find open-toontown repository. Checked: {candidates}")


def parse_ttlocalizer(repo):
    path = os.path.join(repo, 'toontown', 'toonbase', 'TTLocalizerEnglish.py')
    with open(path, 'r', encoding='utf-8') as f:
        tree = ast.parse(f.read(), path)

    env = {}

    def eval_node(node):
        if node is None:
            return None
        if isinstance(node, ast.Constant):
            return node.value
        if isinstance(node, ast.Name):
            return env.get(node.id, node.id)
        if isinstance(node, ast.BinOp):
            left = eval_node(node.left)
            right = eval_node(node.right)
            if isinstance(node.op, ast.Add):
                if isinstance(left, (str, list, tuple)) and isinstance(right, (str, list, tuple)):
                    return left + right
            if isinstance(node.op, ast.Mod):
                return str(left) % right
        if isinstance(node, ast.List):
            return [eval_node(e) for e in node.elts]
        if isinstance(node, ast.Tuple):
            return [eval_node(e) for e in node.elts]
        if isinstance(node, ast.Subscript):
            val = eval_node(node.value)
            idx = eval_node(node.slice)
            if isinstance(val, (list, tuple)) and isinstance(idx, int):
                return val[idx]
        return None

    for stmt in tree.body:
        if isinstance(stmt, ast.Assign):
            val = eval_node(stmt.value)
            for t in stmt.targets:
                if isinstance(t, ast.Name):
                    env[t.id] = val
        elif isinstance(stmt, ast.For):
            items = eval_node(stmt.iter)
            if items and isinstance(items, list) and isinstance(stmt.target, ast.Name):
                target_name = stmt.target.id
                for item in items:
                    env[target_name] = item
                    for b_stmt in stmt.body:
                        if isinstance(b_stmt, ast.Expr) and isinstance(b_stmt.value, ast.Call):
                            call = b_stmt.value
                            if isinstance(call.func, ast.Attribute) and call.func.attr == 'extend':
                                obj = eval_node(call.func.value)
                                arg = eval_node(call.args[0])
                                if isinstance(obj, list) and isinstance(arg, list):
                                    obj.extend(arg)

    return env


def parse_toontownglobals(repo):
    path = os.path.join(repo, 'toontown', 'toonbase', 'ToontownGlobals.py')
    with open(path, 'r', encoding='utf-8') as f:
        tree = ast.parse(f.read(), path)
    tg = {}
    for stmt in tree.body:
        if isinstance(stmt, ast.Assign):
            for t in stmt.targets:
                if isinstance(t, ast.Name) and isinstance(stmt.value, ast.Constant):
                    tg[t.id] = stmt.value.value
    return tg


def get_extended_chat(chatset, extended_chat):
    new_chat = []
    for chat_list in chatset:
        new_chat.append(list(chat_list))
    new_chat[1] += extended_chat
    return new_chat


def c_str(s):
    out = []
    for b in s.encode('utf-8'):
        c = chr(b)
        if c == '"' or c == '\\':
            out.append('\\' + c)
        elif 32 <= b < 127:
            out.append(c)
        elif b < 32 and b != 10:
            continue
        else:
            out.append('\\%03o' % b)
    return '"' + ''.join(out) + '"'


def build_chatter_table(repo):
    env = parse_ttlocalizer(repo)
    tg = parse_toontownglobals(repo)

    MickeyChatter = env['MickeyChatter']
    VampireMickeyChatter = env['VampireMickeyChatter']
    MinnieChatter = env['MinnieChatter']
    GoofyChatter = env['GoofyChatter']
    GoofySpeedwayChatter = env['GoofySpeedwayChatter']
    DonaldChatter = env['DonaldChatter']
    ChipChatter = env['ChipChatter']
    DaleChatter = env['DaleChatter']
    DaisyChatter = env['DaisyChatter']

    def get_chatter(char_name, chatter_type):
        if char_name == 'Mickey':
            if chatter_type == tg['APRIL_FOOLS_COSTUMES']: return env['AFMickeyChatter']
            elif chatter_type in (tg['WINTER_CAROLING'], tg['WACKY_WINTER_CAROLING']): return env['WinterMickeyCChatter']
            elif chatter_type in (tg['WINTER_DECORATIONS'], tg['WACKY_WINTER_DECORATIONS']): return env['WinterMickeyDChatter']
            elif chatter_type == tg['VALENTINES_DAY']: return env['ValentinesMickeyChatter']
            elif chatter_type == tg['SILLY_CHATTER_ONE']: return get_extended_chat(MickeyChatter, env['SillyPhase1Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_TWO']: return get_extended_chat(MickeyChatter, env['SillyPhase2Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_THREE']: return get_extended_chat(MickeyChatter, env['SillyPhase3Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_FOUR']: return get_extended_chat(MickeyChatter, env['SillyPhase4Chatter'])
            elif chatter_type == tg['SELLBOT_FIELD_OFFICE']: return get_extended_chat(MickeyChatter, env['FieldOfficeMickeyChatter'])
            else: return MickeyChatter
        elif char_name == 'VampireMickey': return VampireMickeyChatter
        elif char_name == 'Minnie':
            if chatter_type == tg['APRIL_FOOLS_COSTUMES']: return env['AFMinnieChatter']
            elif chatter_type in (tg['WINTER_CAROLING'], tg['WACKY_WINTER_CAROLING']): return env['WinterMinnieCChatter']
            elif chatter_type in (tg['WINTER_DECORATIONS'], tg['WACKY_WINTER_DECORATIONS']): return env['WinterMinnieDChatter']
            elif chatter_type == tg['VALENTINES_DAY']: return env['ValentinesMinnieChatter']
            elif chatter_type == tg['SILLY_CHATTER_ONE']: return get_extended_chat(MinnieChatter, env['SillyPhase1Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_TWO']: return get_extended_chat(MinnieChatter, env['SillyPhase2Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_THREE']: return get_extended_chat(MinnieChatter, env['SillyPhase3Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_FOUR']: return get_extended_chat(MinnieChatter, env['SillyPhase4Chatter'])
            elif chatter_type == tg['SELLBOT_FIELD_OFFICE']: return get_extended_chat(MinnieChatter, env['FieldOfficeMinnieChatter'])
            else: return MinnieChatter
        elif char_name == 'WitchMinnie': return env['WitchMinnieChatter']
        elif char_name in ('Daisy', 'SockHopDaisy'):
            if chatter_type == tg['APRIL_FOOLS_COSTUMES']: return env['AFDaisyChatter']
            elif chatter_type in (tg['HALLOWEEN_COSTUMES'], tg['SPOOKY_COSTUMES']): return env['HalloweenDaisyChatter']
            elif chatter_type in (tg['WINTER_CAROLING'], tg['WACKY_WINTER_CAROLING']): return env['WinterDaisyCChatter']
            elif chatter_type in (tg['WINTER_DECORATIONS'], tg['WACKY_WINTER_DECORATIONS']): return env['WinterDaisyDChatter']
            elif chatter_type == tg['VALENTINES_DAY']: return env['ValentinesDaisyChatter']
            elif chatter_type == tg['SILLY_CHATTER_ONE']: return get_extended_chat(DaisyChatter, env['SillyPhase1Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_TWO']: return get_extended_chat(DaisyChatter, env['SillyPhase2Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_THREE']: return get_extended_chat(DaisyChatter, env['SillyPhase3Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_FOUR']: return get_extended_chat(DaisyChatter, env['SillyPhase4Chatter'])
            elif chatter_type == tg['SELLBOT_FIELD_OFFICE']: return get_extended_chat(DaisyChatter, env['FieldOfficeDaisyChatter'])
            else: return DaisyChatter
        elif char_name == 'Goofy':
            if chatter_type == tg['APRIL_FOOLS_COSTUMES']: return env['AFGoofySpeedwayChatter']
            elif chatter_type == tg['CRASHED_LEADERBOARD']: return env['CLGoofySpeedwayChatter']
            elif chatter_type == tg['CIRCUIT_RACING_EVENT']: return env['GPGoofySpeedwayChatter']
            elif chatter_type in (tg['WINTER_DECORATIONS'], tg['WINTER_CAROLING'], tg['WACKY_WINTER_DECORATIONS'], tg['WACKY_WINTER_CAROLING']): return env['WinterGoofyChatter']
            elif chatter_type == tg['VALENTINES_DAY']: return env['ValentinesGoofyChatter']
            elif chatter_type == tg['SILLY_CHATTER_ONE']: return get_extended_chat(GoofySpeedwayChatter, env['SillyPhase1Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_TWO']: return get_extended_chat(GoofySpeedwayChatter, env['SillyPhase2Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_THREE']: return get_extended_chat(GoofySpeedwayChatter, env['SillyPhase3Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_FOUR']: return get_extended_chat(GoofySpeedwayChatter, env['SillyPhase4Chatter'])
            else: return GoofySpeedwayChatter
        elif char_name == 'SuperGoofy': return env['SuperGoofyChatter']
        elif char_name in ('Donald', 'FrankenDonald'):
            if chatter_type == tg['APRIL_FOOLS_COSTUMES']: return env['AFDonaldChatter']
            elif chatter_type in (tg['HALLOWEEN_COSTUMES'], tg['SPOOKY_COSTUMES']): return env['HalloweenDreamlandChatter']
            elif chatter_type in (tg['WINTER_CAROLING'], tg['WACKY_WINTER_CAROLING']): return env['WinterDreamlandCChatter']
            elif chatter_type in (tg['WINTER_DECORATIONS'], tg['WACKY_WINTER_DECORATIONS']): return env['WinterDreamlandDChatter']
            elif chatter_type == tg['VALENTINES_DAY']: return env['ValentinesDreamlandChatter']
            elif chatter_type == tg['SELLBOT_FIELD_OFFICE']: return get_extended_chat(DonaldChatter, env['FieldOfficeDreamlandChatter'])
            else: return DonaldChatter
        elif char_name == 'DonaldDock':
            if chatter_type == tg['APRIL_FOOLS_COSTUMES']: return env['AFDonaldDockChatter']
            elif chatter_type in (tg['HALLOWEEN_COSTUMES'], tg['SPOOKY_COSTUMES']): return env['HalloweenDonaldChatter']
            elif chatter_type in (tg['WINTER_CAROLING'], tg['WACKY_WINTER_CAROLING']): return env['WinterDonaldCChatter']
            elif chatter_type in (tg['WINTER_DECORATIONS'], tg['WACKY_WINTER_DECORATIONS']): return env['WinterDonaldDChatter']
            elif chatter_type == tg['VALENTINES_DAY']: return env['ValentinesDonaldChatter']
            else: return None
        elif char_name == 'Pluto':
            if chatter_type == tg['APRIL_FOOLS_COSTUMES']: return env['AFPlutoChatter']
            elif chatter_type in (tg['HALLOWEEN_COSTUMES'], tg['SPOOKY_COSTUMES']): return env['WesternPlutoChatter']
            elif chatter_type in (tg['WINTER_CAROLING'], tg['WACKY_WINTER_CAROLING']): return env['WinterPlutoCChatter']
            elif chatter_type in (tg['WINTER_DECORATIONS'], tg['WACKY_WINTER_DECORATIONS']): return env['WinterPlutoDChatter']
            else: return None
        elif char_name == 'WesternPluto':
            if chatter_type in (tg['HALLOWEEN_COSTUMES'], tg['SPOOKY_COSTUMES']): return env['WesternPlutoChatter']
            else: return None
        elif char_name in ('Chip', 'PoliceChip'):
            if chatter_type == tg['APRIL_FOOLS_COSTUMES']: return env['AFChipChatter']
            elif chatter_type in (tg['HALLOWEEN_COSTUMES'], tg['SPOOKY_COSTUMES']): return env['HalloweenChipChatter']
            elif chatter_type in (tg['WINTER_DECORATIONS'], tg['WINTER_CAROLING'], tg['WACKY_WINTER_DECORATIONS'], tg['WACKY_WINTER_CAROLING']): return env['WinterChipChatter']
            elif chatter_type == tg['VALENTINES_DAY']: return env['ValentinesChipChatter']
            elif chatter_type == tg['SILLY_CHATTER_ONE']: return get_extended_chat(ChipChatter, env['SillyPhase1Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_TWO']: return get_extended_chat(ChipChatter, env['SillyPhase2Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_THREE']: return get_extended_chat(ChipChatter, env['SillyPhase3Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_FOUR']: return get_extended_chat(ChipChatter, env['SillyPhase4Chatter'])
            else: return ChipChatter
        elif char_name in ('Dale', 'JailbirdDale'):
            if chatter_type == tg['APRIL_FOOLS_COSTUMES']: return env['AFDaleChatter']
            elif chatter_type in (tg['HALLOWEEN_COSTUMES'], tg['SPOOKY_COSTUMES']): return env['HalloweenDaleChatter']
            elif chatter_type in (tg['WINTER_DECORATIONS'], tg['WINTER_CAROLING'], tg['WACKY_WINTER_DECORATIONS'], tg['WACKY_WINTER_CAROLING']): return env['WinterDaleChatter']
            elif chatter_type == tg['VALENTINES_DAY']: return env['ValentinesDaleChatter']
            elif chatter_type == tg['SILLY_CHATTER_ONE']: return get_extended_chat(DaleChatter, env['SillyPhase1Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_TWO']: return get_extended_chat(DaleChatter, env['SillyPhase2Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_THREE']: return get_extended_chat(DaleChatter, env['SillyPhase3Chatter'])
            elif chatter_type == tg['SILLY_CHATTER_FOUR']: return get_extended_chat(DaleChatter, env['SillyPhase4Chatter'])
            else: return DaleChatter
        return None

    # All known holiday IDs
    holiday_names = {
        0: 'DEFAULT',
        tg['WINTER_DECORATIONS']: 'WINTER_DECORATIONS',
        tg['CIRCUIT_RACING_EVENT']: 'CIRCUIT_RACING_EVENT',
        tg['HALLOWEEN_COSTUMES']: 'HALLOWEEN_COSTUMES',
        tg['APRIL_FOOLS_COSTUMES']: 'APRIL_FOOLS_COSTUMES',
        tg['CRASHED_LEADERBOARD']: 'CRASHED_LEADERBOARD',
        tg['WINTER_CAROLING']: 'WINTER_CAROLING',
        tg['VALENTINES_DAY']: 'VALENTINES_DAY',
        tg['SILLY_CHATTER_ONE']: 'SILLY_CHATTER_ONE',
        tg['SILLY_CHATTER_TWO']: 'SILLY_CHATTER_TWO',
        tg['SILLY_CHATTER_THREE']: 'SILLY_CHATTER_THREE',
        tg['SILLY_CHATTER_FOUR']: 'SILLY_CHATTER_FOUR',
        tg['SELLBOT_FIELD_OFFICE']: 'SELLBOT_FIELD_OFFICE',
        tg['SPOOKY_COSTUMES']: 'SPOOKY_COSTUMES',
        tg['WACKY_WINTER_DECORATIONS']: 'WACKY_WINTER_DECORATIONS',
        tg['WACKY_WINTER_CAROLING']: 'WACKY_WINTER_CAROLING',
    }

    # Gather all unique sets of phrases (tuple of 3 tuples)
    set_to_id = {}
    sets_data = []  # [(id, (greetings, comments, goodbyes))]

    def intern_set(chat_set):
        if chat_set is None:
            return None
        key = (tuple(chat_set[0]), tuple(chat_set[1]), tuple(chat_set[2]))
        if key in set_to_id:
            return set_to_id[key]
        idx = len(sets_data)
        set_to_id[key] = idx
        sets_data.append((idx, key))
        return idx

    # Build per-character configs
    char_configs = []
    characters = [
        'Mickey', 'VampireMickey', 'WitchMinnie', 'Minnie', 'Goofy',
        'Daisy', 'SockHopDaisy', 'Chip', 'PoliceChip', 'Dale',
        'JailbirdDale', 'Donald', 'FrankenDonald', 'DonaldDock',
        'Pluto', 'WesternPluto', 'SuperGoofy'
    ]

    for char in characters:
        default_chat = get_chatter(char, 0)
        default_set_id = intern_set(default_chat)

        overrides = []
        for h_id in sorted(holiday_names.keys()):
            if h_id == 0:
                continue
            h_chat = get_chatter(char, h_id)
            if h_chat is not None and h_chat != default_chat:
                set_id = intern_set(h_chat)
                overrides.append((h_id, set_id))

        class_ids = CHAR_TO_CLASSES[char]
        char_configs.append({
            'name': char,
            'classes': class_ids,
            'default_set_id': default_set_id,
            'overrides': overrides,
        })

    return sets_data, char_configs, holiday_names, tg


def generate_header(out_dir):
    path = os.path.join(out_dir, 'cchar_chatter_data.h')
    with open(path, 'w', encoding='utf-8') as f:
        f.write('''// GENERATED by tools/gen_cchar_chatter.py from open-toontown. Do not edit.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tt::ui::chatter {

constexpr uint32_t kGreeting = 0;
constexpr uint32_t kComment = 1;
constexpr uint32_t kGoodbye = 2;
constexpr uint32_t kCategoryCount = 3;

struct ChatterCategory {
    const char* const* phrases;
    uint16_t count;
};

struct ChatterSet {
    ChatterCategory categories[kCategoryCount];
};

struct HolidayOverride {
    uint32_t holidayId;
    const ChatterSet* chatter;
};

struct CharChatterConfig {
    const char* name;
    uint16_t classId;
    const ChatterSet* defaultChatter; // nullptr if character has no default chatter (e.g. Pluto)
    const HolidayOverride* overrides;
    uint16_t overrideCount;
};

// Raw phrase template lookup. Returns nullptr if out of bounds or character/holiday has no chatter.
const char* getChatterRaw(uint16_t classId, uint32_t chatterType, uint32_t category, uint32_t msgIndex);
const char* getChatterRawByName(const char* charName, uint32_t chatterType, uint32_t category, uint32_t msgIndex);

// Formats chatter into dst buffer, substituting '%' with targetName (if targetName is not null/empty).
// Returns true on success, false if phrase not found or out of bounds.
bool formatChatter(char* dst, size_t dstCap, uint16_t classId, uint32_t chatterType, uint32_t category, uint32_t msgIndex, const char* targetName);
bool formatChatterByName(char* dst, size_t dstCap, const char* charName, uint32_t chatterType, uint32_t category, uint32_t msgIndex, const char* targetName);

} // namespace tt::ui::chatter
''')


def generate_cpp(out_dir, sets_data, char_configs):
    path = os.path.join(out_dir, 'cchar_chatter_data.cpp')
    with open(path, 'w', encoding='utf-8') as f:
        f.write('''// GENERATED by tools/gen_cchar_chatter.py from open-toontown. Do not edit.
#include "ui/cchar_chatter_data.h"

#include <cstring>

namespace tt::ui::chatter {

namespace {

''')

        # Emit all phrase arrays for each chatter set
        cat_names = ['Greetings', 'Comments', 'Goodbyes']
        for set_id, (greetings, comments, goodbyes) in sets_data:
            cats = [greetings, comments, goodbyes]
            for c_idx, c_name in enumerate(cat_names):
                phrases = cats[c_idx]
                f.write(f'const char* const kSet_{set_id}_{c_name}[{len(phrases)}] = {{\n')
                for p in phrases:
                    f.write(f'    {c_str(p)},\n')
                f.write('};\n\n')

        # Emit ChatterSet structures
        f.write('// ChatterSets\n')
        for set_id, (greetings, comments, goodbyes) in sets_data:
            f.write(f'const ChatterSet kSet_{set_id} = {{\n')
            f.write('    {\n')
            f.write(f'        {{kSet_{set_id}_Greetings, {len(greetings)}}},\n')
            f.write(f'        {{kSet_{set_id}_Comments, {len(comments)}}},\n')
            f.write(f'        {{kSet_{set_id}_Goodbyes, {len(goodbyes)}}},\n')
            f.write('    }\n')
            f.write('};\n\n')

        # Emit HolidayOverrides per character
        for cfg in char_configs:
            c_name = cfg['name']
            overrides = cfg['overrides']
            if overrides:
                f.write(f'const HolidayOverride kOverrides_{c_name}[{len(overrides)}] = {{\n')
                for h_id, set_id in overrides:
                    f.write(f'    {{{h_id}u, &kSet_{set_id}}},\n')
                f.write('};\n\n')

        # Emit CharChatterConfig table for each classId
        f.write('const CharChatterConfig kCharConfigs[] = {\n')
        for cfg in char_configs:
            c_name = cfg['name']
            default_ptr = f'&kSet_{cfg["default_set_id"]}' if cfg['default_set_id'] is not None else 'nullptr'
            overrides_ptr = f'kOverrides_{c_name}' if cfg['overrides'] else 'nullptr'
            overrides_count = len(cfg['overrides'])
            for cls_id in cfg['classes']:
                f.write(f'    {{"{c_name}", {cls_id}, {default_ptr}, {overrides_ptr}, {overrides_count}}},\n')
        f.write('};\n\n')
        f.write('constexpr size_t kNumConfigs = sizeof(kCharConfigs) / sizeof(kCharConfigs[0]);\n\n')

        f.write('''const CharChatterConfig* findConfig(uint16_t classId) {
    for (size_t i = 0; i < kNumConfigs; ++i) {
        if (kCharConfigs[i].classId == classId) return &kCharConfigs[i];
    }
    return nullptr;
}

const CharChatterConfig* findConfigByName(const char* name) {
    if (!name) return nullptr;
    for (size_t i = 0; i < kNumConfigs; ++i) {
        if (!strcmp(kCharConfigs[i].name, name)) return &kCharConfigs[i];
    }
    return nullptr;
}

const ChatterSet* resolveChatter(const CharChatterConfig& cfg, uint32_t chatterType) {
    if (chatterType != 0 && cfg.overrides) {
        for (uint16_t i = 0; i < cfg.overrideCount; ++i) {
            if (cfg.overrides[i].holidayId == chatterType) {
                return cfg.overrides[i].chatter;
            }
        }
    }
    return cfg.defaultChatter;
}

bool substitute(char* dst, size_t dstCap, const char* tmpl, const char* targetName) {
    if (!dst || dstCap == 0 || !tmpl) return false;
    size_t outLen = 0;
    const char* sub = (targetName && targetName[0]) ? targetName : "";
    for (const char* p = tmpl; *p && outLen + 1 < dstCap; ) {
        if (*p == '%') {
            for (const char* s = sub; *s && outLen + 1 < dstCap; ++s) {
                dst[outLen++] = *s;
            }
            p++;
        } else {
            dst[outLen++] = *p++;
        }
    }
    dst[outLen] = '\\0';
    return true;
}

} // namespace

const char* getChatterRaw(uint16_t classId, uint32_t chatterType, uint32_t category, uint32_t msgIndex) {
    const CharChatterConfig* cfg = findConfig(classId);
    if (!cfg) return nullptr;
    const ChatterSet* set = resolveChatter(*cfg, chatterType);
    if (!set) return nullptr;
    if (category >= kCategoryCount) return nullptr;
    const ChatterCategory& cat = set->categories[category];
    if (msgIndex >= cat.count) return nullptr;
    return cat.phrases[msgIndex];
}

const char* getChatterRawByName(const char* charName, uint32_t chatterType, uint32_t category, uint32_t msgIndex) {
    const CharChatterConfig* cfg = findConfigByName(charName);
    if (!cfg) return nullptr;
    const ChatterSet* set = resolveChatter(*cfg, chatterType);
    if (!set) return nullptr;
    if (category >= kCategoryCount) return nullptr;
    const ChatterCategory& cat = set->categories[category];
    if (msgIndex >= cat.count) return nullptr;
    return cat.phrases[msgIndex];
}

bool formatChatter(char* dst, size_t dstCap, uint16_t classId, uint32_t chatterType, uint32_t category, uint32_t msgIndex, const char* targetName) {
    const char* tmpl = getChatterRaw(classId, chatterType, category, msgIndex);
    if (!tmpl) return false;
    return substitute(dst, dstCap, tmpl, targetName);
}

bool formatChatterByName(char* dst, size_t dstCap, const char* charName, uint32_t chatterType, uint32_t category, uint32_t msgIndex, const char* targetName) {
    const char* tmpl = getChatterRawByName(charName, chatterType, category, msgIndex);
    if (!tmpl) return false;
    return substitute(dst, dstCap, tmpl, targetName);
}

} // namespace tt::ui::chatter
''')


def main():
    repo_hint = sys.argv[1] if len(sys.argv) > 1 else None
    out_dir = sys.argv[2] if len(sys.argv) > 2 else 'source/ui'

    repo = find_repo(repo_hint)
    print(f"gen_cchar_chatter: reading from {repo}")
    sets_data, char_configs, holiday_names, tg = build_chatter_table(repo)
    print(f"gen_cchar_chatter: {len(sets_data)} unique chatter sets across {len(char_configs)} character groups")

    os.makedirs(out_dir, exist_ok=True)
    generate_header(out_dir)
    generate_cpp(out_dir, sets_data, char_configs)
    print(f"gen_cchar_chatter: wrote {out_dir}/cchar_chatter_data.h and {out_dir}/cchar_chatter_data.cpp")


if __name__ == '__main__':
    main()
