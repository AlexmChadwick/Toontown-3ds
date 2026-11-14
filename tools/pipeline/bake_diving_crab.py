#!/usr/bin/env python3
"""Treasure Dive's King Crab (DistributedDivingGame.setGameReady: Actor kingCrab-zero with
kingCrab-swimLOOP as 'anim', looped) -> data/minigames/diving_crab_full.ttm + diving_crab_full_anim.tta.

setGameReady scales each crab (1, 0.3, 1) and places it at z -40; the runtime applies that.
Needs ref/ with resources (battle_common.REF):
    .venv/bin/python -I tools/pipeline/bake_diving_crab.py [--out DIR]
"""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import argparse
from battle_common import DATA, bake_actor
from direct.showbase.ShowBase import ShowBase


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', type=Path, default=DATA / 'minigames')
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    base = ShowBase()
    info = bake_actor(base, args.out, 'diving_crab', 'phase_4/models/char/kingCrab-zero',
                      {'anim': 'phase_4/models/char/kingCrab-swimLOOP'}, {'full': 1500}, max_tex=128)
    print(info)


if __name__ == '__main__':
    main()
