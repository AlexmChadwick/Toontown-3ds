#!/usr/bin/env python3
"""Bake SD battle audio with bounded ffmpeg threads and an allocation report."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import argparse
import subprocess
from battle_common import DATA,RES,write_json
import numpy as np
import bake_audio as audio
import audio_tsnd as tsnd


def decode(path,rate,channels):
    cmd=[audio.find_ffmpeg(),'-v','error','-nostdin','-threads','1','-filter_threads','1','-i',str(path),
         '-map','0:a:0','-af','aresample=filter_size=64:cutoff=0.97','-ar',str(rate),'-ac',str(channels),
         '-f','s16le','-acodec','pcm_s16le','-threads','1','-']
    pcm=np.frombuffer(subprocess.run(cmd,check=True,capture_output=True).stdout,dtype='<i2')
    return pcm.reshape(-1,channels).T.copy()


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path,default=DATA/'audio')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=True);audio.decode_ogg=decode
    manifest=dict(version=1,music_ring_bytes=128*1024,assets={})
    for name,rel,kind,loop in audio.BATTLE_ASSETS:
        path=args.out/(name+'.tsnd');src=RES/rel
        result=(audio.bake_music if kind=='music' else audio.bake_sfx)(str(src),str(path),loop)
        size,n,rate,channels,reference,decoded=result
        parsed=tsnd.read(path)
        assert parsed['num_samples']==n
        snr=min(audio.snr_db(reference[c],decoded[c]) for c in range(channels))
        manifest['assets'][name]=dict(file=path.name,source=rel,kind=kind,bytes=size,
            samples=n,rate=rate,channels=channels,seconds=n/rate,loops=loop,
            snr_db=None if not np.isfinite(snr) else float(snr),
            linear_bytes=128*1024 if kind=='music' else n*2)
        print(name,f'{size/1024:.1f} KiB',f'{n/rate:.2f}s',flush=True)
    write_json(args.out/'battle_audio_manifest.json',manifest)

if __name__=='__main__':main()
