#!/usr/bin/env python3
"""Regression checks for generated gag potency data.

The runtime uses integer arithmetic, while Open Toontown's getAvPropDamage uses
floating point plus floor.  Check every possible track-XP value so a generator
change cannot make the battle picker disagree with the unmodified AI.
"""

import math
import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GLOBALS = ROOT / "ref/open-toontown/toontown/toonbase/ToontownBattleGlobals.py"
SPEC = importlib.util.spec_from_file_location("gen_gag_tables", ROOT / "tools/gen_gag_tables.py")
gen_gag_tables = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gen_gag_tables)


def stock(entry, exp):
    (min_damage, max_damage), (min_exp, max_exp) = entry
    bounded = min(exp, max_exp)
    exp_per_hp = float(max_exp - min_exp + 1) / float(max_damage - min_damage + 1)
    damage = math.floor((bounded - min_exp) / exp_per_hp) + min_damage
    return min_damage if damage <= 0 else damage


def runtime(entry, exp):
    (min_damage, max_damage), (min_exp, max_exp) = entry
    bounded = min(exp, max_exp)
    damage = ((bounded - min_exp) * (max_damage - min_damage + 1) //
              (max_exp - min_exp + 1) + min_damage)
    return min_damage if damage <= 0 else damage


def main():
    values = gen_gag_tables.assignments(
        GLOBALS, {"Levels", "AvPropDamage", "UberSkill", "regMaxSkill", "MaxSkill"}
    )
    env = {}
    for name in ("UberSkill", "regMaxSkill", "Levels", "MaxSkill"):
        env[name] = gen_gag_tables.literal(values[name], env)
    damage = gen_gag_tables.literal(values["AvPropDamage"], env)
    checks = 0
    for track in damage:
        for entry in track:
            for exp in range(10501):
                assert runtime(entry, exp) == stock(entry, exp), (entry, exp)
                checks += 1
    print(f"gag data: {checks} stock potency values match")


if __name__ == "__main__":
    main()
