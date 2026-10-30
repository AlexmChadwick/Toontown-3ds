"""Deterministic far LOD and conservative per-PVS model selection.

Near meshes are untouched. Far meshes omit decal layers and snap world positions
to a shared voxel lattice, removing collapsed and duplicate triangles. Keeping UVs
and colours per corner preserves material seams. Adjacent chunks use the same
lattice. Collision is never simplified. Runtime uses the same budgeted plan.
"""
import numpy as np
import ttm

TRI_BUDGET = 15000
# World meshes stop here. The frame still has a 150-draw ceiling; the remainder
# is toons, the boat and other actors. A view that fills 150 has no headroom.
DRAW_CAP = 130
# Chunks within `must` of the toon upgrade first against the whole frame ceiling
# (Quality::lodMust / Zone::kNearDrawBudget): the far mesh is bloated up close.
NEAR_DRAW_CAP = 150
LOW_TARGET = 12000


def simplify(batches, spacing):
    result=[]
    for b in batches:
        if b.layer:
            continue  # windows, letters and other overlaid details disappear only far away
        verts=b.verts[b.indices].copy()
        if spacing:
            verts[:,:3]=np.round(verts[:,:3]/spacing)*spacing
        tri=verts.reshape(-1,3,9)
        area=np.linalg.norm(np.cross(tri[:,1,:3]-tri[:,0,:3],tri[:,2,:3]-tri[:,0,:3]),axis=1)
        tri=tri[area>1e-6]
        if not len(tri):
            continue
        # Keep winding and attributes. Eliminate repeated faces after vertex collapse.
        keys=np.round(tri.reshape(len(tri),27),6)
        _,unique=np.unique(keys,axis=0,return_index=True)
        verts=tri[np.sort(unique)].reshape(-1,9)
        result.append(ttm.Batch(b.texture,b.blend,b.two_sided,b.tint,b.layer,verts,np.arange(len(verts))))
    return result


def count(batches):
    return sum(len(b.indices)//3 for b in batches)


def make_low(chunks, batches):
    before=max(sum(count(batches[j]) for j in c['vis']) for c in chunks)
    for step in (0,.25,.5,.75,1,1.5,2,3,4,6,8):
        low=[simplify(bs,step) for bs in batches]
        worst=max(sum(count(low[j]) for j in c['vis']) for c in chunks)
        draws=max(sum(len(low[j]) for j in c['vis']) for c in chunks)
        if worst<=LOW_TARGET and draws<=150:
            return low,{'voxel_spacing':step,'max_vertex_displacement':step*3**.5/2,
                        'before_worst_triangles':before,'low_worst_triangles':worst}
    raise ValueError(f'far LOD cannot meet budgets: {worst} triangles, {draws} draws')


def distance2(point,bounds):
    p=np.asarray(point)
    lo,hi=map(np.asarray,bounds)
    d=np.maximum(np.maximum(lo-p,p-hi),0)
    return float(d@d)


def plan(chunks,cell,point,near=65,must=0):
    """Low baseline, then nearest-first near upgrades, never removing a PVS cell.

    Mirrors Zone::update: chunks within `must` first (NEAR_DRAW_CAP), then the
    rest within `near` (DRAW_CAP)."""
    ids=chunks[cell]['vis']
    choice={i:1 for i in ids}
    keys=('triangles','batches','geometry_bytes','texture_bytes')
    limits=(TRI_BUDGET, DRAW_CAP, 6*1024*1024, 8*1024*1024)
    total=[sum(chunks[i]['low_stats'][k] for i in ids) for k in keys]
    if any(x>limit for x,limit in zip(total,limits)):
        raise ValueError('far baseline exceeds runtime budget')
    def d2(c):
        return distance2(point,(c.get('bounds') or c['stats']['bounds']))
    order=sorted(ids,key=lambda j:(d2(chunks[j]),j))
    for radius,cap in ((must,NEAR_DRAW_CAP),(near,DRAW_CAP)):
        if radius<=0:
            continue
        caps=(TRI_BUDGET,cap)+limits[2:]
        for i in order:
            c=chunks[i]
            if choice[i]==0 or d2(c)>radius*radius:
                continue
            trial=[value+c['stats'][k]-c['low_stats'][k] for value,k in zip(total,keys)]
            if all(x<=limit for x,limit in zip(trial,caps)):
                total=trial
                choice[i]=0
    return choice,dict(zip(keys,total))
