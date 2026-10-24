#!/usr/bin/env python3
"""Bake Silly Street (2100) and TTC playground (2000) to self-contained .tz files.

.venv/bin/python -I tools/pipeline/bake_street.py [--zone 2100|2000|all] [--preview]
Outputs: sdmc-out/3ds/toontown3ds/data/zones/{2100,2000}.tz + budget JSON.
No source assets or existing pipeline files are modified. Use <=4 encoder threads.
"""
import argparse
import json
import math
import os
from pathlib import Path
import sys
import tempfile

os.environ['TT_ETC1_THREADS'] = '3'
os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
ROOT = HERE.parent.parent


def split_grid(batches, size):
    import numpy as np
    import ttm
    groups = {}
    for b in batches:
        triangles = b.verts[b.indices.reshape(-1,3)]
        cells = np.floor(triangles[:,:,:2].mean(axis=1)/size).astype(int)
        for key in sorted(set(map(tuple,cells))):
            verts = triangles[np.all(cells==key,axis=1)].reshape(-1,9)
            groups.setdefault(key,[]).append(ttm.Batch(b.texture,b.blend,b.two_sided,b.tint,b.layer,
                                                     verts,np.arange(len(verts))))
    return groups


def bake(zone_id, args, base):
    import numpy as np
    import assemble
    import collide
    import dna
    import street_atlas
    import ttm
    import tz
    resources = Path(args.resources)
    store = dna.Storage(base.loader, str(resources))
    storages = ['phase_4/dna/storage.dna','phase_4/dna/storage_TT.dna']
    if zone_id == 2100:
        storages += ['phase_5/dna/storage_town.dna','phase_5/dna/storage_TT_town.dna']
        src = 'phase_5/dna/toontown_central_2100.dna'
    else:
        storages += ['phase_4/dna/storage_TT_sz.dna']
        src = 'phase_4/dna/toontown_central_sz.dna'
    for s in storages:
        store.load(str(resources/s))
    warnings = []
    tree = dna.parse_file(resources/src,warnings)
    root,asm = assemble.assemble(tree,store,seed=1)
    ex = street_atlas.Extractor()
    chunks, raw, floors = [],[],[]
    whole_collision = collide.extract(root)
    collision, collision_stats = whole_collision.pack()
    if zone_id == 2100:
        nodes = sorted(root.findAllMatches('**/='+assemble.VISGROUP_TAG),
                       key=lambda n:int(n.getTag(assemble.VISGROUP_TAG)))
        indices = {n.getTag(assemble.VISGROUP_TAG):i for i,n in enumerate(nodes)}
        definitions = {n.name:n for n in tree.walk() if n.kind=='visgroup'}
        # Each visgroup is extracted in world coordinates, with inherited state.
        # Extractor.add(root, skip_under=...) preserves the root-space transforms.
        for i,n in enumerate(nodes):
            name=n.getTag(assemble.VISGROUP_TAG)
            ex._groups={}
            ex.add(root,skip_under=[other for other in nodes if other!=n])
            raw.append(ex.batches())
            visible = definitions[name].get('vis',[])
            if not isinstance(visible,list):
                visible=[visible]
            missing = [str(v) for v in visible if str(v) not in indices]
            if missing:
                raise ValueError(f'{name}: missing PVS cells {missing}')
            chunks.append({'name':name,'zone':int(name),'vis':sorted({i}|{indices[str(v)] for v in visible})})
            mesh=collide.extract(n,fallback_floor=False)
            for a,b,c,normal,flags in mesh.tris:
                if flags & collide.FLOOR and not flags & collide.TRIGGER and normal[2]>=0.707:
                    floors.append((np.array([a,b,c]),i,int(name)))
    else:
        ex.add(root)
        groups=split_grid(ex.batches(),args.grid_size)
        # Include grid cells crossed by floor triangles even when they own no render triangles.
        for a,b,c,normal,flags in whole_collision.tris:
            if flags & collide.FLOOR and not flags & collide.TRIGGER and normal[2]>=0.707:
                p=np.array([a,b,c])
                for cy in range(math.floor(p[:,1].min()/args.grid_size),math.floor(p[:,1].max()/args.grid_size)+1):
                    for cx in range(math.floor(p[:,0].min()/args.grid_size),math.floor(p[:,0].max()/args.grid_size)+1):
                        poly=list(np.pad(p,((0,0),(0,6))))
                        for axis,key in ((0,cx),(1,cy)):
                            poly=street_atlas.clip(street_atlas.clip(poly,axis,key*args.grid_size,True),axis,(key+1)*args.grid_size,False) if poly else []
                        if len(poly)<3:
                            continue
                        groups.setdefault((cx,cy),[])
        keys=sorted(groups)
        for i,key in enumerate(keys):
            raw.append(groups[key])
            chunks.append({'name':f'g{i}','zone':2000,'vis':[], 'grid':key})
        for a,b,c,normal,flags in whole_collision.tris:
            if flags & collide.FLOOR and not flags & collide.TRIGGER and normal[2]>=0.707:
                for i,(cx,cy) in enumerate(keys):
                    poly=list(np.pad(np.array([a,b,c]),((0,0),(0,6))))
                    for axis,key in ((0,cx),(1,cy)):
                        poly=street_atlas.clip(street_atlas.clip(poly,axis,key*args.grid_size,True),axis,(key+1)*args.grid_size,False) if poly else []
                    for j in range(1,len(poly)-1):
                        tri=np.array([poly[k][:3] for k in (0,j,j+1)])
                        if np.linalg.norm(np.cross(tri[1]-tri[0],tri[2]-tri[0]))>1e-6:
                            floors.append((tri,i,2000))
        floors.sort(key=lambda f:f[1])
    textures, batches=street_atlas.build(ex.sources,raw)
    out=Path(args.out)
    out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for i,(chunk,bs) in enumerate(zip(chunks,batches)):
            if bs:
                used=sorted({b.texture for b in bs if b.texture is not None})
                mapping={old:new for new,old in enumerate(used)}
                for b in bs:
                    if b.texture is not None:
                        b.texture=mapping[b.texture]
                path=Path(tmp)/f'{i}.ttm'
                chunk['stats']=ttm.write(path,[textures[j] for j in used],bs)
                if chunk['stats']['uv_clamped']:
                    raise ValueError('UV overflow')
                chunk['model']=path.read_bytes()
            else:
                chunk['stats']={'batches':0,'triangles':0,'geometry_bytes':0,'texture_bytes':0,'bounds':[[0,0,0],[0,0,0]]}
                chunk['model']=b''
    if zone_id==2000:
        for i,c in enumerate(chunks):
            cx,cy=c['grid']
            lo=np.array([cx,cy])*args.grid_size
            hi=lo+args.grid_size
            for j,d in enumerate(chunks):
                bmin,bmax=map(np.array,d['stats']['bounds'])
                # Any camera within this grid cell, plus 30-unit camera orbit margin.
                dist=np.maximum(np.maximum(bmin[:2]-hi,lo-bmax[:2]),0)
                if i==j or np.linalg.norm(dist)<=args.distance+30:
                    c['vis'].append(j)
    triggers=[]
    for t in whole_collision.triggers:
        tunnel='tunnel' in (t.name+' '+t.owner).lower()
        triggers.append({'min':list(t.lo),'max':list(t.hi),'kind':2 if tunnel else 1 if 'door' in t.name.lower() else 0,
                         'name':t.name,'owner':t.owner})
    # Tunnel exit anchors have no trigger in some BAMs. Small named approach volumes
    # let the integration queue a transition; actual teleporting remains game policy.
    for n in root.findAllMatches('**/linktunnel_*'):
        p=np.array(n.getPos(root))
        triggers.append({'min':list(p-[6,6,3]),'max':list(p+[6,6,8]),'kind':2,
                         'name':n.getName(),'owner':n.getParent().getName()})
    report=tz.write(out/f'{zone_id}.tz',zone_id,chunks,floors,triggers,collision)
    report.update({'zone':zone_id,'floor_triangles':len(floors),'triggers':len(triggers),
                   'file_bytes':(out/f'{zone_id}.tz').stat().st_size,'storage_errors':store.errors,
                   'unresolved_codes':dict(asm.unresolved),'skipped':dict(asm.skipped),
                   'warnings':warnings,'collision':collision_stats,'atlas_pages':len(textures)})
    (out/f'{zone_id}.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('visible_sets','collision')},indent=2),flush=True)
    root.removeNode()
    return out/f'{zone_id}.tz'


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--zone',choices=('2100','2000','all'),default='all')
    ap.add_argument('--resources',default=str(ROOT/'ref/resources'))
    ap.add_argument('--out',default=str(ROOT/'sdmc-out/3ds/toontown3ds/data/zones'))
    ap.add_argument('--grid-size',type=float,default=128)
    ap.add_argument('--distance',type=float,default=120)
    ap.add_argument('--preview',action='store_true')
    args=ap.parse_args()
    if args.grid_size<=0 or args.distance<=0:
        ap.error('grid size and distance must be positive')
    from panda3d.core import loadPrcFileData
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\ntextures-power-2 none\nmodel-cache-dir')
    from direct.showbase.ShowBase import ShowBase
    base=ShowBase()
    paths=[bake(z,args,base) for z in ((2100,2000) if args.zone=='all' else (int(args.zone),))]
    base.destroy()
    if args.preview:
        import subprocess
        for path in paths:
            subprocess.run([sys.executable,'-I',str(HERE/'street_preview.py'),str(path),
                            str(ROOT/'sdmc-out/previews/streets')],check=True)


if __name__=='__main__':
    main()
