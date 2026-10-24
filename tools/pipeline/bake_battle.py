#!/usr/bin/env python3
"""Reproduce the complete battle SD asset set without touching RomFS or references.

.venv/bin/python -I tools/pipeline/bake_battle.py [--stage all|toon|suit|props|audio|verify|preview]
At most two single-threaded workers run during the Toon stage; other stages are
single-process. No emulator, build, package installation, or runtime edits.
"""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import argparse
import hashlib
import json
import subprocess
from battle_common import ROOT,DATA,write_json
LEGACY='neutral,run,walk,jump-idle,wave'


def run(script,args=(),log=None):
    cmd=[sys.executable,'-I',str(ROOT/'tools/pipeline'/script),*map(str,args)]
    print('running',script,' '.join(map(str,args)),flush=True)
    if log:
        with (ROOT/'sdmc-out'/log).open('w') as stream:
            subprocess.run(cmd,cwd=ROOT,check=True,stdout=stream,stderr=subprocess.STDOUT)
    else:subprocess.run(cmd,cwd=ROOT,check=True)


def manifest():
    # Every shipping output is hashed; staging baseline/repeat subdirectories are
    # deliberately excluded. JSON files carry format versions and runtime maps.
    files={}
    for directory in ('suit','toon','props','audio'):
        for path in sorted((DATA/directory).iterdir()):
            if path.is_file() and path.name!='battle_manifest.json':
                files[f'{directory}/{path.name}']=dict(bytes=path.stat().st_size,
                    sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    write_json(DATA/'suit/battle_manifest.json',dict(magic='BATTLE_ASSETS',version=1,files=files,
        budget='suit/battle_budget.json',suits='suit/suit_manifest.json',toons='toon/toon_manifest.json',
        props='props/props_manifest.json',audio='audio/battle_audio_manifest.json'))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--stage',choices=('all','toon','suit','props','audio','verify','preview'),default='all')
    args=ap.parse_args();(ROOT/'sdmc-out').mkdir(exist_ok=True)
    if args.stage in ('all','toon'):
        out=DATA/'toon';out.mkdir(parents=True,exist_ok=True)
        hashfile=out/'legacy_before_sha256.json'
        if not hashfile.exists():
            before=out/'before'
            run('battle_toon.py',['--out-dir',before,'--clothes','none','--jobs','2','--anims',LEGACY],'toon-before.log')
            hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(before.iterdir()) if p.suffix in ('.ttm','.tta','.ttx')}
            write_json(hashfile,hashes)
        run('battle_toon.py',['--out-dir',out,'--clothes','none','--jobs','2'],'toon-after.log')
        run('battle_toon.py',['--worker','clothes','mat','--out-dir',out,'--lods','500,250',
            '--anims','neutral','--max-tex','128','--clothes-max-tex','64'],'toon-clothes.log')
        run('battle_toon_extras.py',log='toon-extras.log')
    for stage,script in [('suit','bake_suit.py'),('props','bake_props.py'),('audio','battle_audio.py')]:
        if args.stage in ('all',stage):run(script,log=stage+'.log')
    if args.stage in ('all','verify'):
        run('battle_validate.py');manifest()
    if args.stage in ('all','preview'):run('suit_preview.py',log='preview.log')

if __name__=='__main__':main()
