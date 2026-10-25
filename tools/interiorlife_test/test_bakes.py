#!/usr/bin/env python3
"""Host test: verify that all interior life bakes are deterministic and byte-identical.

Tests:
- interiorfish.ttm
- interiorfish_swim.tta
- periscope.ttm
- periscope_anim.tta
- name_star.ttm
- audio/gui_knock.tsnd
- audio/tell_joke.tsnd
"""
import hashlib
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PIPELINE = os.path.join(ROOT, 'tools', 'pipeline')
if PIPELINE not in sys.path:
    sys.path.insert(0, PIPELINE)

from direct.showbase.ShowBase import ShowBase
import bake_audio
import bake_interior_props


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def bake_all_to_dir(target_dir, base, res):
    os.makedirs(target_dir, exist_ok=True)
    os.makedirs(os.path.join(target_dir, 'audio'), exist_ok=True)

    # 1. Pet Shop fish
    fish_mod = os.path.join(res, 'phase_4', 'models', 'props', 'interiorfish-zero.bam')
    fish_anim = os.path.join(res, 'phase_4', 'models', 'props', 'interiorfish-swim.bam')
    fish_ttm = os.path.join(target_dir, 'interiorfish.ttm')
    fish_tta = os.path.join(target_dir, 'interiorfish_swim.tta')
    bake_interior_props.bake_animated_actor(base, fish_mod, fish_anim, 'swim', fish_ttm, fish_tta, target_fps=12.0)

    # 2. HQ periscope
    peri_mod = os.path.join(res, 'phase_3.5', 'models', 'props', 'HQ_periscope-mod.bam')
    peri_anim = os.path.join(res, 'phase_3.5', 'models', 'props', 'HQ_periscope-chan.bam')
    peri_ttm = os.path.join(target_dir, 'periscope.ttm')
    peri_tta = os.path.join(target_dir, 'periscope_anim.tta')
    bake_interior_props.bake_animated_actor(base, peri_mod, peri_anim, 'anim', peri_ttm, peri_tta, target_fps=12.0)

    # 3. Leaderboard trophy star
    star_mod = os.path.join(res, 'phase_3.5', 'models', 'gui', 'name_star.bam')
    star_ttm = os.path.join(target_dir, 'name_star.ttm')
    bake_interior_props.bake_static_model(base, star_mod, star_ttm)

    # 4. Knock-knock SFX
    sfx_knock_src = os.path.join(res, 'phase_5', 'audio', 'sfx', 'GUI_knock_3.ogg')
    sfx_joke_src = os.path.join(res, 'phase_5', 'audio', 'sfx', 'AA_heal_telljoke.ogg')
    sfx_knock_out = os.path.join(target_dir, 'audio', 'gui_knock.tsnd')
    sfx_joke_out = os.path.join(target_dir, 'audio', 'tell_joke.tsnd')
    bake_audio.bake_sfx_adpcm(sfx_knock_src, sfx_knock_out, False)
    bake_audio.bake_sfx_adpcm(sfx_joke_src, sfx_joke_out, False)


def main():
    print("Testing deterministic bakes for interior life assets...")
    base = ShowBase()
    res = bake_interior_props.find_resources()
    print(f"Resources: {res}")

    dir1 = tempfile.mkdtemp(prefix='bake_test_1_')
    dir2 = tempfile.mkdtemp(prefix='bake_test_2_')

    try:
        print("Running bake pass 1...")
        bake_all_to_dir(dir1, base, res)

        print("Running bake pass 2...")
        bake_all_to_dir(dir2, base, res)

        files = [
            'interiorfish.ttm',
            'interiorfish_swim.tta',
            'periscope.ttm',
            'periscope_anim.tta',
            'name_star.ttm',
            os.path.join('audio', 'gui_knock.tsnd'),
            os.path.join('audio', 'tell_joke.tsnd'),
        ]

        all_match = True
        for rel in files:
            p1 = os.path.join(dir1, rel)
            p2 = os.path.join(dir2, rel)
            h1 = sha256_file(p1)
            h2 = sha256_file(p2)
            sz = os.path.getsize(p1)
            if h1 != h2:
                print(f"FAIL: {rel} mismatch! ({h1} != {h2})")
                all_match = False
            else:
                print(f"PASS: {rel} ({sz} bytes, {h1[:16]}...) byte-identical.")

        if not all_match:
            sys.exit(1)
        print("ALL BAKE DETERMINISM TESTS PASSED.")
    finally:
        shutil.rmtree(dir1, ignore_errors=True)
        shutil.rmtree(dir2, ignore_errors=True)


if __name__ == '__main__':
    main()
