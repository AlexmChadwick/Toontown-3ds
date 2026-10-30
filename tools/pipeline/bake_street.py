#!/usr/bin/env python3
"""Bake all supported streets and playgrounds to self-contained .tz files.

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
sys.dont_write_bytecode = True
import tempfile

os.environ['TT_ETC1_THREADS'] = '3'
os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
os.environ['VECLIB_MAXIMUM_THREADS'] = '1'
os.environ['MKL_NUM_THREADS'] = '1'
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
ROOT = HERE.parent.parent


# Near atlas matches source/render/quality.h: sharp ground and props up close.
# The far LOD is a coarse vertex-coloured mesh (no texture copies), so a long PVS
# still fits the 8 MiB texture budget. Playground vis (--distance 700) plus the
# 38-unit margin covers the New 3DS draw distance; Old 3DS loads less of it.
NEAR_ATLAS = dict(tile=64, gutter=8, standalone_size=256, mip_levels=3, large_ground=True)
OLD_LOD_NEAR = 160.0
NEW_LOD_NEAR = 260.0


def _far_draws(batches):
    """Draws after far batches that share blend and cull are merged."""
    return len({(b.blend, b.two_sided) for b in batches})


def _colorize_far(sources, chunks):
    """Sample each far triangle's texture into its vertex colour and drop the page.

    Far chunks then cost geometry only. The sample is the texel at the corner, at
    32px, which is enough once fog has started. Decals are already gone.
    """
    import numpy as np
    import street_atlas
    import ttm
    cache = {}
    result = []
    for batches in chunks:
        groups = {}
        for b in batches:
            if b.indices is None or len(b.indices) < 3:
                continue
            verts = np.asarray(b.verts, dtype=np.float64)[np.asarray(b.indices, dtype=np.int64)]
            verts = verts.reshape(-1, 9).copy()
            if b.texture is not None:
                image = cache.get(b.texture)
                if image is None:
                    image, _, _ = street_atlas.texture_pixels(sources[b.texture], 32)
                    cache[b.texture] = image
                height, width = image.shape[:2]
                u = np.mod(verts[:, 3], 1.0)
                v = np.mod(verts[:, 4], 1.0)
                x = np.clip((u * (width - 1)).astype(np.intp), 0, width - 1)
                y = np.clip(((1.0 - v) * (height - 1)).astype(np.intp), 0, height - 1)
                verts[:, 5:9] *= image[y, x].astype(np.float64) / 255.0
            verts[:, 3:5] = 0.0
            np.clip(verts[:, 5:9], 0.0, 1.0, out=verts[:, 5:9])
            groups.setdefault((b.blend, bool(b.two_sided)), []).append(verts)
        merged = []
        for (blend, two), parts in sorted(groups.items(), key=lambda kv: (kv[0][0], kv[0][1])):
            block = np.concatenate(parts)
            for start in range(0, len(block), ttm.MAX_BATCH_VERTS):
                vs = block[start:start + ttm.MAX_BATCH_VERTS]
                merged.append(ttm.Batch(None, blend, two, 0, 0, vs, np.arange(len(vs), dtype=np.int64)))
        result.append(merged)
    return result


def choose_far(sources, raw, chunks):
    """Voxel-simplify, then bake vertex colours, until every PVS fits the far budget.

    Spacing climbs until 48 units so a whole playground still fits. Far objects
    are allowed to look rough. Textures are not encoded for this LOD.
    """
    import street_lod
    steps = (0, .25, .5, .75, 1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48)
    def worst_of(low):
        tris = max((sum(street_lod.count(low[j]) for j in c['vis']) for c in chunks), default=0)
        draws = max((sum(_far_draws(low[j]) for j in c['vis']) for c in chunks), default=0)
        return tris, draws
    before, _ = worst_of(raw)
    worst = draws = None
    for step in steps:
        low = [street_lod.simplify(bs, step) for bs in raw]
        worst, draws = worst_of(low)
        if worst <= street_lod.LOW_TARGET and draws <= 150:
            colored = _colorize_far(sources, low)
            return colored, {'voxel_spacing': step, 'max_vertex_displacement': step * 3 ** .5 / 2,
                             'before_worst_triangles': before, 'low_worst_triangles': worst,
                             'low_worst_draws': draws, 'textured': False}
    raise ValueError(f'far LOD cannot meet budgets: {worst} triangles, {draws} draws')


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
    import street_catalog
    import extract
    import street_lod
    resources = Path(args.resources)
    if not (resources / 'phase_4/dna/storage.dna').exists():
        resources = ROOT / 'ref/ref/resources'
    store = dna.Storage(base.loader, str(resources))
    playground,storages,src=street_catalog.paths(zone_id)
    for s in storages:
        store.load(str(resources/s))
    warnings = []
    tree = dna.parse_file(resources/src,warnings)
    root,asm = assemble.assemble(tree,store,seed=1)
    if args.separate_boat:
        # Donald's Dock boat is the moving DistributedBoat (bake_boat.py). Stashed nodes are
        # skipped by findAllMatches, so neither its geometry nor its collision reach the .tz.
        for n in root.findAllMatches('**/donalds_boat'):
            n.stash()
    import bake_moving_props
    movers = bake_moving_props.split(root, zone_id, args.out)
    if args.points_only:  # refresh the .pts anchors without re-encoding the zone
        from bake_zone import write_points
        num_points = write_points(root, str(Path(args.out) / f'{zone_id}.pts'))
        num_points += bake_moving_props.append_points(Path(args.out) / f'{zone_id}.pts', movers)
        print(f'{zone_id}: {num_points} points', flush=True)
        return {'zone': zone_id, 'points': num_points}
    ex = street_atlas.Extractor()
    chunks, raw, floors = [],[],[]
    ground_stats = {'triangles_in': 0, 'triangles_out': 0, 'split': 0}
    whole_collision = collide.extract(root)
    collision, collision_stats = whole_collision.pack()
    if not playground:
        nodes = sorted(root.findAllMatches('**/='+assemble.VISGROUP_TAG),
                       key=lambda n:int(n.getTag(assemble.VISGROUP_TAG)))
        indices = {n.getTag(assemble.VISGROUP_TAG):i for i,n in enumerate(nodes)}
        definitions = {n.name:n for n in tree.walk() if n.kind=='visgroup'}
        # Extract each subtree once; visgroup transforms are applied below.
        for i,n in enumerate(nodes):
            name=n.getTag(assemble.VISGROUP_TAG)
            ex._groups={}
            ex.add(n)
            bs=ex.batches()
            mat=n.getMat(root)
            if not mat.isIdentity():
                for b in bs:
                    b.verts[:,:3]=[tuple(mat.xformPoint(tuple(p))) for p in b.verts[:,:3]]
            bs, part = extract.subdivide_ground(bs)
            for key in ground_stats:
                ground_stats[key] += part[key]
            raw.append(bs)
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
        split_src, part = extract.subdivide_ground(ex.batches())
        for key in ground_stats:
            ground_stats[key] += part[key]
        groups=split_grid(split_src,args.grid_size)
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
            chunks.append({'name':f'g{i}','zone':zone_id,'vis':[], 'grid':key})
        for a,b,c,normal,flags in whole_collision.tris:
            if flags & collide.FLOOR and not flags & collide.TRIGGER and normal[2]>=0.707:
                for i,(cx,cy) in enumerate(keys):
                    poly=list(np.pad(np.array([a,b,c]),((0,0),(0,6))))
                    for axis,key in ((0,cx),(1,cy)):
                        poly=street_atlas.clip(street_atlas.clip(poly,axis,key*args.grid_size,True),axis,(key+1)*args.grid_size,False) if poly else []
                    for j in range(1,len(poly)-1):
                        tri=np.array([poly[k][:3] for k in (0,j,j+1)])
                        if np.linalg.norm(np.cross(tri[1]-tri[0],tri[2]-tri[0]))>1e-6:
                            floors.append((tri,i,zone_id))
        floors.sort(key=lambda f:f[1])
    textures, batches=street_atlas.build(ex.sources, raw, **NEAR_ATLAS)
    batches=[[street_atlas.rebase_large_uvs(b,textures) for b in bs] for bs in batches]
    out=Path(args.out)
    out.mkdir(parents=True,exist_ok=True)
    def pack(bs,tmp,tex_list):
        if not bs:
            return b'',{'batches':0,'triangles':0,'geometry_bytes':0,'texture_bytes':0,'bounds':[[0,0,0],[0,0,0]]}
        used=sorted({b.texture for b in bs if b.texture is not None})
        mapping={old:new for new,old in enumerate(used)}
        copies=[ttm.Batch(mapping[b.texture] if b.texture is not None else None,
                          b.blend,b.two_sided,b.tint,b.layer,b.verts,b.indices) for b in bs]
        path=Path(tmp)/'chunk.ttm'
        stats=ttm.write(path,[tex_list[j] for j in used],copies)
        if stats['uv_clamped']:
            raise ValueError('UV overflow')
        return path.read_bytes(),stats
    with tempfile.TemporaryDirectory() as tmp:
        for chunk,bs in zip(chunks,batches):
            chunk['model'],chunk['stats']=pack(bs,tmp,textures)
    if playground:
        for i,c in enumerate(chunks):
            cx,cy=c['grid']
            lo=np.array([cx,cy])*args.grid_size
            hi=lo+args.grid_size
            for j,d in enumerate(chunks):
                bmin,bmax=map(np.array,d['stats']['bounds'])
                # Any camera within this grid cell, plus 30-unit camera orbit and 8-unit maximum LOD displacement margin.
                dist=np.maximum(np.maximum(bmin[:2]-hi,lo-bmax[:2]),0)
                # 30-unit camera orbit plus up to 48 units of far-mesh snap.
                if i==j or np.linalg.norm(dist)<=args.distance+78:
                    c['vis'].append(j)
    low, lod_report = choose_far(ex.sources, raw, chunks)
    with tempfile.TemporaryDirectory() as tmp:
        for chunk,bs in zip(chunks,low):
            # No texture table: the far mesh is vertex coloured. pack() still
            # accepts a texture list for the near chunks above.
            chunk['low_model'],chunk['low_stats']=pack(bs,tmp,[])
            if chunk['low_model']:
                chunk['stats']['bounds']=[np.minimum(chunk['stats']['bounds'][0],chunk['low_stats']['bounds'][0]).tolist(),
                                          np.maximum(chunk['stats']['bounds'][1],chunk['low_stats']['bounds'][1]).tolist()]
    before=tz.report(chunks)
    low_chunks=[dict(c,stats=c['low_stats']) for c in chunks]
    low_report=tz.report(low_chunks)
    samples=[]
    for i,c in enumerate(chunks):
        points=[np.mean(f[0],axis=0) for f in floors if f[1]==i]
        if not points:
            points=[np.mean(c['stats']['bounds'],axis=0)]
        # Floor centroids span each cell; every plan is hard capped independently.
        for p in points[::max(1,len(points)//8)]:
            for near in (OLD_LOD_NEAR, NEW_LOD_NEAR):
                _,metrics=street_lod.plan(chunks,i,p,near=near)
                samples.append(metrics)
    planned={k:max(row[k] for row in samples) for k in ('batches','triangles','geometry_bytes','texture_bytes')}
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
    sign_views=[]
    for n in root.findAllMatches('**/=sign_baseline'):
        letters=n.getTag('letters')
        bounds=n.getTightBounds(root)
        if not letters.strip() or bounds is None:
            continue
        lo,hi=map(np.array,bounds)
        centre=(lo+hi)/2
        normal=np.array(n.getMat(root).xformVec((0,-1,0)))
        normal/=max(float(np.linalg.norm(normal)),1e-8)
        eye=centre+normal*max(8,float(np.linalg.norm(hi-lo))*1.2)
        sign_views.append({'letters':letters,'look':centre.tolist(),'eye':eye.tolist()})
    from bake_zone import write_points
    num_points=write_points(root,str(out/f'{zone_id}.pts'))
    num_points += bake_moving_props.append_points(out/f'{zone_id}.pts', movers)
    report.update({'moving_props':movers,'version':3,'before':before['worst'],'low':low_report['worst'],'planned_worst':planned,
                   'lod':lod_report,'sign_views':sign_views,'points':num_points,'sign_baselines':asm.sign_baselines,'sign_letters':asm.sign_letters,'zone':zone_id,'floor_triangles':len(floors),'triggers':len(triggers),
                   'file_bytes':(out/f'{zone_id}.tz').stat().st_size,'storage_errors':store.errors,
                   'unresolved_codes':dict(asm.unresolved),'skipped':dict(asm.skipped),
                   'warnings':warnings,'collision':collision_stats,'atlas_pages':len(textures),
                   'near_atlas':NEAR_ATLAS,'ground_split':ground_stats})
    (out/f'{zone_id}.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('visible_sets','collision','sign_views')},indent=2),flush=True)
    root.removeNode()
    return out/f'{zone_id}.tz'


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--zone',default='all',help='all or comma-separated supported zone ids')
    ap.add_argument('--resources',default=str(ROOT/'ref/resources'))
    ap.add_argument('--out',default=str(ROOT/'sdmc-out/3ds/toontown3ds/data/zones'))
    ap.add_argument('--grid-size',type=float,default=128)
    ap.add_argument('--distance',type=float,default=760,
                    help='playground cell vis radius (plus 78 covers the New 3DS draw; Old loads less)')
    ap.add_argument('--preview',action='store_true')
    ap.add_argument('--points-only',action='store_true',help='only rewrite the .pts anchor sidecars')
    ap.add_argument('--separate-boat',action='store_true',
                    help="leave donalds_boat out of zone 1000 (bake_boat.py bakes it as 1000_boat.ttm)")
    args=ap.parse_args()
    if args.grid_size<=0 or args.distance<=0:
        ap.error('grid size and distance must be positive')
    if args.zone=='all' or ',' in args.zone:
        import subprocess
        import street_catalog
        requested=street_catalog.ZONES if args.zone=='all' else tuple(int(z) for z in args.zone.split(','))
        for z in requested:
            command=[sys.executable,'-I',str(Path(__file__).resolve()),'--zone',str(z),
                     '--resources',args.resources,'--out',args.out,'--grid-size',str(args.grid_size),
                     '--distance',str(args.distance)]
            if args.preview:
                command.append('--preview')
            if args.separate_boat:
                command.append('--separate-boat')
            if args.points_only:
                command.append('--points-only')
            subprocess.run(command,check=True)
        return
    from panda3d.core import loadPrcFileData, BamCache
    BamCache.getGlobalPtr().setActive(False)
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\ntextures-power-2 none\nmodel-cache-dir')
    from direct.showbase.ShowBase import ShowBase
    base=ShowBase()
    import street_catalog
    zones=street_catalog.ZONES if args.zone=='all' else tuple(int(z) for z in args.zone.split(','))
    paths=[]
    for z in zones:
        print(f'Baking zone {z}',flush=True)
        paths.append(bake(z,args,base))
    base.destroy()
    if args.preview:
        import subprocess
        for path in paths:
            subprocess.run([sys.executable,'-I',str(HERE/'street_preview.py'),str(path),
                            str(ROOT/'sdmc-out/previews/streets')],check=True)


if __name__=='__main__':
    main()
