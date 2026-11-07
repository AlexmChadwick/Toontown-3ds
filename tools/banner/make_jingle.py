#!/usr/bin/env python3
"""HOME Menu banner jingle: an edit of tt_theme.ogg that ends on the tonic.

Usage: make_jingle.py <tt_theme decoded to 32 kHz 16-bit stereo .wav> <out.wav>

tt_theme (phase_3/audio/bgm, F major, ~156 bpm, beat 0.385 s) is cut into
  - the opening phrase: pickup at 0.35 s through the downbeat bar, cut at the beat-3
    onset (~1.48 s, C chord), then
  - the final cadence from the onset at ~49.16 s: G (V/V) -> C (V, 49.57 s) -> F (I, 49.96 s)
    and the track's own ring-out to its end (50.77 s).
Both cuts are snapped to the actual note onsets (2 ms energy rise) and joined with a 20 ms
equal-power crossfade that ends 10 ms before the onset, so neither attack is smeared.
Result ~2.75 s (HOME limit 3 s; build.sh enforces <= 2.9 s), peak -1 dBFS.
"""
import sys
import wave

import numpy as np
src, out = sys.argv[1], sys.argv[2]
w=wave.open(src); sr=w.getframerate(); assert sr==32000 and w.getnchannels()==2
x=np.frombuffer(w.readframes(w.getnframes()),np.int16).reshape(-1,2).astype(np.float64)/32768
m=x.mean(1)
def refine(t, span=0.03):
    # sharpest rise of the short-time energy near t (2 ms hops)
    h=64; a=int((t-span)*sr); b=int((t+span)*sr)
    e=np.array([np.sum(m[i:i+h]**2) for i in range(a,b,h)])
    d=np.diff(np.log(e+1e-9)); return (a+h*(np.argmax(d)+1))/sr
START=0.33           # just before the pickup (audio starts at ~0.35 s)
A=refine(1.50)       # opening phrase cut: beat 3 of bar 1 (beat = 0.385 s, 156 bpm)
B=refine(49.19)      # final cadence: G (V/V) -> C (V) at 49.57 -> F (I) at 49.96, ring-out to end
END=len(m)/sr
pre=0.010; xf=0.020  # crossfade 20 ms, ending 10 ms before each onset so both attacks stay intact
a0=int((A-pre-xf)*sr); a1=int((A-pre)*sr); b0=int((B-pre-xf)*sr)
n=a1-a0; t=np.linspace(0,np.pi/2,n)[:,None]
head=x[int(START*sr):a0]; mix=x[a0:a1]*np.cos(t)+x[b0:b0+n]*np.sin(t); tail=x[b0+n:]
y=np.concatenate([head,mix,tail])
fi=int(0.004*sr); y[:fi]*=np.linspace(0,1,fi)[:,None]
fo=int(0.12*sr); y[-fo:]*=np.cos(np.linspace(0,np.pi/2,fo))[:,None]**2
y*=10**(-1/20)/np.abs(y).max()
print(f'A={A:.4f} B={B:.4f} splice gap kept on beat: (B-A)/0.385={(B-A)/0.385:.2f} beats; len={len(y)/sr:.3f}s')
o=wave.open(out,'wb'); o.setnchannels(2); o.setsampwidth(2); o.setframerate(sr)
o.writeframes((np.clip(y,-1,1)*32767).astype('<i2').tobytes()); o.close()
