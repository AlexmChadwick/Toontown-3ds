"""Shared offline battle helpers; never modify shared references or runtime formats."""
import sys
sys.dont_write_bytecode = True
import os
for name in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','VECLIB_MAXIMUM_THREADS'):
    os.environ[name] = '1'
import ast
import json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import numpy as np
from panda3d.core import BamCache, loadPrcFileData
BamCache.getGlobalPtr().setActive(False)
ROOT = Path(__file__).resolve().parents[2]
REF = ROOT/'ref'
if not (REF/'resources/phase_3').is_dir():
    REF = REF/'ref'
RES = REF/'resources'
UP = REF/'open-toontown'
DATA = ROOT/'sdmc-out/3ds/toontown3ds/data'
loadPrcFileData('',f'window-type none\naudio-library-name null\nnotify-level warning\nmodel-cache-models false\nmodel-cache-textures false\nmodel-path {RES}\ndefault-model-extension .bam')
import extract
import ttm
import tta


def literal_table(path,name):
    tree = ast.parse((UP/path).read_text())
    return ast.literal_eval(next(n.value for n in tree.body if isinstance(n,ast.Assign)
        and any(isinstance(t,ast.Name) and t.id == name for t in n.targets)))


def write_json(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,indent=2,sort_keys=True)+'\n')


def compact(batch):
    used = np.unique(batch.indices)
    batch.verts = batch.verts[used]
    batch.indices = np.searchsorted(used,batch.indices)
    return batch


class StableExtractor(extract.Extractor):
    """Stable joint-name skinning avoids pointer-ordered float32 blend sums."""
    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs)
        self.skin_cache = {}
        self.matrix_cache = {}
        self.was_animated = False

    def skin(self,data):
        from panda3d.core import GeomVertexData,GeomVertexReader,GeomVertexWriter,LMatrix4f
        key = int(data.this)
        if key in self.skin_cache:
            return self.skin_cache[key]
        table = data.getTransformBlendTable()
        if table is None or not table.getNumBlends():
            return data
        assert data.hasColumn('transform_blend')
        slider = data.getSliderTable()
        assert slider is None or slider.getNumSliders() == 0
        rv,rb = GeomVertexReader(data,'vertex'),GeomVertexReader(data,'transform_blend')
        n = data.getNumRows()
        source = np.array([tuple(rv.getData3()) for _ in range(n)])
        blends = np.array([rb.getData1i() for _ in range(n)])
        positions = np.empty_like(source)
        for index in np.unique(blends):
            blend = table.getBlend(int(index))
            terms = []
            for i in range(blend.getNumTransforms()):
                transform = blend.getTransform(i)
                key_t = int(transform.this)
                if key_t not in self.matrix_cache:
                    matrix = LMatrix4f()
                    transform.getMatrix(matrix)
                    self.matrix_cache[key_t] = (transform.getJoint().getName(),
                        np.array([[matrix.getCell(r,c) for c in range(4)] for r in range(4)]))
                name,matrix = self.matrix_cache[key_t]
                terms.append((name,blend.getWeight(i),matrix))
            matrix = sum(w*m for _,w,m in sorted(terms,key=lambda x:x[0]))
            mask = blends == index
            positions[mask] = source[mask]@matrix[:3,:3]+matrix[3,:3]
        result = GeomVertexData(data)
        writer = GeomVertexWriter(result,'vertex')
        for vertex in positions:
            writer.setData3(*vertex)
        self.skin_cache[key] = result
        return result

    def _add_geom(self,geom,state,mat,tint,layer,animated):
        if animated:
            result = geom.makeCopy()
            result.setVertexData(self.skin(geom.getVertexData()))
            geom = result
        before = {key:len(parts) for key,parts in self._groups.items()}
        super()._add_geom(geom,state,mat,tint,layer,False)
        if animated:
            self.was_animated = True
            if mat.getUpper3().determinant() < 0:
                # Animated squash can cross zero scale. Keep source winding
                # stable and render these morph batches two-sided throughout.
                for key,parts in self._groups.items():
                    if len(parts)>before.get(key,0):
                        verts,idx=parts[-1]
                        parts[-1]=(verts,idx.reshape(-1,3)[:,[0,2,1]].reshape(-1))

    def batches(self):
        batches = super().batches()
        if self.was_animated:
            for batch in batches:
                batch.two_sided = True
        return batches

    def _texture(self,tex):
        from panda3d.core import Texture
        if self.encode_textures and tex.getFormat() == Texture.F_alpha:
            key = tex.getFullpath().getFullpath() or tex.getName()
            if key not in self._tex_index:
                px,_,_ = extract.texture_pixels(tex,self.max_tex)
                px[:,:,3] = px[:,:,0]
                px[:,:,:3] = 255
                self._tex_index[key] = len(self.textures)
                self.textures.append(ttm.Texture(tex.getName(),px,ttm.GPU_A8))
            return self._tex_index[key]
        return super()._texture(tex)


