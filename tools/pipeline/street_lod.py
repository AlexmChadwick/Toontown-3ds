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
# Chunks within `must` of the toon are considered first. They use the same 130-draw
# cap as the rest of the cell. A 150-draw near envelope counted chunks behind the
# camera and reached 147-149 draws in Donald's Dock.
NEAR_DRAW_CAP = DRAW_CAP
LOW_TARGET = 12000


def position_keys(points):
    """Exact-match keys for world positions (1/1024 unit), one void scalar per row."""
    q=np.ascontiguousarray(np.round(np.asarray(points,dtype=np.float64)[:,:3]*1024.0).astype(np.int64))
    return q.view(np.dtype((np.void,24))).ravel()


def shared_positions(chunks):
    """Keys of the vertex positions used by more than one chunk's batches.

    Chunks own whole triangles (split_grid bins them by centroid), so the seam
    between two chunks is a run of original edges whose corners both chunks use.
    """
    per=[np.unique(position_keys(np.concatenate([b.verts[b.indices] for b in bs])))
         for bs in chunks if any(len(b.indices) for b in bs)]
    if len(per)<2:
        return per[0][:0] if per else position_keys(np.zeros((0,3)))
    keys,counts=np.unique(np.concatenate(per),return_counts=True)
    return keys[counts>1]


def simplify(batches, spacing, keep_layers=False, fixed=None):
    """Far mesh of one chunk at a voxel spacing.

    `fixed` (shared_positions) are seam corners that stay where they are. Snapped,
    a far chunk pulled its edge off its near neighbour's and the sky showed through
    the TTC hills on Old 3DS, which draws the far mesh from 160.
    """
    result=[]
    for b in batches:
        if b.layer and not keep_layers:
            continue  # windows, letters and other overlaid details disappear only far away
        verts=b.verts[b.indices].copy()
        # A kept decal is not snapped: snapped, it cuts through its wall at an angle and
        # z-fights. Its wall moves up to half a voxel diagonal, so the decal is lifted by
        # that much instead; the TTC tunnel signs were buried in their snapped rock.
        if spacing and not b.layer:
            snapped=np.round(verts[:,:3]/spacing)*spacing
            if fixed is not None and len(fixed):
                keep=np.isin(position_keys(verts),fixed)
                snapped[keep]=verts[keep,:3]
            verts[:,:3]=snapped
        elif spacing:
            verts=lift(verts,spacing)
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


def lift(verts, spacing):
    """Move each triangle of unsnapped decal corners off its wall along the face normal.

    `verts` are non-indexed corners (3 per triangle). The snapped wall behind moves at
    most spacing*sqrt(3)/2, so a decal lifted past that cannot fall behind it. Past
    lodNear the gap does not show.
    """
    out=np.asarray(verts,dtype=np.float64).copy()
    tri=out[:,:3].reshape(-1,3,3)
    n=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0])
    length=np.linalg.norm(n,axis=1,keepdims=True)
    n=np.where(length>1e-9,n/np.maximum(length,1e-9),0.0)
    tri+=n[:,None,:]*(spacing*3**.5/2+0.05)
    out[:,:3]=tri.reshape(-1,3)
    return out


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

    Mirrors Zone::update with no camera: chunks within `must` first, then the rest
    within `near`. Both passes stop at DRAW_CAP. A camera at runtime counts only
    the submitted view, which is a tighter set than this whole-cell plan."""
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
