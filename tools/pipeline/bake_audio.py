"""Bake upstream .ogg audio into .tsnd (see audio_tsnd.py for the format).

    .venv/bin/python -I tools/pipeline/bake_audio.py [--out romfs/baked/audio]
    .venv/bin/python -I tools/pipeline/bake_audio.py --set boat --out sdmc-out/3ds/toontown3ds/data/audio
        [--resources /path/to/ref/resources]

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
Set `boat` (SD, Donald's Dock, source/world/boat.cpp; toontown/safezone/DDSafeZoneLoader.py loads them):
  boat_bell     SZ_DD_shipbell   played at the start of each sail (SoundInterval at the boat)
  boat_foghorn  SZ_DD_foghorn    end-aligned with the sail
  boat_creak    SZ_DD_dockcreak  pier tilting down / rising
  boat_lap      SZ_DD_waterlap   looped while the local toon is on the boat (DDPlayground.enterOnBoat)
Upstream has no jump sound for toons (LocalAvatar plays none), so none is baked.

The soundscape set (SOUNDSCAPE below; docs/design/audio.md has the table with sources):
  dest 'romfs'  -> --out        small SFX every zone needs (doors, teleport, SpeedChat, TTC birds)
  dest 'sd'     -> --sd-out     street/interior music and the other playgrounds' ambient SFX,
                                streamed / loaded on zone change from sdmc:/3ds/toontown3ds/data/audio/
SFX kinds: 'sfx' = PCM16 mono 22050 Hz, 'sfx_adpcm' = DSP-ADPCM mono 22050 Hz (0.28x the bytes;
set TT_SFX_ADPCM=0 or --sfx-pcm16 to bake those as PCM16 for an A/B test).

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


def _find_resources():
    """ref/resources of this checkout, else of the main checkout when run from a git worktree."""
    if os.environ.get('TT_RES'):
        return os.environ['TT_RES']
    d = ROOT
    while True:
        cand = os.path.join(d, 'ref', 'resources')
        if (os.path.isdir(cand) and os.listdir(cand)) or os.path.dirname(d) == d:
            return cand
        d = os.path.dirname(d)


RES = _find_resources()
BUDGET_BYTES = 3 * 1024 * 1024
ROMFS_NEW_BUDGET = 300 * 1024          # SOUNDSCAPE assets with dest 'romfs'
RESIDENT_SFX_BUDGET = 1536 * 1024      # all SOUNDSCAPE SFX loaded at once (2 MB audio - ring - slack)
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

# (output name, upstream path, kind, loops, dest). See docs/design/audio.md for the use of each.
_SFX = 'phase_3.5/audio/sfx/'
SOUNDSCAPE = [
    # --- global SFX (RomFS) ---
    ('door_open', _SFX + 'Door_Open_1.ogg', 'sfx', False, 'romfs'),            # DistributedDoor.py:44
    ('door_close', _SFX + 'Door_Close_1.ogg', 'sfx', False, 'romfs'),          # DistributedDoor.py:45
    ('teleport', _SFX + 'AV_teleport.ogg', 'sfx_adpcm', False, 'romfs'),       # Toon.py:1763
    ('sc_open', _SFX + 'GUI_quicktalker.ogg', 'sfx', False, 'romfs'),          # ToontownChatManager.py:37
    ('treasure_reject', 'phase_4/audio/sfx/ring_miss.ogg', 'sfx', False, 'romfs'),  # DistributedTreasure.py:20
    # --- ambient SFX (random one-shots, see *Playground.py) ---
    ('amb_tt_bird1', 'phase_4/audio/sfx/SZ_TC_bird1.ogg', 'sfx_adpcm', False, 'romfs'),  # TTSafeZoneLoader.py:19
    ('amb_tt_bird2', 'phase_4/audio/sfx/SZ_TC_bird2.ogg', 'sfx', False, 'romfs'),
    ('amb_tt_bird3', 'phase_4/audio/sfx/SZ_TC_bird3.ogg', 'sfx_adpcm', False, 'romfs'),
    ('amb_dd_seagull', 'phase_6/audio/sfx/SZ_DD_Seagull.ogg', 'sfx_adpcm', False, 'sd'),  # DDSafeZoneLoader.py:21
    ('amb_br_wind1', 'phase_8/audio/sfx/SZ_TB_wind_1.ogg', 'sfx_adpcm', False, 'sd'),     # BRSafeZoneLoader.py:20-22
    ('amb_br_wind2', 'phase_8/audio/sfx/SZ_TB_wind_2.ogg', 'sfx_adpcm', False, 'sd'),
    ('amb_br_wind3', 'phase_8/audio/sfx/SZ_TB_wind_3.ogg', 'sfx_adpcm', False, 'sd'),
    ('amb_dg_bird1', 'phase_8/audio/sfx/SZ_DG_bird_01.ogg', 'sfx_adpcm', False, 'sd'),    # DGSafeZoneLoader.py:17-20
    ('amb_dg_bird2', 'phase_8/audio/sfx/SZ_DG_bird_02.ogg', 'sfx_adpcm', False, 'sd'),
    ('amb_dg_bird3', 'phase_8/audio/sfx/SZ_DG_bird_03.ogg', 'sfx_adpcm', False, 'sd'),
    ('amb_dg_bird4', 'phase_8/audio/sfx/SZ_DG_bird_04.ogg', 'sfx_adpcm', False, 'sd'),
    # --- street music (TownLoader.musicFile) st_<hood> and interior music (activityMusicFile) in_<hood> ---
    ('st_2000', 'phase_3.5/audio/bgm/TC_SZ.ogg', 'music', True, 'sd'),
    ('in_2000', 'phase_3.5/audio/bgm/TC_SZ_activity.ogg', 'music', True, 'sd'),
    ('st_1000', 'phase_6/audio/bgm/DD_SZ.ogg', 'music', True, 'sd'),
    ('in_1000', 'phase_6/audio/bgm/DD_SZ_activity.ogg', 'music', True, 'sd'),
    ('st_4000', 'phase_6/audio/bgm/MM_SZ.ogg', 'music', True, 'sd'),
    ('in_4000', 'phase_6/audio/bgm/MM_SZ_activity.ogg', 'music', True, 'sd'),
    ('st_5000', 'phase_8/audio/bgm/DG_SZ.ogg', 'music', True, 'sd'),   # DG's interiors use the same file
    ('st_3000', 'phase_8/audio/bgm/TB_SZ.ogg', 'music', True, 'sd'),
    ('in_3000', 'phase_8/audio/bgm/TB_SZ_activity.ogg', 'music', True, 'sd'),
    ('st_9000', 'phase_8/audio/bgm/DL_SZ.ogg', 'music', True, 'sd'),
    ('in_9000', 'phase_8/audio/bgm/DL_SZ_activity.ogg', 'music', True, 'sd'),
    ('in_8000', 'phase_6/audio/bgm/GS_KartShop.ogg', 'music', True, 'sd'),  # Goofy Speedway / Outdoor Zone shops
    # --- Donald's Dock boat (source/world/boat.cpp; docs/design/boat.md) ---
    ('boat_bell', 'phase_6/audio/sfx/SZ_DD_shipbell.ogg', 'sfx', False, 'sd'),
    ('boat_foghorn', 'phase_5/audio/sfx/SZ_DD_foghorn.ogg', 'sfx', False, 'sd'),
    ('boat_creak', 'phase_6/audio/sfx/SZ_DD_dockcreak.ogg', 'sfx', False, 'sd'),
    ('boat_lap', 'phase_6/audio/sfx/SZ_DD_waterlap.ogg', 'sfx', True, 'sd'),
]
ALL_ASSETS = [(n, r, k, l, 'romfs') for n, r, k, l in ASSETS] + SOUNDSCAPE


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


def bake_sfx_adpcm(src, dst, loops):
    """Mono DSP-ADPCM SFX: a single block holding the whole sound (loaded with one read)."""
    pcm = decode_ogg(src, SFX_RATE, 1)
    n = pcm.shape[1]
    coefs = [adp.compute_coefs(pcm[0])]
    data, decoded, headers, h0, h1 = adp.encode([pcm[0]], coefs)
    block_samples = (n + adp.SPF - 1) // adp.SPF * adp.SPF
    loop_ctx = [(int(headers[0, 0]), int(h0[0, 0]), int(h1[0, 0]))]
    block_ctx = [[loop_ctx[0]]]
    size = tsnd.write(dst, encoding=tsnd.ENC_ADPCM, sample_rate=SFX_RATE, num_samples=n,
                      channel_data=data, loop=loops, loop_start=0, loop_end=n,
                      block_samples=block_samples, coefs=coefs,
                      block_ctx=block_ctx, loop_ctx=loop_ctx)
    return size, n, SFX_RATE, 1, pcm, [decoded[0]]


def snr_db(ref, test):
    ref = ref.astype(np.float64)
    err = ref - test.astype(np.float64)
    pe = (err * err).sum()
    return float('inf') if pe == 0 else 10 * np.log10((ref * ref).sum() / pe)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(ROOT, 'romfs', 'baked', 'audio'))
    ap.add_argument('--sd-out', default=os.path.join(ROOT, 'sdmc-out', '3ds', 'toontown3ds', 'data', 'audio'),
                    help="output for SOUNDSCAPE assets with dest 'sd' (gitignored)")
    ap.add_argument('--only', help='bake only this output name')
    ap.add_argument('--resources', default=RES, help='ref/resources checkout (default: <repo>/ref/resources)')
    ap.add_argument('--sfx-pcm16', action='store_true', help="bake 'sfx_adpcm' assets as PCM16 (A/B test)")
    args = ap.parse_args()
    if os.environ.get('TT_SFX_ADPCM') == '0':
        args.sfx_pcm16 = True
    os.makedirs(args.out, exist_ok=True)
    os.makedirs(args.sd_out, exist_ok=True)
    totals = {'romfs': 0, 'sd': 0}
    romfs_new = 0
    resident = 0
    print(f'{"asset":16} {"dst":5} {"enc":6} {"ch":>2} {"rate":>6} {"secs":>6} {"ogg KB":>7} {"tsnd KB":>8} {"SNR dB":>7}')
    for name, rel, kind, loops, dest in ALL_ASSETS:
        dst = os.path.join(args.out if dest == 'romfs' else args.sd_out, name + '.tsnd')
        legacy = (name, rel, kind, loops) in ASSETS
        if args.only and args.only != name:
            if os.path.exists(dst):
                size = os.path.getsize(dst)
                totals[dest] += size
                if not legacy and dest == 'romfs':
                    romfs_new += size
                if kind != 'music' and not legacy:
                    resident += size
            continue
        src = os.path.join(args.resources, rel)
        t0 = time.time()
        if kind == 'music':
            size, n, rate, ch, ref, dec = bake_music(src, dst, loops)
            snr = min(snr_db(ref[c], dec[c]) for c in range(ch))
            enc = 'ADPCM'
        elif kind == 'sfx_adpcm' and not args.sfx_pcm16:
            size, n, rate, ch, ref, dec = bake_sfx_adpcm(src, dst, loops)
            snr = snr_db(ref[0], dec[0])
            enc = 'ADPCM'
        else:
            size, n, rate, ch, ref, dec = bake_sfx(src, dst, loops)
            snr = float('inf')
            enc = 'PCM16'
        totals[dest] += size
        if not legacy and dest == 'romfs':
            romfs_new += size
        if kind != 'music' and not legacy:
            resident += size
        print(f'{name:16} {dest:5} {enc:6} {ch:2} {rate:6} {n / rate:6.2f} {os.path.getsize(src) / 1024:7.1f} '
              f'{size / 1024:8.1f} {snr:7.1f}   ({time.time() - t0:.1f}s) <- {rel}')
    print(f'romfs total {totals["romfs"] / 1024:.1f} KB (budget {BUDGET_BYTES / 1024:.0f} KB), '
          f'of which soundscape {romfs_new / 1024:.1f} KB (budget {ROMFS_NEW_BUDGET / 1024:.0f} KB)')
    print(f'sd total {totals["sd"] / 1024:.1f} KB; soundscape SFX if all resident {resident / 1024:.1f} KB '
          f'(budget {RESIDENT_SFX_BUDGET / 1024:.0f} KB)')
    if totals['romfs'] > BUDGET_BYTES or romfs_new > ROMFS_NEW_BUDGET or resident > RESIDENT_SFX_BUDGET:
        sys.exit('bake_audio: over budget')


if __name__ == '__main__':
    main()
