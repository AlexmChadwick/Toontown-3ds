"""Validate baked .tsnd files against their upstream .ogg.

    .venv/bin/python -I tools/pipeline/audio_validate.py [--wav-dir DIR] [names...]

For each asset in bake_audio.ASSETS:
  * parses the file with audio_tsnd.read (independent of the writer's state)
  * decodes it with the reference decoder in audio_dspadpcm.decode
  * re-decodes every block from its stored block-header state, as the runtime
    streamer does, and checks it is bit-identical to the continuous decode
  * checks the loop-point state matches the continuous decode
  * reports SNR against the ffmpeg-decoded source at the baked rate
  * optionally writes the decoded audio as .wav
"""
import argparse
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_dspadpcm as adp  # noqa: E402
import audio_tsnd as tsnd  # noqa: E402
import bake_audio as bake  # noqa: E402


def decode_file(info):
    ns = info['num_samples']
    chans = []
    for c in range(info['channels']):
        if info['encoding'] == tsnd.ENC_PCM16:
            chans.append(np.frombuffer(info['data'][c], dtype='<i2').copy())
            continue
        full = adp.decode(info['data'][c], info['coefs'][c], ns)
        # block-wise decode, as the streamer queues it
        bs, bpb = info['block_samples'], info['block_samples'] // adp.SPF * adp.BPF
        for b in range(info['num_blocks']):
            ps, h0, h1 = info['block_ctx'][c][b]
            seg = info['data'][c][b * bpb:(b + 1) * bpb]
            n = min(bs, ns - b * bs)
            part = adp.decode(seg, info['coefs'][c], n, hist0=h0, hist1=h1)
            if not np.array_equal(part, full[b * bs:b * bs + n]):
                raise SystemExit(f'block {b} ch {c}: block state mismatch')
            if ps != seg[0]:
                raise SystemExit(f'block {b} ch {c}: ps {ps} != frame header {seg[0]}')
        ls = info['loop_start']
        _, lh0, lh1 = info['loop_ctx'][c]
        exp = (int(full[ls - 1]) if ls >= 1 else 0, int(full[ls - 2]) if ls >= 2 else 0)
        if ls and (lh0, lh1) != exp:
            raise SystemExit(f'ch {c}: loop state mismatch')
        chans.append(full)
    return np.stack(chans)


def write_wav(path, pcm, rate):
    with wave.open(path, 'wb') as w:
        w.setnchannels(pcm.shape[0])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(np.ascontiguousarray(pcm.T, dtype='<i2').tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=os.path.join(bake.ROOT, 'romfs', 'baked', 'audio'))
    ap.add_argument('--wav-dir')
    ap.add_argument('names', nargs='*')
    args = ap.parse_args()
    if args.wav_dir:
        os.makedirs(args.wav_dir, exist_ok=True)
    for name, rel, kind, loops in bake.ASSETS:
        if args.names and name not in args.names:
            continue
        info = tsnd.read(os.path.join(args.dir, name + '.tsnd'))
        dec = decode_file(info)
        ref = bake.decode_ogg(os.path.join(bake.RES, rel), info['sample_rate'], info['channels'])
        snrs = [bake.snr_db(ref[c], dec[c]) for c in range(info['channels'])]
        # segmental SNR (20 ms windows, skipping near-silence) for the music
        seg = []
        win = info['sample_rate'] // 50
        for c in range(info['channels']):
            r = ref[c].astype(np.float64)
            e = r - dec[c]
            k = len(r) // win
            pr = (r[:k * win].reshape(k, win) ** 2).sum(1)
            pe = (e[:k * win].reshape(k, win) ** 2).sum(1)
            m = pr > win * 100.0
            if m.any() and (pe[m] > 0).all():
                seg.append(float(np.mean(10 * np.log10(pr[m] / pe[m]))))
        enc = 'ADPCM' if info['encoding'] == tsnd.ENC_ADPCM else 'PCM16'
        snr_txt = ' / '.join('exact' if s == float('inf') else f'{s:.2f}' for s in snrs)
        seg_txt = (' segSNR ' + ' / '.join(f'{s:.2f}' for s in seg)) if seg else ''
        print(f'{name:16} {enc} {info["channels"]}ch {info["sample_rate"]} Hz '
              f'{info["num_samples"]} samples, loop={info["loop"]}: SNR {snr_txt} dB{seg_txt}  [blocks ok]')
        if args.wav_dir:
            write_wav(os.path.join(args.wav_dir, name + '_decoded.wav'), dec, info['sample_rate'])
            write_wav(os.path.join(args.wav_dir, name + '_source.wav'), ref, info['sample_rate'])


if __name__ == '__main__':
    main()
