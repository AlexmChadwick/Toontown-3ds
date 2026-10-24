#!/usr/bin/env python3
"""Generate source/world/fish_tables.{h,cpp} from upstream Open Toontown.

    .venv/bin/python -I tools/gen_fish_tables.py ref/open-toontown source/world

Sources (read with ast / per-statement exec, never imported: toontown.* needs panda3d.otp):
  toontown/fishing/FishGlobals.py        __fishDict, __rodDict, RodRarityFactor, Rod2JellybeanDict,
                                         ProbabilityDict, value constants; the pond table is rebuilt
                                         with the same algorithm as FishGlobals' module-level loop
  toontown/fishing/FishingTargetGlobals.py   water level per pond area
  toontown/toonbase/ToontownGlobals.py   hood / street zone ids, HoodHierarchy
  toontown/toonbase/TTLocalizerEnglish.py    FishGenusNames, FishSpeciesNames

Output is deterministic and has no heap use at runtime. See docs/design/fishing.md.
"""
import ast
import copy
import math
import os
import sys

ANYWHERE = 1


def top_level_assigns(path):
    """name -> ast.Assign value node for every simple top-level `NAME = expr`."""
    tree = ast.parse(open(path).read())
    out = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            out[node.targets[0].id] = node
    return out


def eval_node(node, env):
    expr = ast.Expression(body=node.value)
    ast.fix_missing_locations(expr)
    return eval(compile(expr, '<upstream>', 'eval'), {'__builtins__': {}}, env)


def load_globals(upstream):
    """ToontownGlobals constants (ints) + HoodHierarchy, evaluated statement by statement."""
    path = os.path.join(upstream, 'toontown', 'toonbase', 'ToontownGlobals.py')
    env = {}
    for name, node in top_level_assigns(path).items():
        try:
            env[name] = eval_node(node, env)
        except Exception:  # noqa: BLE001 - panda3d / localizer dependent assignments
            pass
    return env