def capture(root,encode=False,tint_rules=(),animated=True,max_tex=64):
    ex = StableExtractor(max_tex=max_tex,tint_rules=tint_rules,encode_textures=encode)
    ex.add(root,animated=animated)
    return ex,[compact(b) for b in ex.batches()]


def check_order(batches,reference):
    assert len(batches) == len(reference)
    for b,r in zip(batches,reference):
        assert (b.texture,b.blend,b.two_sided,b.tint,b.layer) == (r.texture,r.blend,r.two_sided,r.tint,r.layer)
        assert b.verts.shape == r.verts.shape
        assert np.array_equal(b.indices,r.indices), 'animation index order changed'
        assert np.array_equal(b.verts[:,3:],r.verts[:,3:]), 'animation UV/color order changed'


def mat34(node,root):
    m = node.getMat(root)
    return np.array([[m.getCell(c,r) for c in range(4)] for r in range(3)])


def write_joints(path,names,frames):
    """JNT1: 32B header, 32B UTF-8 names; aligned float32 F,J,3,4 tracks."""
    import struct
    array = np.asarray(frames,dtype='<f4').reshape(len(frames),len(names),3,4)
    offset = ttm._align(32+32*len(names))
    out = bytearray(offset+array.nbytes)
    struct.pack_into('<4sHHIfIIII',out,0,b'JNT1',1,len(names),len(frames),12.0,offset,len(names)*48,32,0)
    for i,name in enumerate(names):
        out[32+i*32:64+i*32] = name.encode()[:31].ljust(32,b'\0')
    out[offset:] = array.tobytes()
    path.write_bytes(out)


def bake_actor(base,out,stem,model,clips,targets=None,prepare=None,tint_rules=(),head_joint=None):
    import math
    from direct.actor.Actor import Actor
    import suit_lod
    actor = Actor(model,clips)
    if prepare:
        prepare(actor)
    first = 'neutral' if 'neutral' in clips else next(iter(clips))
    actor.pose(first,0)
    actor.update(force=True)
    ex,reference = capture(actor,True,tint_rules)
    for batch in reference:
        if batch.tint in (1,2,3):
            batch.tex_slot = batch.tint
            batch.tint = 0
    # Frame extraction still uses the pre-slot tint keys for identity validation.
    _,identity = capture(actor,False,tint_rules)
    child = actor.find('**/'+head_joint) if head_joint else None
    if head_joint:
        assert not child.isEmpty(), (model,head_joint)
    joint_names = [name for name in ('joint_Lhold','joint_Rhold','joint_shadow')
                   if not actor.find('**/'+name).isEmpty()]
    joints = [actor.find('**/'+n) for n in joint_names]
    results,plans = {},{}
    for lod,target in (targets or {'full':1500}).items():
        batches,maps = suit_lod.build(reference,target)
        stats = ttm.write(out/f'{stem}_{lod}.ttm',ex.textures,batches)
        assert stats['vertices'] == sum(len(b.verts) for b in batches) and not stats['uv_clamped']
        assert stats['triangles'] <= target
        results[lod] = dict(file=f'{stem}_{lod}.ttm',source_model=model,**stats,clips={})
        plans[lod] = maps
    for clip in clips:
        control = actor.getAnimControl(clip)
        assert control is not None, (model,clip,clips[clip])
        nf,rate = control.getNumFrames(),control.getFrameRate()
        count = max(1,math.ceil(nf/rate*12-1e-8))
        frames = {lod:[] for lod in results}
        attach,extra = [],[]
        cache = {}
        def pose(index):
            if index not in cache:
                actor.pose(clip,index)
                actor.update(force=True)
                _,batches = capture(actor,False,tint_rules)
                check_order(batches,identity)
                cache[index] = (batches,mat34(child,actor) if child else None,
                                np.array([mat34(j,actor) for j in joints]))
            return cache[index]
        for i in range(count):
            src = min(i*rate/12,nf-1)
            a,b = int(src),min(int(src)+1,nf-1)
            f = src-a
            ba,ha,ja = pose(a)
            bb,hb,jb = pose(b)
            for lod,maps in plans.items():
                frames[lod].append(suit_lod.apply(ba,maps)*(1-f)+suit_lod.apply(bb,maps)*f)
            if child:
                attach.append(ha*(1-f)+hb*f)
            if joints:
                extra.append(ja*(1-f)+jb*f)
        for lod,entry in results.items():
            filename = f'{stem}_{lod}_{clip}.tta'
            stats = tta.write(out/filename,frames[lod],12,attach if child else None)
            entry['clips'][clip] = dict(file=filename,source=clips[clip],fps=12,**stats)
            if joints:
                write_joints(out/f'{stem}_{lod}_{clip}.jnt',joint_names,extra)
        print(stem,clip,count,'frames',flush=True)
    actor.cleanup()
    actor.removeNode()
    return results
