#!/usr/bin/env python3
"""Toon hand tracks (JNT1, as battle_toon_extras.py) for non-battle clips that hold a prop.

    .venv/bin/python -I -B tools/pipeline/bake_toon_hands.py --resources ref/resources \
        [--out sdmc-out/3ds/toontown3ds/data/toon] [--clips sit,swim,jump-idle]

Toon Slingshot (DistributedTargetGame) instances its umbrella on the Toon's right hands through
sit (SitStart), swim (fly) and jump-idle (jumpAirborne, fall). Writes
torso_<code>_<lod>_<clip>.jnt for every torso and LOD; existing files are left alone.
"""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import argparse
import bake_toon as toon
from battle_common import DATA, write_joints, mat34
from direct.actor.Actor import Actor
from direct.showbase.ShowBase import ShowBase


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, default=DATA / 'toon')
    ap.add_argument('--clips', default='sit,swim,jump-idle')
    args, _ = ap.parse_known_args()
    clips = args.clips.split(',')
    ShowBase()
    names = ['def_joint_left_hold', 'def_joint_right_hold']
    written = 0
    for code, prefix in toon.TORSOS.items():
        for lod in toon.LODS:
            paths = {c: toon.phase_dir(toon.ANIMS[c][1]) + prefix + toon.ANIMS[c][0] for c in clips}
            actor = Actor({'torso': toon.CHAR + prefix + lod}, {'torso': paths})
            part = actor.getPart('torso')
            joints = [part.find('**/' + n) for n in names]
            assert all(not j.isEmpty() for j in joints)
            for clip in clips:
                out = args.out / f'torso_{code}_{lod}_{clip}.jnt'
                if out.exists():
                    continue
                rate = actor.getFrameRate(clip)
                step = max(1, round(rate / 12))
                assert rate / step == 12
                frames = []
                for f in range(0, actor.getNumFrames(clip), step):
                    actor.pose(clip, f)
                    actor.update(force=True)
                    frames.append([mat34(j, part) for j in joints])
                write_joints(out, names, frames)
                written += 1
            actor.cleanup()
            actor.removeNode()
    print(f'{written} hand tracks written to {args.out}')


if __name__ == '__main__':
    main()