def load_fish(upstream, tg):
    path = os.path.join(upstream, 'toontown', 'fishing', 'FishGlobals.py')
    env = {'TTG': type('TTG', (), tg), 'Anywhere': ANYWHERE}
    for name, node in top_level_assigns(path).items():
        try:
            env[name] = eval_node(node, env)
        except Exception:  # noqa: BLE001 - localizer dependent tables (trophies)
            pass
    for need in ('__fishDict', '__rodDict', 'RodRarityFactor', 'Rod2JellybeanDict', 'ProbabilityDict',
                 'GlobalRarityDialBase', 'OVERALL_VALUE_SCALE', 'RARITY_VALUE_SCALE', 'WEIGHT_VALUE_SCALE',
                 'MAX_RARITY', 'FishingAngleMax', 'CastTimeout'):
        if need not in env:
            sys.exit(f'FishGlobals.{need} could not be evaluated')
    return env


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    upstream, outdir = sys.argv[1], sys.argv[2]
    tg = load_globals(upstream)
    F = load_fish(upstream, tg)
    fish, rods = F['__fishDict'], F['__rodDict']
    hier = tg['HoodHierarchy']
    max_rarity = F['MAX_RARITY']

    def can_catch(g, s, rod):
        lo, hi = fish[g][s][0], fish[g][s][1]
        rlo, rhi = rods[rod][0], rods[rod][1]
        return rlo <= hi and rhi >= lo

    empty = {rod: {} for rod in rods}
    anywhere = copy.deepcopy(empty)
    ponds = {}
    for g, species_list in fish.items():
        for s, desc in enumerate(species_list):
            rarity, zone_list = desc[2], desc[3]
            for zone_index, zone in enumerate(zone_list):
                eff = min(max_rarity, rarity + zone_index)
                if zone == ANYWHERE:
                    for rod, rd in anywhere.items():
                        if can_catch(g, s, rod):
                            rd.setdefault(eff, []).append((g, s))
                else:
                    for pz in [zone] + list(hier.get(zone, ())):
                        rod_dict = ponds.setdefault(pz, copy.deepcopy(empty))
                        for rod, rd in rod_dict.items():
                            if can_catch(g, s, rod):
                                rd.setdefault(eff, []).append((g, s))
    for zone, rod_dict in ponds.items():
        for rod, any_rd in anywhere.items():
            for rarity, lst in any_rd.items():
                rod_dict[rod].setdefault(rarity, []).extend(lst)
    ponds[0] = anywhere  # area 0: pond with no species of its own (fallback)

    # ---- names ----
    loc = top_level_assigns(os.path.join(upstream, 'toontown', 'toonbase', 'TTLocalizerEnglish.py'))
    genus_names = eval_node(loc['FishGenusNames'], {})
    species_names = eval_node(loc['FishSpeciesNames'], {})

    # ---- water level per area (FishingTargetGlobals.__targetInfoDict) ----
    ftg = top_level_assigns(os.path.join(upstream, 'toontown', 'fishing', 'FishingTargetGlobals.py'))
    target_info = eval_node(ftg['__targetInfoDict'], {'ToontownGlobals': type('G', (), tg)})
    water = sorted((int(z), float(info[5])) for z, info in target_info.items())
    # Where the fish swim: centre (x, y) and radius (FishingTargetGlobals.getTargetCenter/Radius).
    pond_info = sorted((int(z), float(info[1]), float(info[2]), float(info[4])) for z, info in target_info.items())

    # ---- emit ----
    genera = sorted(fish)
    max_genus = max(genera)
    flat = []  # (genus, species, minLb, maxLb, rarity, name)
    for g in genera:
        for s, desc in enumerate(fish[g]):
            flat.append((g, s, desc[0], desc[1], desc[2], species_names[g][s]))

    rows = []
    pool = []
    for area in sorted(ponds):
        for rod in sorted(rods):
            for rarity in range(1, max_rarity + 1):
                lst = ponds[area][rod].get(rarity)
                if not lst:
                    continue
                rows.append((area, rod, rarity, len(pool), len(lst)))
                pool.extend(lst)
    assert len(pool) < 65536

    h = ['// GENERATED by tools/gen_fish_tables.py -- do not edit.',
         '// Source: toontown/fishing/FishGlobals.py, FishingTargetGlobals.py, TTLocalizerEnglish.py',
         '// (docs/design/fishing.md section 6).',
         '#pragma once', '', '#include <cstdint>', '', 'namespace tt::world::fish {', '',
         f'constexpr int kNumSpecies = {len(flat)};',
         f'constexpr int kMaxGenus = {max_genus};',
         f'constexpr int kNumRods = {len(rods)};',
         f'constexpr int kMaxRarity = {max_rarity};',
         f'constexpr float kCastTimeout = {F["CastTimeout"]}f;   // FishGlobals.CastTimeout',
         f'constexpr float kFishingAngleMax = {F["FishingAngleMax"]}f; // FishGlobals.FishingAngleMax',
         f'constexpr float kGlobalRarityDialBase = {F["GlobalRarityDialBase"]}f;',
         f'constexpr float kOverallValueScale = {float(F["OVERALL_VALUE_SCALE"])}f;',
         f'constexpr float kRarityValueScale = {float(F["RARITY_VALUE_SCALE"])}f;',
         f'constexpr float kWeightValueScale = {F["WEIGHT_VALUE_SCALE"]!r}f;', '',
         '// FishGlobals.ProbabilityDict: roll 1..100; <= kFishCutoff fish, == kBeanCutoff jellybeans, else boot.',
         ]
    cut = sorted(F['ProbabilityDict'].items())
    for k, v in cut:
        # ProbabilityDict values are ints (FishItem = 2, JellybeanItem = 3, BootItem = 4)
        label = {2: 'kFishCutoff', 3: 'kBeanCutoff', 4: 'kBootCutoff'}[v]
        h.append(f'constexpr int {label} = {k};')
    h += ['',
          'struct Species {',
          '    uint8_t genus, species;',
          '    uint8_t minLb, maxLb; // FishGlobals.getWeightRange (pounds)',
          '    uint8_t rarity;       // 1..10',
          '    const char* name;',
          '};',
          'extern const Species kSpecies[kNumSpecies];',
          '// Genus display names, indexed by genus id (null where the id is unused).',
          'extern const char* const kGenusNames[kMaxGenus + 1];', '',
          'struct Rod {',
          '    uint8_t minLb, maxLb;   // __rodDict weight range (pounds)',
          '    uint8_t castCost;       // jellybeans per cast',
          '    uint8_t beans;          // Rod2JellybeanDict (a jellybean catch)',
          '    float rarityExp;        // RodRarityFactor',
          '};',
          'extern const Rod kRods[kNumRods];', '',
          '// Pond catch lists: rows sorted by (area, rod, rarity); each lists (genus, species) pairs',
          '// from kPool. Area 0 holds the species that live "anywhere" (fallback for unknown ponds).',
          'struct PondRow {',
          '    uint16_t area;',
          '    uint8_t rod, rarity;',
          '    uint16_t first, count;',
          '};',
          'extern const PondRow kPondRows[];',
          'extern const int kNumPondRows;',
          'extern const uint8_t kPool[][2];', '',
          '// FishingTargetGlobals water level (relative to the fishing spot origin), per pond area.',
          'struct Water {',
          '    uint16_t area;',
          '    float level;',
          '};',
          'extern const Water kWater[];',
          'extern const int kNumWater;', '',
          '// Fish target region per pond area: centre (x, y) in zone space and radius.',
          'struct PondArea {',
          '    uint16_t area;',
          '    float cx, cy, radius;',
          '};',
          'extern const PondArea kPondAreas[];',
          'extern const int kNumPondAreas;', '',
          '} // namespace tt::world::fish', '']
    c = ['// GENERATED by tools/gen_fish_tables.py -- do not edit.', '#include "world/fish_tables.h"', '',
         'namespace tt::world::fish {', '', 'const Species kSpecies[kNumSpecies] = {']
    for g, s, lo, hi, r, name in flat:
        c.append('    {%d, %d, %d, %d, %d, "%s"},' % (g, s, lo, hi, r, name.replace('"', '\\"')))
    c += ['};', '', 'const char* const kGenusNames[kMaxGenus + 1] = {']
    for g in range(max_genus + 1):
        c.append('    %s,' % ('"%s"' % genus_names[g].replace('"', '\\"') if g in genus_names else 'nullptr'))
    c += ['};', '', 'const Rod kRods[kNumRods] = {']
    for rod in sorted(rods):
        lo, hi, cost = rods[rod]
        c.append('    {%d, %d, %d, %d, %.8ff},' % (lo, hi, cost, F['Rod2JellybeanDict'][rod], F['RodRarityFactor'][rod]))
    c += ['};', '', 'const PondRow kPondRows[] = {']
    for area, rod, rarity, first, count in rows:
        c.append('    {%d, %d, %d, %d, %d},' % (area, rod, rarity, first, count))
    c += ['};', f'const int kNumPondRows = {len(rows)};', '', 'const uint8_t kPool[][2] = {']
    for i in range(0, len(pool), 8):
        c.append('    ' + ' '.join('{%d, %d},' % p for p in pool[i:i + 8]))
    c += ['};', '', 'const Water kWater[] = {']
    for area, level in water:
        c.append('    {%d, %.4ff},' % (area, level))
    c += ['};', f'const int kNumWater = {len(water)};', '', 'const PondArea kPondAreas[] = {']
    for area, cx, cy, rad in pond_info:
        c.append('    {%d, %.2ff, %.2ff, %.2ff},' % (area, cx, cy, rad))
    c += ['};', f'const int kNumPondAreas = {len(pond_info)};', '', '} // namespace tt::world::fish', '']
    with open(os.path.join(outdir, 'fish_tables.h'), 'w') as f:
        f.write('\n'.join(h))
    with open(os.path.join(outdir, 'fish_tables.cpp'), 'w') as f:
        f.write('\n'.join(c))
    print(f'{len(flat)} species, {len(rows)} pond rows, {len(pool)} pool entries, {len(water)} water levels',
          file=sys.stderr)


if __name__ == '__main__':
    main()
