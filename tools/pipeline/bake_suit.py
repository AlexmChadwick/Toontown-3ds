#!/usr/bin/env python3
"""Bake shared Cog bodies, 32 heads, department textures and 12 Hz animation."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import argparse
import json
import struct
from battle_common import DATA,RES,capture,ttm,write_json,bake_actor
import numpy as np
from panda3d.core import NodePath
from direct.showbase.ShowBase import ShowBase
from suit_source import source_data,animations
DEPTS = ('c','l','m','s')
SLOTS = [('torso','blazer',1),('arms','sleeve',2),('legs','leg',3)]
TINT_RULES = [('torso',1),('arms',2),('legs',3),('hands',4)]


def clothes(base,actor,dept='c'):
    for node,suffix,slot in SLOTS:
        for part in actor.findAllMatches('**/'+node):
            part.setTexture(base.loader.loadTexture(f'phase_3.5/maps/{dept}_{suffix}.jpg'),1)
    for part in actor.findAllMatches('**/hands'):
        part.setColor(1,1,1,1)


def pack(base,path):
    import extract
    textures=[]
    for dept in DEPTS:
        for _,suffix,_ in SLOTS:
            tex=base.loader.loadTexture(f'phase_3.5/maps/{dept}_{suffix}.jpg')
            px,n,fmt=extract.texture_pixels(tex,64)
            textures.append(ttm.Texture(f'{dept}_{suffix}',px,extract.choose_format(px,n,fmt)))
    # TTX1's four table counts are reused as 3 entries per department, not Toon DNA.
    names=32; entries=56; cursor=ttm._align(entries+48*len(textures));out=bytearray(cursor)
    struct.pack_into('<4sHH4HII2I',out,0,b'TTX1',1,12,3,3,3,3,names,entries,0,0)
    struct.pack_into('<12H',out,names,*range(12))
    for i,t in enumerate(textures):
        struct.pack_into('<HHBBBBII32s',out,entries+i*48,t.w,t.h,t.fmt,t.mips,t.wrap_s,t.wrap_t,
                         cursor,len(t.data),t.name.encode().ljust(32,b'\0'))
        out.extend(t.data);cursor=ttm._align(len(out));out.extend(bytes(cursor-len(out)))
    path.write_bytes(out)
    return dict(file=path.name,bytes=len(out),texture_bytes=sum(len(t.data) for t in textures),
                departments=list(DEPTS),slots={node:slot for node,_,slot in SLOTS})


def head(base,out,code,spec):
    body=spec['body'];phase=3.5 if body=='c' else 4
    model=base.loader.loadModel(f'phase_{phase:g}/models/char/suit{body.upper()}-heads')
    root=NodePath('head')
    for part in spec['parts']:
        found=model.findAllMatches('**/'+part)
        assert len(found), (code,part)
        for node in found:
            copy=node.copyTo(root)
            if spec['head_texture']:
                copy.setTexture(base.loader.loadTexture(f"phase_{phase:g}/maps/{spec['head_texture']}"),1)
            if spec['head_color'] is not None:
                copy.setColor(*spec['head_color'])
    for max_tex in (256,128,64):
        ex,batches=capture(root,True,animated=False,max_tex=max_tex)
        if sum(len(t.data) for t in ex.textures)<=64*1024:break
    filename=f'head_{code}.ttm';stats=ttm.write(out/filename,ex.textures,batches)
    assert stats['texture_bytes']<=96*1024
    root.removeNode();model.removeNode()
    return dict(file=filename,max_texture_size=max_tex,**spec,**stats)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=DATA/'suit')
    ap.add_argument('--no-variants',action='store_true')
    ap.add_argument('--heads-only',action='store_true',help='refresh heads in an existing complete bake')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    base=ShowBase();specs,tables,anim_of=source_data()
    if args.heads_only:
        manifest=json.loads((args.out/'suit_manifest.json').read_text())
        for code,spec in specs.items():manifest['heads'][code]=head(base,args.out,code,spec)
        for variant,lods in manifest['bodies'].items():
            if 'skeleton' in variant:continue
            heads=[h for h in manifest['heads'].values() if h['body']==variant[0]]
            for entry in lods.values():
                entry['max_actor_texture_bytes']=entry['texture_bytes']+max(h['texture_bytes'] for h in heads)
                assert entry['max_actor_texture_bytes']<=96*1024
        write_json(args.out/'suit_manifest.json',manifest);base.destroy();return
    manifest=dict(version=1,fps=12,bodies={},heads={},attacks={},clothes=pack(base,args.out/'departments.ttx'))
    for code,spec in specs.items():
        manifest['heads'][code]=head(base,args.out,code,spec)
    for body in 'abc':
        clips,counts=animations(body,specs,tables,anim_of)
        prefix=f'suit{body.upper()}'
        manifest['attacks'][body]=dict(selected=counts,fallback='throw-paper',
            species={c:{atk:(anim_of[atk] if anim_of[atk] in counts else 'throw-paper') for atk in s['attacks']}
                     for c,s in specs.items() if s['body']==body})
        manifest['bodies'][body]=bake_actor(base,args.out,prefix,f'phase_3.5/models/char/{prefix}-mod',clips,
            {'full':1400,'low':327},lambda a:clothes(base,a),TINT_RULES,'joint_head')
        if not args.no_variants:
            phase=3.5 if body=='c' else 4
            lose={"lose":f'phase_{phase:g}/models/char/{prefix}-lose'}
            manifest['bodies'][body+'_lose']=bake_actor(base,args.out,prefix+'_lose',
                f'phase_{phase:g}/models/char/{prefix}-lose-mod',lose,{'full':1400,'low':327},
                lambda a:clothes(base,a),TINT_RULES,'joint_head')
            manifest['bodies'][body+'_skeleton']=bake_actor(base,args.out,prefix+'_skeleton',
                f'phase_5/models/char/cog{body.upper()}_robot-zero',clips,{'full':1500,'low':400})
            manifest['bodies'][body+'_skeleton_lose']=bake_actor(base,args.out,prefix+'_skeleton_lose',
                f'phase_5/models/char/cog{body.upper()}_robot-lose-mod',lose,{'full':1500,'low':400})
    for body in 'abc':
        heads=[h for h in manifest['heads'].values() if h['body']==body]
        for lod,limit in [('full',1500),('low',400)]:
            for variant in (body,body+'_lose'):
                if variant in manifest['bodies']:
                    entry=manifest['bodies'][variant][lod]
                    entry['max_actor_triangles']=entry['triangles']+max(h['triangles'] for h in heads)
                    entry['max_actor_texture_bytes']=entry['texture_bytes']+max(h['texture_bytes'] for h in heads)
                    assert entry['max_actor_triangles']<=limit
                    assert entry['max_actor_texture_bytes']<=96*1024
    from battle_ties import finalize
    finalize(base,args.out,manifest)
    write_json(args.out/'suit_manifest.json',manifest)
    base.destroy()

if __name__=='__main__':main()
