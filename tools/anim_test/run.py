#!/usr/bin/env python3
"""ASan/UBSan host playback and real TTA/cache tests; all fixtures stay in this worktree."""
import os,sys,struct,subprocess,re
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'sdmc-out/anim-test'
OUT.mkdir(parents=True,exist_ok=True)
COMMON=['c++','-std=c++17','-fno-rtti','-fno-exceptions','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(ROOT/'source')]
subprocess.run(COMMON+[str(ROOT/'tools/anim_test/playback_test.cpp'),str(ROOT/'source/ui/speedchat_data.cpp'),'-o',str(OUT/'playback')],check=True)
subprocess.run([str(OUT/'playback')],check=True)
subprocess.run(COMMON+['-I',str(ROOT/'tools/anim_test/stubs'),str(ROOT/'tools/anim_test/stream_test.cpp'),str(ROOT/'source/render/anim.cpp'),str(ROOT/'source/world/toon_cache.cpp'),str(ROOT/'source/world/toon_dna.cpp'),'-o',str(OUT/'stream')],check=True)
DATA=OUT/'sdmc:/3ds/toontown3ds/data/toon'
DATA.mkdir(parents=True,exist_ok=True)
clips=re.findall(r'TT_TOON_CLIP\([^,]+, "([^"]+)"\)',(ROOT/'source/world/toon_clips.inc').read_text())
rows={key:list(map(int,frames.split(','))) for key,frames in re.findall(r'\{"([^"]+)", \{([^}]+)\}\}',(ROOT/'source/world/toon_timing.inc').read_text())}
def model(path,expensive=False):
    header=bytearray(64);struct.pack_into('<4sHHHHII',header,0,b'TTM1',1,0,1 if expensive else 0,0,3,3);struct.pack_into('<I',header,44,64)
    if expensive:header+=struct.pack('<HHBBBBII32s',256,256,0,1,0,0,112,262144,b'fixture')
    path.write_bytes(header)
def clip(path,nf):
    nf=(nf+1)//2;ao=(128+nf*24+127)&~127
    buf=bytearray((ao+nf*48+127)&~127)
    struct.pack_into('<4sHHIHHf3f3fIIIII',buf,0,b'TTA1',1,1,3,nf,0,12,*([.001]*3),*([0]*3),128,24,ao,0,0)
    for f in range(nf):
        for v in range(3):struct.pack_into('<4h',buf,128+f*24+v*8,f,0,0,0)
        struct.pack_into('<12f',buf,ao+f*48,1,0,0,f,0,1,0,0,0,0,1,0)
    path.write_bytes(buf)
for key in ('legs_m_500','torso_ms_500','head_dls_500'):
    model(DATA/(key+'.ttm'))
    prefix=key[:-3]
    for name,nf in zip(clips,rows[prefix]):
        if nf:clip(DATA/(key+'_'+name+'.tta'),nf)
# The DNA table maps these static head codes; ample live references force admission denial.
for prefix in ('cat','horse','mouse','rabbit','duck','monkey','bear','pig'):
    for style in ('ss','sl','ls','ll'):
        code={'cat':'c','horse':'h','mouse':'m','rabbit':'r','duck':'f','monkey':'p','bear':'b','pig':'s'}[prefix]+style
        model(DATA/('head_'+code+'_500.ttm'),True)
(OUT/'bad.tta').write_bytes(b'TTA1')
subprocess.run([str(OUT/'stream')],check=True,cwd=OUT)
