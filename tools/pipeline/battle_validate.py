#!/usr/bin/env python3
"""Validate generated battle formats, legacy hashes, and the resident scene budget."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import hashlib
import json
import struct
from battle_common import DATA,ROOT,ttm,write_json
import numpy as np
import audio_tsnd


def mesh(path):
    data=path.read_bytes();h=struct.unpack_from('<4sHHHHII3f3fIIIII',data)
    magic,version,hdr_flags,nt,nb,nv,ni,*rest=h
    assert magic==b'TTM1' and version==1 and hdr_flags in (0, 1)
    texoff,batchoff,voff,ioff,_=rest[6:]
    assert voff%128==ioff%128==0 and len(data)%128==0
    vertices=indices=0;textures=0
    for i in range(nb):
        tex,blend,flags,tint,layer,slot,_,fv,cnt,fi,icnt=struct.unpack_from('<HBBBBBBIIII',data,batchoff+i*24)
        assert fv==vertices and fi==indices and icnt%3==0 and (tex==65535 or tex<nt)
        idx=np.frombuffer(data,dtype='<u2',count=icnt,offset=ioff+fi*2)
        if hdr_flags & 1:
            assert int(idx.min())>=fv and int(idx.max())<fv+cnt
        else:
            assert int(idx.max())<cnt
        vertices+=cnt;indices+=icnt
    assert (vertices,indices)==(nv,ni)
    for i in range(nt):
        w,ht,fmt,mips,ws,wt,off,size,name=struct.unpack_from('<HHBBBBII32s',data,texoff+i*48)
        assert off%128==0 and off+size<=len(data)
        assert size==sum(ttm.level_size(w>>j,ht>>j,fmt) for j in range(mips))
        textures+=size
    return dict(vertices=nv,triangles=ni//3,batches=nb,texture_bytes=textures,file_bytes=len(data),
                # Conservatively count all file padding and an allocator allowance.
                resident_bytes=len(data)+(nt+2)*128)


def clip(path,vertices):
    data=path.read_bytes();h=struct.unpack_from('<4sHHIHHf3f3fIIIII',data)
    magic,version,flags,nv,nf,_,fps,*rest=h
    assert magic==b'TTA1' and version==1 and nv==vertices and nf>0 and fps==12
    off,stride,attach,_,_=rest[6:]
    assert off%128==0 and stride==nv*8 and off+stride*nf<=len(data)
    assert np.isfinite(rest[:6]).all() and min(rest[:3])>0
    if flags&1:assert attach%128==0 and attach+nf*48<=len(data)
    q=np.frombuffer(data,dtype='<i2',count=nf*nv*4,offset=off).reshape(nf,nv,4)
    assert np.all(q[:,:,3]==0)
    return dict(frames=nf,bytes=len(data),vertices=nv,frame_bytes=stride,
                pair_bytes=2*ttm._align(stride),attach=bool(flags&1))


def clothes_resident(path,toons):
    """clothes.ttx is never resident: ToonCache keeps its map and texture table in heap and
    reads each toon's shirt, sleeve and bottom texture by seeking. Count that index plus
    the largest texture of each kind for every toon (both bottom tables can be a toon's)."""
    data=path.read_bytes();h=struct.unpack_from('<4sHH4HII2I',data)
    ntex,counts,mapoff,texoff=h[2],h[3:7],h[7],h[8]
    table=struct.unpack_from(f'<{sum(counts)}H',data,mapoff)
    sizes=[struct.unpack_from('<HHBBBBII32s',data,texoff+i*48)[7] for i in range(ntex)]
    largest=[];start=0
    for n in counts:
        largest.append(max([sizes[t] for t in table[start:start+n] if t!=65535] or [0]));start+=n
    per_toon=largest[0]+largest[1]+max(largest[2],largest[3])
    return 2*sum(counts)+48*ntex+toons*per_toon

def main():
    counts=dict(models=0,clips=0,joint_tracks=0,audio=0,texture_packs=0)
    mesh_info={};clip_info={}
    for directory in ('suit','toon','props'):
        root=DATA/directory
        for path in sorted(root.glob('*.ttm')):
            info=mesh(path);mesh_info[f'{directory}/{path.name}']=info;counts['models']+=1
        for path in sorted(root.glob('*.tta')):
            # Clip names add a suffix to the corresponding model stem. Match the
            # longest existing stem, since both species and clip names have '_'.
            stems=[(key,info) for key,info in mesh_info.items() if key.startswith(directory+'/')
                   and path.stem.startswith(Path(key).stem+'_')]
            key,info=max(stems,key=lambda item:len(Path(item[0]).stem))
            result=clip(path,info['vertices']);clip_info[f'{directory}/{path.name}']=result;counts['clips']+=1
        for path in sorted(root.glob('*.jnt')):
            data=path.read_bytes();magic,ver,nj,nf,fps,off,stride,names,_=struct.unpack_from('<4sHHIfIIII',data)
            assert magic==b'JNT1' and ver==1 and fps==12 and off%128==0 and stride==nj*48
            assert names==32 and len(data)==off+nf*stride
            assert np.isfinite(np.frombuffer(data,dtype='<f4',offset=off)).all();counts['joint_tracks']+=1
    for path in sorted(DATA.glob('*/*.ttx')):
        data=path.read_bytes();h=struct.unpack_from('<4sHH4HII2I',data)
        magic,version,ntex,*tail=h;counts_in_tables=tail[:4];mapoff,texoff=tail[4:6]
        assert magic==b'TTX1' and version==1
        table=np.frombuffer(data,dtype='<u2',offset=mapoff,count=sum(counts_in_tables))
        assert np.all((table<ntex)|(table==65535))
        for i in range(ntex):
            w,ht,fmt,mips,ws,wt,off,size,name=struct.unpack_from('<HHBBBBII32s',data,texoff+i*48)
            assert off%128==0 and off+size<=len(data)
            assert size==sum(ttm.level_size(w>>j,ht>>j,fmt) for j in range(mips))
        counts['texture_packs']+=1
    for path in sorted((DATA/'audio').glob('*.tsnd')):
        parsed=audio_tsnd.read(path);assert parsed['num_samples']>0;counts['audio']+=1
    legacy=json.loads((DATA/'toon/legacy_before_sha256.json').read_text())
    for name,digest in legacy.items():
        assert hashlib.sha256((DATA/'toon'/name).read_bytes()).hexdigest()==digest,name
    suit=json.loads((DATA/'suit/suit_manifest.json').read_text());assert len(suit['heads'])==32
    rows=[];cog_resident=[];cog_full=[]
    for variant,lods in suit['bodies'].items():
        body=variant[0];is_skeleton='skeleton' in variant
        heads=[h for h in suit['heads'].values() if h['body']==body]
        for lod,entry in lods.items():
            head_tri=0 if is_skeleton else max(h['triangles'] for h in heads)
            tex_head=0 if is_skeleton else max(h['texture_bytes'] for h in heads)
            actor_tri=entry['triangles']+head_tri
            assert actor_tri <= (400 if lod=='low' else 1500),(variant,lod,actor_tri)
            assert entry['texture_bytes']+tex_head<=96*1024
            assert len(suit['attacks'][body]['selected'])<=12
            body_mesh=mesh_info['suit/'+entry['file']]
            head_size=0 if is_skeleton else max(mesh_info['suit/'+h['file']]['resident_bytes'] for h in heads)
            pairs=max(clip_info['suit/'+c['file']]['pair_bytes'] for c in entry['clips'].values())
            biggest=max(c['bytes'] for c in entry['clips'].values())
            resident=body_mesh['resident_bytes']+head_size+pairs
            if lod=='full':cog_resident.append(resident);cog_full.append(body_mesh['resident_bytes']+head_size+biggest)
            rows.append(dict(variant=variant,lod=lod,body_triangles=entry['triangles'],actor_triangles=actor_tri,
                batches=entry['batches'],texture_bytes=entry['texture_bytes']+tex_head,
                model_bytes=entry['file_bytes'],largest_clip_bytes=biggest,resident_actor_bytes=resident))
    toon=json.loads((DATA/'toon/toon_manifest.json').read_text());assert len(toon['battle_clips'])==12
    toon_pair=toon_full=0
    for kind in ('legs','torso','head'):
        resident=[];full=[]
        for code,lods in toon['parts'][kind].items():
            entry=lods['500'];model=mesh_info['toon/'+entry['ttm']]
            assert all(name in entry['clips'] for name in toon['battle_clips'])
            clips=[clip_info['toon/'+entry['clips'][name]] for name in toon['battle_clips']]
            resident.append(model['resident_bytes']+max(c['pair_bytes'] for c in clips))
            full.append(model['resident_bytes']+max(c['bytes'] for c in clips))
        toon_pair+=max(resident);toon_full+=max(full)
    # One largest eyelash mesh per toon, including male/female mixed parties.
    lash=max(v['resident_bytes'] for k,v in mesh_info.items() if k.startswith('toon/lashes_'))
    toon_pair+=lash;toon_full+=lash
    props=json.loads((DATA/'props/props_manifest.json').read_text())
    prop_models=sum(mesh_info['props/'+e['file']]['resident_bytes'] for e in props['props'].values())
    prop_pairs=sum(max([clip_info['props/'+c['file']]['pair_bytes'] for c in e['clips'].values()] or [0]) for e in props['props'].values())
    audio=json.loads((DATA/'audio/battle_audio_manifest.json').read_text())
    sfx=sorted((e['linear_bytes'] for e in audio['assets'].values() if e['kind']=='sfx'),reverse=True)
    allocation={
        'four_cogs_models_and_two_frames':4*max(cog_resident),
        'four_toons_models_and_two_frames':4*toon_pair,
        'all_twenty_prop_models_and_two_frames':prop_models+prop_pairs,
        'skeleton_tie_pack':(DATA/'suit/skeleton_ties.ttx').stat().st_size,
        'department_pack':(DATA/'suit/departments.ttx').stat().st_size,
        'four_toons_clothing':clothes_resident(DATA/'toon/clothes.ttx',4),
        'gag_icon_atlas':mesh_info['props/inventory_atlas.ttm']['resident_bytes'],
        'music_ring':audio['music_ring_bytes'],
        'four_sfx_slots_largest_cue':4*max(sfx)+4*128,
        'allocation_and_transition_reserve':256*1024}
    total=sum(allocation.values());assert total<=4*1024*1024,allocation
    assert allocation['music_ring']+allocation['four_sfx_slots_largest_cue']<=2*1024*1024
    report=dict(version=1,counts=counts,legacy_hashes_unchanged=len(legacy),actors=rows,
        resident_allocation=allocation,resident_total_bytes=total,limit_bytes=4*1024*1024,
        remaining_bytes=4*1024*1024-total,all_sfx_payload_bytes=sum(sfx),
        full_active_clip_four_cogs=4*max(cog_full),full_active_clip_four_toons=4*toon_full,
        policy='Two-frame TTA streaming; all gag props resident; up to four cached SFX; one streamed music ring. Inactive clips/LODs released.')
    write_json(DATA/'suit/battle_budget.json',report)
    print(json.dumps(dict(counts=counts,legacy_unchanged=len(legacy),resident_bytes=total,headroom=report['remaining_bytes']),indent=2))

if __name__=='__main__':main()
