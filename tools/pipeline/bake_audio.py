"""Bake upstream .ogg audio into .tsnd (see audio_tsnd.py for the format).

    .venv/bin/python -I tools/pipeline/bake_audio.py [--out romfs/baked/audio]

Music  -> DSP-ADPCM, stereo, 32000 Hz (the DSP mixes at ~32728 Hz, so higher rates
          only cost bytes), streamed in 14336-sample blocks. ~4x smaller than PCM16.
SFX    -> PCM16 mono 22050 Hz, one block, loaded fully into linear memory.

Upstream sources (ref/open-toontown):
  TC_nbrhood.ogg          toontown/safezone/TTSafeZoneLoader.py  musicFile (TTC playground)
  AV_footstep_walkloop    toontown/toon/LocalToon.py soundWalk (looped while walking)
  AV_footstep_runloop     toontown/toon/LocalToon.py soundRun  (looped while running)
  GUI_create_toon_fwd     toontown/toonbase/ToontownStart.py default DirectGui click
  GUI_rollover            toontown/toonbase/ToontownStart.py default DirectGui rollover
  GUI_create_toon_back    "back" button click (MakeAToon etc.)
  GUI_balloon_popup       popup / dialog open
Upstream has no jump sound for toons (LocalAvatar plays none), so none is baked.

Decoding uses ffmpeg (found on PATH or /usr/local/bin). Output is deterministic for
a given ffmpeg build. The bake fails if the total exceeds BUDGET_BYTES.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_dspadpcm as adp  # noqa: E402
import audio_tsnd as tsnd  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RES = os.path.join(ROOT, 'ref', 'resources')
BUDGET_BYTES = 3 * 1024 * 1024
MUSIC_RATE = 32000
SFX_RATE = 22050
MUSIC_BLOCK_SAMPLES = 1024 * adp.SPF  # 14336 samples = 8 KB per channel per block

# (output name, upstream path, kind, loops)
ASSETS = [
    ('ttc_nbrhood', 'phase_4/audio/bgm/TC_nbrhood.ogg', 'music', True),
    ('footstep_walk', 'phase_3.5/audio/sfx/AV_footstep_walkloop.ogg', 'sfx', True),
    ('footstep_run', 'phase_3.5/audio/sfx/AV_footstep_runloop.ogg', 'sfx', True),
    ('gui_click', 'phase_3/audio/sfx/GUI_create_toon_fwd.ogg', 'sfx', False),
    # DistributedTTTreasure.grabSoundPath (toontown/safezone/DistributedTTTreasure.py).
    ('treasure_grab', 'phase_4/audio/sfx/SZ_DD_treasure.ogg', 'sfx', False),
    ('gui_rollover', 'phase_3/audio/sfx/GUI_rollover.ogg', 'sfx', False),
    ('gui_back', 'phase_3/audio/sfx/GUI_create_toon_back.ogg', 'sfx', False),
    ('gui_popup', 'phase_3/audio/sfx/GUI_balloon_popup.ogg', 'sfx', False),
]


# Battle assets are kept separate from the RomFS boot-audio budget. The battle
# driver streams music and writes these to SD, never to the boot directory.
BATTLE_ASSETS = [
    ('encntr_general_bg', 'phase_3.5/audio/bgm/encntr_general_bg.ogg', 'music', True),
    ('encntr_suit_winning', 'phase_9/audio/bgm/encntr_suit_winning.ogg', 'music', True),
    ('pie_throw', 'phase_3.5/audio/sfx/AA_pie_throw_only.ogg', 'sfx', False),
    ('throw_tart_hit', 'phase_3.5/audio/sfx/AA_tart_only.ogg', 'sfx', False),
    ('throw_slice_hit', 'phase_5/audio/sfx/AA_slice_only.ogg', 'sfx', False),
    ('throw_miss', 'phase_5/audio/sfx/General_throw_miss.ogg', 'sfx', False),
    ('squirt_flower_hit', 'phase_3.5/audio/sfx/AA_squirt_flowersquirt.ogg', 'sfx', False),
    ('squirt_flower_miss', 'phase_5/audio/sfx/AA_squirt_flowersquirt_miss.ogg', 'sfx', False),
    ('squirt_glass_hit', 'phase_5/audio/sfx/AA_squirt_glasswater.ogg', 'sfx', False),
    ('squirt_glass_miss', 'phase_5/audio/sfx/AA_squirt_glasswater_miss.ogg', 'sfx', False),
    ('squirt_gun_hit', 'phase_5/audio/sfx/AA_squirt_neonwatergun.ogg', 'sfx', False),
    ('squirt_gun_miss', 'phase_5/audio/sfx/AA_squirt_neonwatergun_miss.ogg', 'sfx', False),
    ('SA_hangup', 'phase_3.5/audio/sfx/SA_hangup.ogg', 'sfx', False),
    ('Cog_Death', 'phase_3.5/audio/sfx/Cog_Death.ogg', 'sfx', False),
    ('ENC_propeller_in', 'phase_5/audio/sfx/ENC_propeller_in.ogg', 'sfx', False),
    ('ENC_propeller_out', 'phase_5/audio/sfx/ENC_propeller_out.ogg', 'sfx', False),
    ('ENC_cogfall_apart', 'phase_3.5/audio/sfx/ENC_cogfall_apart.ogg', 'sfx', False),
    ('toon_hit', 'phase_5/audio/sfx/tt_s_ara_cfg_toonHit.ogg', 'sfx', False),
    ('toon_bodyfall', 'phase_5/audio/sfx/Toon_bodyfall_synergy.ogg', 'sfx', False),
] + [(f'toon_{species}_ouch', f'phase_3.5/audio/dial/AV_{species}_howl.ogg', 'sfx', False)
     for species in ('dog', 'cat', 'horse', 'rabbit', 'mouse', 'duck', 'monkey', 'bear', 'pig')]


def find_ffmpeg():
    p = shutil.which('ffmpeg') or ('/usr/local/bin/ffmpeg' if os.path.exists('/usr/local/bin/ffmpeg') else None)
    if not p:
        sys.exit('bake_audio: ffmpeg not found (brew install ffmpeg)')
    return p


def decode_ogg(path, rate, channels):
    """Returns int16 array (channels, N)."""
    cmd = [find_ffmpeg(), '-v', 'error', '-nostdin', '-i', path, '-map', '0:a:0',
           '-af', 'aresample=filter_size=64:cutoff=0.97',
           '-ar', str(rate), '-ac', str(channels), '-f', 's16le', '-acodec', 'pcm_s16le', '-']
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    pcm = np.frombuffer(raw, dtype='<i2')
    return pcm.reshape(-1, channels).T.copy()


def bake_music(src, dst, loops):
    pcm = decode_ogg(src, MUSIC_RATE, 2)
    n = pcm.shape[1]
    coefs = [adp.compute_coefs(pcm[c]) for c in range(2)]
    data, decoded, headers, h0, h1 = adp.encode([pcm[0], pcm[1]], coefs)
    fpb = MUSIC_BLOCK_SAMPLES // adp.SPF
    nblocks = (n + MUSIC_BLOCK_SAMPLES - 1) // MUSIC_BLOCK_SAMPLES
    block_ctx = [[(int(headers[c, b * fpb]), int(h0[c, b * fpb]), int(h1[c, b * fpb]))
                  for b in range(nblocks)] for c in range(2)]
    loop_ctx = [(int(headers[c, 0]), int(h0[c, 0]), int(h1[c, 0])) for c in range(2)]
    size = tsnd.write(dst, encoding=tsnd.ENC_ADPCM, sample_rate=MUSIC_RATE, num_samples=n,
                      channel_data=data, loop=loops, loop_start=0, loop_end=n,
                      block_samples=MUSIC_BLOCK_SAMPLES, coefs=coefs,
                      block_ctx=block_ctx, loop_ctx=loop_ctx)
    return size, n, MUSIC_RATE, 2, pcm, decoded


def bake_sfx(src, dst, loops):
    pcm = decode_ogg(src, SFX_RATE, 1)
    n = pcm.shape[1]
    size = tsnd.write(dst, encoding=tsnd.ENC_PCM16, sample_rate=SFX_RATE, num_samples=n,
                      channel_data=[tsnd.pcm16_bytes(pcm[0])], loop=loops,
                      loop_start=0, loop_end=n)
    return size, n, SFX_RATE, 1, pcm, pcm


def snr_db(ref, test):
    ref = ref.astype(np.float64)
    err = ref - test.astype(np.float64)
    pe = (err * err).sum()
    return float('inf') if pe == 0 else 10 * np.log10((ref * ref).sum() / pe)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(ROOT, 'romfs', 'baked', 'audio'))
    ap.add_argument('--only', help='bake only this output name')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    total = 0
    print(f'{"asset":16} {"enc":6} {"ch":>2} {"rate":>6} {"secs":>6} {"ogg KB":>7} {"tsnd KB":>8} {"SNR dB":>7}')
    for name, rel, kind, loops in ASSETS:
        dst = os.path.join(args.out, name + '.tsnd')
        if args.only and args.only != name:
            if os.path.exists(dst):
                total += os.path.getsize(dst)
            continue
        src = os.path.join(RES, rel)
        t0 = time.time()
        if kind == 'music':
            size, n, rate, ch, ref, dec = bake_music(src, dst, loops)
            snr = min(snr_db(ref[c], dec[c]) for c in range(ch))
            enc = 'ADPCM'
        else:
            size, n, rate, ch, ref, dec = bake_sfx(src, dst, loops)
            snr = float('inf')
            enc = 'PCM16'
        total += size
        print(f'{name:16} {enc:6} {ch:2} {rate:6} {n / rate:6.2f} {os.path.getsize(src) / 1024:7.1f} '
              f'{size / 1024:8.1f} {snr:7.1f}   ({time.time() - t0:.1f}s) <- {rel}')
    print(f'total {total / 1024:.1f} KB  (budget {BUDGET_BYTES / 1024:.0f} KB)')
    if total > BUDGET_BYTES:
        sys.exit('bake_audio: over budget')


if __name__ == '__main__':
    main()
