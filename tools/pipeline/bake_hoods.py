#!/usr/bin/env python3
"""Bake every playground besides TTC into the SD data set.

For each hood: <zone>.ttm (+ .pts anchors) via bake_zone.py, <zone>.ttc via
bake_collision.py, and its playground music <zone>.tsnd. Output:
sdmc-out/3ds/toontown3ds/data/{zones,audio}/ (gitignored; copy to the SD card).
The runtime resolves these by zone id (source/world/place.cpp).

    .venv/bin/python -I tools/pipeline/bake_hoods.py [--only 1000]
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
RES = os.path.join(ROOT, 'ref', 'resources')
OUT = os.path.join(ROOT, 'sdmc-out', '3ds', 'toontown3ds', 'data')
sys.path.insert(0, HERE)

# zone id, hood code, playground DNA, music (toontown/safezone/*SafeZoneLoader.py musicFile).
HOODS = [
    (1000, 'DD', 'phase_6/dna/donalds_dock_sz.dna', 'phase_6/audio/bgm/DD_nbrhood.ogg'),
    (3000, 'BR', 'phase_8/dna/the_burrrgh_sz.dna', 'phase_8/audio/bgm/TB_nbrhood.ogg'),
    (4000, 'MM', 'phase_6/dna/minnies_melody_land_sz.dna', 'phase_6/audio/bgm/MM_nbrhood.ogg'),
    (5000, 'DG', 'phase_8/dna/daisys_garden_sz.dna', 'phase_8/audio/bgm/DG_nbrhood.ogg'),
    (6000, 'OZ', 'phase_6/dna/outdoor_zone_sz.dna', 'phase_6/audio/bgm/OZ_SZ.ogg'),
    (8000, 'GS', 'phase_6/dna/goofy_speedway_sz.dna', 'phase_6/audio/bgm/GS_SZ.ogg'),
    (9000, 'DL', 'phase_8/dna/donalds_dreamland_sz.dna', 'phase_8/audio/bgm/DL_nbrhood.ogg'),
]


def storage_for(code, sz_dna):
    phase = sz_dna.split('/')[0]
    return [os.path.join(RES, 'phase_4/dna/storage.dna'),
            os.path.join(RES, phase, 'dna', f'storage_{code}.dna'),
            os.path.join(RES, phase, 'dna', f'storage_{code}_sz.dna')]


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout[-2000:] + r.stderr[-2000:])
        raise SystemExit(f'failed: {" ".join(cmd[:4])} ...')
    return r.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', type=int)
    ap.add_argument('--no-audio', action='store_true')
    args = ap.parse_args()
    zones_dir, audio_dir = os.path.join(OUT, 'zones'), os.path.join(OUT, 'audio')
    os.makedirs(zones_dir, exist_ok=True)
    os.makedirs(audio_dir, exist_ok=True)
    py = [sys.executable, '-I']
    for zone, code, sz, music in HOODS:
        if args.only and args.only != zone:
            continue
        t0 = time.time()
        storage = storage_for(code, sz)
        ttm = os.path.join(zones_dir, f'{zone}.ttm')
        run(py + [os.path.join(HERE, 'bake_zone.py'), *storage, '--zone', os.path.join(RES, sz), '--out', ttm])
        run(py + [os.path.join(HERE, 'bake_collision.py'), os.path.join(RES, sz), os.path.join(zones_dir, f'{zone}.ttc'),
                  '--storage', *storage])
        if not args.no_audio:
            import bake_audio
            bake_audio.bake_music(os.path.join(RES, music), os.path.join(audio_dir, f'{zone}.tsnd'), True)
        print(f'{zone} {code}: {os.path.getsize(ttm) // 1024} KB model, '
              f'{os.path.getsize(os.path.join(zones_dir, f"{zone}.ttc")) // 1024} KB collision '
              f'({time.time() - t0:.0f} s)', flush=True)


if __name__ == '__main__':
    main()
