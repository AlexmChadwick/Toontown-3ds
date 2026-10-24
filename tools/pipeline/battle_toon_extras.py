#!/usr/bin/env python3
"""Verify legacy hashes; export rigid-head battle clips and Toon hand tracks."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import argparse
import hashlib
import json
import struct
from battle_common import DATA,UP,ROOT,write_json,write_joints,mat34,tta
import numpy as np
sys.path.insert(0,str(UP))
import bake_toon as toon
from direct.actor.Actor import Actor
from direct.showbase.ShowBase import ShowBase
BATTLE=('throw','pushbutton','cringe','conked','sidestep-left','sidestep-right','duck','victory','lose','sad-walk','teleport','book')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=DATA/'toon')
    args=ap.parse_args();out=args.out
    before=json.loads((out/'legacy_before_sha256.json').read_text())
    for filename,digest in before.items():
        assert hashlib.sha256((out/filename).read_bytes()).hexdigest()==digest, filename
    base=ShowBase();manifest=json.loads((out/'toon_manifest.json').read_text())
    static=0;hands=0
    for code,lods in manifest['parts']['head'].items():
        if code.startswith('d'):continue
        for lod,entry in lods.items():
            data=(out/entry['ttm']).read_bytes()
            h=struct.unpack_from('<4sHHHHII3f3fIIIII',data)
            nv=h[5];scale=np.array(h[7:10]);offset=np.array(h[10:13]);voff=h[-3]
            verts=np.frombuffer(data,dtype=[('p','<i2',4),('uv','<i2',2),('c','u1',4)],count=nv,offset=voff)
            positions=verts['p'][:,:3]*scale+offset
            for clip in BATTLE:
                filename=f'head_{code}_{lod}_{clip}.tta'
                tta.write(out/filename,positions[None,:,:],12)
                entry['clips'][clip]=filename;static+=1
            entry['rigid_battle_clips']=True
    for code,prefix in toon.TORSOS.items():
        for lod in ('500','250'):
            paths={name:toon.phase_dir(toon.ANIMS[name][1])+prefix+toon.ANIMS[name][0] for name in BATTLE}
            actor=Actor({'torso':toon.CHAR+prefix+lod},{'torso':paths})
            part=actor.getPart('torso')
            names=['def_joint_left_hold','def_joint_right_hold']
            joints=[part.find('**/'+name) for name in names]
            assert all(not j.isEmpty() for j in joints)
            for clip in BATTLE:
                rate=actor.getFrameRate(clip);step=max(1,round(rate/12));assert rate/step==12
                frames=[]
                for f in range(0,actor.getNumFrames(clip),step):
                    actor.pose(clip,f);actor.update(force=True)
                    frames.append([mat34(j,part) for j in joints])
                filename=f'torso_{code}_{lod}_{clip}.jnt';write_joints(out/filename,names,frames);hands+=1
            actor.cleanup();actor.removeNode()
    clothes=json.loads((ROOT/'sdmc-out/toon-clothes.log').read_text().strip().splitlines()[-1])
    manifest['clothes']={'file':'clothes.ttx',**clothes}
    # Keep the standard Toon manifest and its complete file census current.
    manifest['battle_clips']=list(BATTLE);manifest['legacy_hash_count']=len(before)
    manifest['rigid_head_battle_clips']=static;manifest['hand_tracks']=hands
    manifest['files']={p.name:p.stat().st_size for p in sorted(out.iterdir()) if p.suffix in ('.ttm','.tta','.ttx','.jnt')}
    manifest['totals']={'files':len(manifest['files']),'bytes':sum(manifest['files'].values())}
    write_json(out/'toon_manifest.json',manifest)
    write_json(out/'legacy_hash_verification.json',dict(version=1,files=len(before),unchanged=True,sha256=before))
    print(f'PASS: {len(before)} legacy files unchanged; {static} rigid-head clips; {hands} hand tracks')
    base.destroy()

if __name__=='__main__':main()
