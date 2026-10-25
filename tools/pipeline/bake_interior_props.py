#!/usr/bin/env python3
"""Bake the interior and street life assets (docs/design/interior_life.md).

Outputs, under <sd>/3ds/toontown3ds/data/ (default sd: sdmc-out):
  interiors/props/interiorfish.ttm, interiorfish_swim.tta   DistributedPetshopInterior fish
  interiors/props/name_star.ttm                             HQ leaderboard trophy star
  audio/gui_knock.tsnd, audio/tell_joke.tsnd                DistributedKnockKnockDoor SFX

Usage:
  .venv/bin/python -I tools/pipeline/bake_interior_props.py [--sd sdmc-out]
"""
import argparse
import math
import os
import sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

from panda3d.core import Filename, loadPrcFileData
loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\ntextures-power-2 none')
from direct.showbase.ShowBase import ShowBase
from direct.actor.Actor import Actor

import bake_audio
import extract
import tta
import ttm


def find_resources():
    if os.environ.get('TT_RES'):
        return os.environ['TT_RES']
    d = ROOT
    while True:
        cand = os.path.join(d, 'ref', 'resources')
        if (os.path.isdir(cand) and os.listdir(cand)) or os.path.dirname(d) == d:
            return cand
        cand = os.path.join(d, 'ref', 'ref', 'resources')
        if os.path.isdir(cand) and os.listdir(cand):
            return cand
        d = os.path.dirname(d)


def capture_actor(actor, encode=False):
    actor.update(force=True)
    ex = extract.Extractor(max_tex=128, encode_textures=encode)
    ex.add(actor, animated=True)
    batches = ex.batches()
    return ex, batches


def get_positions(batches, reference):
    assert len(batches) == len(reference), "batch count mismatch"
    for b, r in zip(batches, reference):
        assert b.verts.shape == r.verts.shape, "vertex count mismatch"
    return np.concatenate([b.verts[:, :3] for b in batches])


def bake_animated_actor(base, model_path, anim_path, clip_name, out_ttm, out_tta, target_fps=12.0):
    actor = Actor(model_path, {clip_name: anim_path})
    actor.pose(clip_name, 0)
    ex, ref_batches = capture_actor(actor, encode=True)

    os.makedirs(os.path.dirname(os.path.abspath(out_ttm)), exist_ok=True)
    stats = ttm.write(out_ttm, ex.textures, ref_batches)
    print(f"Baked model {out_ttm}: {stats['file_bytes']} bytes, {stats['triangles']} tris")

    ctrl = actor.getAnimControl(clip_name)
    count = ctrl.getNumFrames()
    src_rate = ctrl.getFrameRate()
    assert count > 0 and src_rate > 0

    nf = max(1, math.ceil(count / src_rate * target_fps - 1e-8))
    frame_cache = {}

    def get_frame(idx):
        if idx not in frame_cache:
            actor.pose(clip_name, idx)
            _, batches = capture_actor(actor, encode=False)
            frame_cache[idx] = get_positions(batches, ref_batches)
        return frame_cache[idx]

    frames = []
    for i in range(nf):
        src = i * src_rate / target_fps
        a = min(int(src), count - 1)
        b = min(a + 1, count - 1)
        frac = src - a
        pos_a = get_frame(a)
        pos_b = get_frame(b)
        frames.append(pos_a * (1.0 - frac) + pos_b * frac)

    os.makedirs(os.path.dirname(os.path.abspath(out_tta)), exist_ok=True)
    tta_stats = tta.write(out_tta, np.array(frames), float(target_fps))
    print(f"Baked anim {out_tta}: {tta_stats['bytes']} bytes, {tta_stats['frames']} frames @ {target_fps} fps")

    actor.cleanup()
    actor.removeNode()


def bake_static_model(base, model_path, out_ttm):
    m = base.loader.loadModel(Filename.fromOsSpecific(model_path))
    ex = extract.Extractor(max_tex=128)
    ex.add(m)
    os.makedirs(os.path.dirname(os.path.abspath(out_ttm)), exist_ok=True)
    stats = ttm.write(out_ttm, ex.textures, ex.batches())
    print(f"Baked static model {out_ttm}: {stats['file_bytes']} bytes, {stats['triangles']} tris")


def bake_all(base, res, sd_root):
    """Bake every asset into sd_root/3ds/toontown3ds/data; returns the written paths."""
    data = os.path.join(sd_root, '3ds', 'toontown3ds', 'data')
    props = os.path.join(data, 'interiors', 'props')
    audio = os.path.join(data, 'audio')
    os.makedirs(props, exist_ok=True)
    os.makedirs(audio, exist_ok=True)

    # 1. Pet shop fish (DistributedPetshopInterior.setup: interiorfish-zero + swim)
    fish_mod = os.path.join(res, 'phase_4', 'models', 'props', 'interiorfish-zero.bam')
    fish_anim = os.path.join(res, 'phase_4', 'models', 'props', 'interiorfish-swim.bam')
    fish_ttm = os.path.join(props, 'interiorfish.ttm')
    fish_tta = os.path.join(props, 'interiorfish_swim.tta')
    bake_animated_actor(base, fish_mod, fish_anim, 'swim', fish_ttm, fish_tta, target_fps=12.0)

    # 2. Leaderboard trophy star (DistributedHQInterior.buildTrophy)
    star_mod = os.path.join(res, 'phase_3.5', 'models', 'gui', 'name_star.bam')
    star_ttm = os.path.join(props, 'name_star.ttm')
    bake_static_model(base, star_mod, star_ttm)

    # 3. Knock-knock SFX
    sfx_knock_src = os.path.join(res, 'phase_5', 'audio', 'sfx', 'GUI_knock_3.ogg')
    sfx_joke_src = os.path.join(res, 'phase_5', 'audio', 'sfx', 'AA_heal_telljoke.ogg')
    sfx_knock_out = os.path.join(audio, 'gui_knock.tsnd')
    sfx_joke_out = os.path.join(audio, 'tell_joke.tsnd')
    bake_audio.bake_sfx_adpcm(sfx_knock_src, sfx_knock_out, False)
    # DistributedKnockKnockDoor.knockKnockTrack: SoundInterval(self.rimshot, startTime=2.0)
    bake_audio.bake_sfx_adpcm(sfx_joke_src, sfx_joke_out, False, start_seconds=2.0)
    return [fish_ttm, fish_tta, star_ttm, sfx_knock_out, sfx_joke_out]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sd', default=os.path.join(ROOT, 'sdmc-out'))
    args = ap.parse_args()
    res = find_resources()
    print(f"Using resources at: {res}")
    base = ShowBase()
    for path in bake_all(base, res, args.sd):
        print(f"{path}: {os.path.getsize(path)} bytes")


if __name__ == '__main__':
    main()
