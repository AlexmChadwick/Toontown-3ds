#!/usr/bin/env python3
"""Bake the pinned classic-character tables. Run with .venv/bin/python -I.

Default: every mid/lowest LOD and every AnimDict clip, exact 12 Hz, no threads.
--characters accepts CharDNA codes for a focused rebake. Outputs are SD artifacts.
"""
import sys
sys.dont_write_bytecode = True
import os
for _key in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','VECLIB_MAXIMUM_THREADS'):
    os.environ[_key] = '1'
import argparse
import json
import math
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import numpy as np
from panda3d.core import (CharacterJointEffect, GeomVertexData, GeomVertexReader, GeomVertexWriter,
                          LMatrix4f, ModelNode, Texture, TransparencyAttrib, loadPrcFileData)
from char_source import ROOT, RESOURCES, NAMES, HEIGHTS, tables
loadPrcFileData('', f'window-type none\naudio-library-name null\nnotify-level warning\nmodel-cache-models false\nmodel-cache-textures false\nmodel-path {RESOURCES}\ndefault-model-extension .bam')
from direct.actor.Actor import Actor
from direct.showbase.ShowBase import ShowBase
import extract
import ttm
import tta
import char_paths

LAYERS = [('*pupil*',2), ('eyes*',1)]

class CharExtractor(extract.Extractor):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._skin_cache = {}
        self._matrix_cache = {}

    def _skin(self, data):
        key = int(data.this)
        if key in self._skin_cache:
            return self._skin_cache[key]
        table = data.getTransformBlendTable()
        if table is None or not table.getNumBlends():
            return data
        assert data.hasColumn('transform_blend')
        slider = data.getSliderTable()
        assert slider is None or slider.getNumSliders() == 0, 'unsupported morph sliders'
        rv = GeomVertexReader(data,'vertex')
        rb = GeomVertexReader(data,'transform_blend')
        n = data.getNumRows()
        source = np.array([tuple(rv.getData3()) for _ in range(n)],dtype=np.float64)
        blends = np.array([rb.getData1i() for _ in range(n)])
        skinned = np.empty_like(source)
        for index in np.unique(blends):
            blend = table.getBlend(int(index))
            transforms = []
            for i in range(blend.getNumTransforms()):
                transform = blend.getTransform(i)
                transform_key = int(transform.this)
                if transform_key not in self._matrix_cache:
                    matrix = LMatrix4f()
                    transform.getMatrix(matrix)
                    assert hasattr(transform,'getJoint'), 'unexpected vertex transform'
                    self._matrix_cache[transform_key] = (transform.getJoint().getName(),
                        np.array([[matrix.getCell(r,c) for c in range(4)] for r in range(4)]))
                name,matrix = self._matrix_cache[transform_key]
                transforms.append((name,blend.getWeight(i),matrix))
            matrix = sum(weight*m for _,weight,m in sorted(transforms,key=lambda t:t[0]))
            mask = blends == index
            skinned[mask] = source[mask] @ matrix[:3,:3] + matrix[3,:3]
        copy = GeomVertexData(data)
        writer = GeomVertexWriter(copy,'vertex')
        for vertex in skinned:
            writer.setData3(*vertex)
        self._skin_cache[key] = copy
        return copy

    def _add_geom(self, geom, state, mat, tint, layer, animated):
        # Panda sums float matrices in pointer order, varying between processes.
        # Sum named joints in stable order in float64. This cache lasts one pose.
        if animated:
            data = self._skin(geom.getVertexData())
            geom = geom.makeCopy()
            geom.setVertexData(data)
        super()._add_geom(geom,state,mat,tint,layer,False)

    def _texture(self, tex):
        # F_alpha is stored by Panda as one grayscale PNM channel. The generic
        # extractor reads alpha=255; normalize it here without changing that file.
        if self.encode_textures and tex.getFormat() == Texture.F_alpha:
            key = tex.getFullpath().getFullpath() or tex.getName()
            if key not in self._tex_index:
                px, _, _ = extract.texture_pixels(tex,self.max_tex)
                px[:,:,3] = px[:,:,0]
                px[:,:,:3] = 255
                self._tex_index[key] = len(self.textures)
                self.textures.append(ttm.Texture(tex.getName(),px,ttm.GPU_A8,
                    extract._WRAP.get(tex.getWrapU(),ttm.GPU_REPEAT),
                    extract._WRAP.get(tex.getWrapV(),ttm.GPU_REPEAT)))
            return self._tex_index[key]
        return super()._texture(tex)


def fix_geometry(actor, code, loader):
    actor.setTransparency(TransparencyAttrib.M_dual,1)
    if code in ('mk','vmk','mn'):
        bundle = actor.getPartBundle('modelRoot')
        joint = bundle.findChild('sphere3') or bundle.findChild('*sphere3')
        part = actor.getPart('modelRoot')
        ears = part.find('**/sphere3')
        if ears.isEmpty():
            ears = part.find('**/*sphere3')
        assert joint is not None and not ears.isEmpty(), 'missing ear joint'
        joint.clearNetTransforms()
        ears.clearEffect(CharacterJointEffect.getClassType())
        root = part.attachNewNode('earRoot')
        pitch = root.attachNewNode('earPitch')
        pitch.setP(40)
        ears.reparentTo(pitch)
        joint.addNetTransform(root.node())
        ears.clearMat()
        ears.node().setPreserveTransform(ModelNode.PTNone)
        ears.setP(-40)
        ears.flattenMedium()
        # The runtime mesh stores this front-facing reference, not a billboard.

    for pattern in ('**/eyesClosed','**/eyesclose','**/blink'):
        for node in actor.findAllMatches(pattern):
            node.hide()
    for pattern in ('**/eyesOpen','**/eyesopen','**/eyespupil'):
        for node in actor.findAllMatches(pattern):
            node.show()
    eye = {'mk':('3','eyes1'), 'mn':('3','eyes1'), 'p':('6','plutoEyesOpen'),
           'ch':('6','dale_eye1'), 'da':('6','dale_eye1')}.get(code)
    if eye:
        phase, stem = eye
        tex = loader.loadTexture(f'phase_{phase}/maps/{stem}.jpg',f'phase_{phase}/maps/{stem}_a.rgb')
        for node in actor.findAllMatches('**/eyes'):
            node.setTexture(tex,1)
    if code == 'mk':
        for node in actor.findAllMatches('**/joint_pupilR'):
            offset = node.getParent().attachNewNode('pupilOffsetNode')
            offset.setY(.025)
            node.reparentTo(offset)


