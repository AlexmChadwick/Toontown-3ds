"""Deterministic short-edge collapse; its linear map is reused for every pose."""
import numpy as np
from battle_common import ttm


def plan(batch,target):
    verts = batch.verts.copy()
    tris = batch.indices.reshape(-1,3).copy()
    weights = np.eye(len(verts))
    while len(tris) > target:
        edges = np.sort(np.concatenate((tris[:,[0,1]],tris[:,[1,2]],tris[:,[2,0]])),axis=1)
        edges = np.unique(edges,axis=0)
        delta = verts[edges[:,0]]-verts[edges[:,1]]
        cost = (delta[:,:3]**2).sum(axis=1)+.15*(delta[:,3:5]**2).sum(axis=1)+(delta[:,5:]**2).sum(axis=1)
        a,b = edges[int(np.argmin(cost))]
        verts[a] = (verts[a]+verts[b])*.5
        weights[a] = (weights[a]+weights[b])*.5
        tris[tris==b] = a
        tris = tris[(tris[:,0]!=tris[:,1])&(tris[:,1]!=tris[:,2])&(tris[:,2]!=tris[:,0])]
    used = np.unique(tris)
    assert len(used)>0, 'LOD removed an entire batch'
    result = ttm.Batch(batch.texture,batch.blend,batch.two_sided,batch.tint,batch.layer,
                       verts[used],np.searchsorted(used,tris).reshape(-1))
    result.tex_slot = getattr(batch,'tex_slot',0)
    return result,weights[used]


def build(batches,target):
    counts = [len(b.indices)//3 for b in batches]
    if sum(counts) <= target:
        return batches,[np.eye(len(b.verts)) for b in batches]
    targets = [max(1,int(n*target/sum(counts))) for n in counts]
    while sum(targets)>target:
        targets[int(np.argmax(targets))]-=1
    results = [plan(b,t) for b,t in zip(batches,targets)]
    return [r[0] for r in results],[r[1] for r in results]


def apply(batches,maps):
    return np.concatenate([m@b.verts[:,:3] for b,m in zip(batches,maps)])
