"""Deterministic far LOD and conservative per-PVS model selection.

Near meshes are untouched. Far meshes omit decal layers and collapse world positions
per voxel of a shared lattice (to the voxel's mean), removing collapsed and duplicate
triangles. Keeping UVs
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


# A triangle at least this upward (normal z) is ground for ground_spacing; ground_split
# uses the same test for what it splits.
GROUND_UP = 0.5


def cluster_points(batches, spacing, fixed=None, ground_spacing=None):
    """Vertex clustering of one chunk: the new position of every corner, batch by batch.

    Every corner in a voxel (round(p / spacing)) collapses to the mean of the distinct
    positions in it, not to the voxel centre. The merges are the same, so triangle
    counts match; the shape does not. A centre moved flat ground and rock tops up to
    half a voxel in height: at Acorn Acres' 16 unit far voxels the round rock behind
    the Donald's Dock tunnel became a spike, and the ground stepped. A mean stays on a
    flat or gently curved surface. `fixed` positions are not moved and do not count.

    With `ground_spacing`, corners whose every triangle faces up (ground and the middle
    of roofs: what ground_split cut into 20 unit pieces) use that coarser lattice; a
    corner on any wall or slope keeps `spacing`. Split ground filled the far budget,
    so Cashbot and Lawbot HQ walls collapsed at 48 units while their flat yards did not
    need the triangles.
    """
    corners=[b.verts[b.indices][:,:3] for b in batches]
    if not corners:
        return []
    pts=np.concatenate(corners).astype(np.float64)
    keys,first,inverse=np.unique(position_keys(pts),return_index=True,return_inverse=True)
    pos=pts[first]
    rep=pos.copy()
    free=~np.isin(keys,fixed) if fixed is not None and len(fixed) else np.ones(len(keys),bool)
    if free.any():
        step=np.full(len(keys),float(spacing))
        if ground_spacing:
            tri=pts.reshape(-1,3,3)
            n=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0])
            ln=np.linalg.norm(n,axis=1)
            up=np.where(ln>1e-8,n[:,2]/np.maximum(ln,1e-8),0.0)>=GROUND_UP
            ground=np.ones(len(keys),bool)
            np.logical_and.at(ground,inverse.ravel(),np.repeat(up,3))
            step[ground]=ground_spacing
        lattice=np.round(pos/step[:,None]).astype(np.int64)
        cells=np.concatenate([lattice,(step!=spacing)[:,None].astype(np.int64)],axis=1)[free]
        _,group=np.unique(cells,axis=0,return_inverse=True)
        group=group.ravel()
        sums=np.zeros((group.max()+1,3))
        np.add.at(sums,group,pos[free])
        rep[free]=(sums/np.bincount(group)[:,None])[group]
    moved=rep[inverse.ravel()]
    return np.split(moved,np.cumsum([len(c) for c in corners])[:-1])


def simplify(batches, spacing, keep_layers=False, fixed=None, ground=None):
    """Far mesh of one chunk at a voxel spacing.

    `fixed` (shared_positions) are seam corners that stay where they are. Snapped,
    a far chunk pulled its edge off its near neighbour's and the sky showed through
    the TTC hills on Old 3DS, which draws the far mesh from 160. Other corners
    collapse per voxel (cluster_points); `ground` is its ground_spacing.
    """
    result=[]
    snapped=[b for b in batches if not b.layer] if spacing else []
    moved=dict(zip(map(id,snapped),cluster_points(snapped,spacing,fixed,ground)))
    for b in batches:
        if b.layer and not keep_layers:
            continue  # windows, letters and other overlaid details disappear only far away
        verts=b.verts[b.indices].copy()
        # A kept decal is not snapped: snapped, it cuts through its wall at an angle and
        # z-fights. It is lifted off its collapsed wall instead (lift); the TTC tunnel signs were buried in their snapped rock.
        if spacing and not b.layer:
            verts[:,:3]=moved[id(b)]
            verts[:,5:9]=_merged_colors(b.verts[b.indices],verts)
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


def _merged_colors(before, after):
    """Vertex colours after a collapse: each merged corner takes the area-weighted mean
    of the corners that landed on it (in this batch, degenerate triangles included).

    Survivors otherwise keep only their own colours. Cashbot HQ's vault stairs bake
    their lighting into vertex colour (treads 1.0, risers 0.24 to 0.43); the risers
    collapsed and the far stairs were a white band across the entrance."""
    tri=before[:,:3].reshape(-1,3,3)
    area=np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)
    weight=np.repeat(area,3)[:,None]+1e-12
    _,group=np.unique(np.round(after[:,:3],6),axis=0,return_inverse=True)
    group=group.reshape(-1)
    total=np.zeros((group.max()+1,4))
    norm=np.zeros((group.max()+1,1))
    np.add.at(total,group,before[:,5:9]*weight)
    np.add.at(norm,group,weight)
    return total[group]/norm[group]


def lift(verts, spacing):
    """Move each triangle of unsnapped decal corners off its wall along the face normal.

    `verts` are non-indexed corners (3 per triangle). A voxel mean of a flat wall stays
    on its plane, and at an outside corner it moves into the building, away from the
    decal; a lift of half a voxel diagonal covers the rest. Past lodNear the gap does
    not show.
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