def capture(actor, encode=False):
    actor.update(force=True)
    ex = CharExtractor(max_tex=128,layer_rules=LAYERS,encode_textures=encode)
    ex.add(actor, animated=True)
    batches = ex.batches()
    for batch in batches:
        # BAM geoms often share a complete actor vertex array while referencing
        # only a handful of rows. Remove unused rows in sorted source order in
        # every frame, retaining a stable remap for TTM and TTA alike.
        used = np.unique(batch.indices)
        batch.verts = batch.verts[used]
        batch.indices = np.searchsorted(used,batch.indices)
    return ex, batches


def positions(batches, reference):
    assert len(batches) == len(reference), 'batch count drift'
    for b,r in zip(batches,reference):
        assert (b.texture,b.blend,b.two_sided,b.tint,b.layer) == (r.texture,r.blend,r.two_sided,r.tint,r.layer)
        assert b.verts.shape == r.verts.shape, 'vertex count drift'
        assert np.array_equal(b.indices,r.indices), 'index order drift'
        assert np.array_equal(b.verts[:,3:],r.verts[:,3:]), 'UV/color/order drift'
    pos = np.concatenate([b.verts[:,:3] for b in batches])
    assert np.isfinite(pos).all()
    return pos


def bake(base, code, lod, data, out):
    prefix = data['ModelDict'][code]
    suffix = '' if lod == 'static' else ('-' if code == 'g' else '') + str(lod)
    model = prefix+suffix
    anims = {name:prefix[:6]+str(phase)+prefix[7:]+suffix
             for name,suffix,phase in data['AnimDict'][code]}
    if code == 'cl':
        # Char.py's old estate pose is absent from the pinned assets. The catalog
        # uses this shipped actor and listen clip instead (CatalogScreen.py:727).
        model = 'phase_5.5/models/char/Clarabelle-zero'
        anims = {'neutral':'phase_5.5/models/char/Clarabelle-listens',
                 'listen':'phase_5.5/models/char/Clarabelle-listens'}
    actor = Actor(model,anims)
    fix_geometry(actor,code,base.loader)
    if anims:
        actor.pose('neutral',0)
    ex, batches = capture(actor,True)
    stem = f'{NAMES[code]}_{lod}'
    stats = ttm.write(out / (stem+'.ttm'),ex.textures,batches)
    assert stats['vertices'] == sum(len(b.verts) for b in batches), 'TTM changed order'
    assert stats['uv_clamped'] == 0
    stats.update(character=NAMES[code],dna_code=code,lod=lod,file=stem+'.ttm',
                 source_model=model,source_anims=anims,height=HEIGHTS[code],nametag_z=HEIGHTS[code]+.5,scale=1,nametag_scale=1,clips=[])
    for clip in anims:
        ctrl = actor.getAnimControl(clip)
        assert ctrl is not None, (model,clip,anims[clip])
        count, rate = ctrl.getNumFrames(), ctrl.getFrameRate()
        assert count > 0 and rate > 0
        nf = max(1,math.ceil(count/rate*12-1e-8))
        cache = {}
        def frame(i):
            if i not in cache:
                actor.pose(clip,i)
                cache[i] = positions(capture(actor)[1],batches)
            return cache[i]
        frames = []
        for i in range(nf):
            src = i*rate/12
            a = min(int(src),count-1)
            b = min(a+1,count-1)
            f = src-a
            frames.append(frame(a)*(1-f)+frame(b)*f)
        target = out / f'{stem}_{clip}.tta'
        tta.write(target,np.array(frames),12.0)
        stats['clips'].append(dict(name=clip,file=target.name,frames=nf,fps=12,
            bytes=target.stat().st_size,source_frames=count,source_fps=rate,
            source_seconds=count/rate,baked_seconds=nf/12))
    stats['over_target'] = stats['triangles'] > 2000 or stats['batches'] > 12
    print(f"{stem}: {stats['triangles']} tris, {stats['batches']} batches, {stats['file_bytes']/1024:.1f} KiB, {len(anims)} clips",flush=True)
    actor.cleanup()
    actor.removeNode()
    return stats


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out-dir',type=Path,default=ROOT/'sdmc-out/3ds/toontown3ds/data/chars')
    ap.add_argument('--characters',nargs='+',choices=list(NAMES))
    args = ap.parse_args()
    args.out_dir.mkdir(parents=True,exist_ok=True)
    base = ShowBase()
    data = tables()
    results = []
    for code in args.characters or NAMES:
        lods = data['LODModelDict'][code]
        chosen = list(dict.fromkeys((lods[len(lods)//2],lods[-1]))) if lods else ['static']
        for lod in chosen:
            results.append(bake(base,code,lod,data,args.out_dir))
    manifest = dict(magic='CCHAR_MANIFEST',version=1,fps=12,models=results,
                    paths=char_paths.export(args.out_dir))
    (args.out_dir/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
    base.destroy()

if __name__ == '__main__':
    main()
