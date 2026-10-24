#!/usr/bin/env python3
"""Validate all generated character binaries/paths; optionally compare a repeat bake."""
import sys
sys.dont_write_bytecode = True
import os
for key in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','VECLIB_MAXIMUM_THREADS'):
    os.environ[key] = '1'
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import numpy as np
from char_source import ROOT, NAMES, tables
from bake_char import positions, capture, fix_geometry, LAYERS
import extract
import ttm


def check_source(manifest):
    from direct.actor.Actor import Actor
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()
    maximum = 0
    samples = 0
    for entry in manifest['models']:
        actor = Actor(entry['source_model'],entry['source_anims'])
        fix_geometry(actor,entry['dna_code'],base.loader)
        for clip in entry['clips']:
            control = actor.getAnimControl(clip['name'])
            actor.pose(clip['name'],min(7,control.getNumFrames()-1))
            _,batches = capture(actor)
            actual = np.concatenate([b.verts[:,:3] for b in batches])
            standard = extract.Extractor(max_tex=128,layer_rules=LAYERS,encode_textures=False)
            standard.add(actor,animated=True)
            expected = np.concatenate([b.verts[np.unique(b.indices),:3] for b in standard.batches()])
            assert expected.shape == actual.shape
            error = float(np.max(np.abs(expected-actual)))
            maximum = max(maximum,error)
            assert error < 2e-5, (entry['file'],clip['name'],error)
            samples += 1
        actor.cleanup()
        actor.removeNode()
    base.destroy()
    print(f'PASS: deterministic skinning matches Panda CPU reference for {samples} clips; max error {maximum:.9g} units')


def validate(data, repeat=None, source_check=False):
    manifest = json.loads((data/'manifest.json').read_text())
    assert manifest['magic'] == 'CCHAR_MANIFEST' and manifest['version'] == 1
    assert {e['dna_code'] for e in manifest['models']} == set(NAMES)
    total_clips = 0
    sources = tables()
    for entry in manifest['models']:
        buf = (data/entry['file']).read_bytes()
        magic,ver,_,nt,nb,nv,ni,*rest = struct.unpack_from('<4sHHHHII3f3fIIIII',buf)
        assert magic == b'TTM1' and ver == 1
        assert (nb,nv,ni//3,len(buf)) == (entry['batches'],entry['vertices'],entry['triangles'],entry['file_bytes'])
        tex_off,batch_off,v_off,i_off,_ = rest[6:]
        assert v_off%128 == i_off%128 == 0
        vertices = indices = 0
        for i in range(nb):
            tex,blend,flags,tint,layer,slot,pad,fv,cnt,fi,icnt = struct.unpack_from('<HBBBBBBIIII',buf,batch_off+i*24)
            assert tex == 65535 or tex < nt
            assert fv == vertices and fi == indices and cnt > 0 and icnt%3 == 0
            idx = np.frombuffer(buf,dtype='<u2',offset=i_off+fi*2,count=icnt)
            assert int(idx.max()) < cnt
            vertices += cnt
            indices += icnt
        assert (vertices,indices) == (nv,ni)
        for i in range(nt):
            w,h,fmt,mips,ws,wt,offset,size,_ = struct.unpack_from('<HHBBBBII32s',buf,tex_off+i*48)
            expected = sum(ttm.level_size(w>>j,h>>j,fmt) for j in range(mips))
            assert size == expected and offset%128 == 0 and offset+size <= len(buf)
        expected_clips = {v[0] for v in sources['AnimDict'][entry['dna_code']]}
        if entry['dna_code'] == 'cl':
            expected_clips = {'neutral','listen'}
        assert {c['name'] for c in entry['clips']} == expected_clips
        for clip in entry['clips']:
            anim = (data/clip['file']).read_bytes()
            magic,ver,flags,avn,nf,res,fps,*tail = struct.unpack_from('<4sHHIHHf3f3fIIIII',anim)
            off,stride,attach,_,_ = tail[6:]
            assert magic == b'TTA1' and ver == 1 and flags == 0 and avn == nv
            assert fps == 12 and nf == clip['frames'] and off%128 == 0
            assert stride == nv*8 and attach == 0 and off+nf*stride <= len(anim)
            assert len(anim) == clip['bytes'] and len(anim)%128 == 0
            assert np.isfinite(tail[:6]).all() and min(tail[:3]) > 0
            q = np.frombuffer(anim,dtype='<i2',count=nf*nv*4,offset=off).reshape(nf,nv,4)
            assert np.all(q[:,:,3] == 0)
            assert 0 <= clip['baked_seconds']-clip['source_seconds'] < 1/12+1e-6
            total_clips += 1
    edges = 0
    for report in manifest['paths']:
        graph = json.loads((data/report['file']).read_text())
        assert graph['magic'] == 'CCHAR_PATHS' and graph['version'] == 1
        nodes = graph['nodes']
        assert graph['start'] in nodes and len(nodes) == report['nodes']
        pairs = {(s,d) for s,n in nodes.items() for d in n['adjacent']}
        assert pairs == {(e['src'],e['dst']) for e in graph['edges']}
        for edge in graph['edges']:
            pts = edge['points']
            assert pts[0] == nodes[edge['src']]['position'] and pts[-1] == nodes[edge['dst']]['position']
            distance = sum(math.dist(a,b) for a,b in zip(pts,pts[1:]))
            assert abs(distance-edge['distance']) < 1e-4
            if graph['speed']:
                assert abs(edge['nominal_seconds']-distance/graph['speed']) < 1e-4
            assert edge['raycast'] in (0,1)
            edges += 1
    # Prove the order assertion rejects state-preserving UV or index drift.
    verts = np.zeros((3,9)); idx = np.array([0,1,2])
    ref = ttm.Batch(None,0,False,0,0,verts,idx)
    for what in ('uv','index'):
        bad = ttm.Batch(None,0,False,0,0,verts.copy(),idx.copy())
        if what == 'uv':
            bad.verts[0,3] = .25
        else:
            bad.indices[:] = [0,2,1]
        try:
            positions([bad],[ref])
        except AssertionError:
            pass
        else:
            raise AssertionError('order drift was not detected')
    if source_check:
        check_source(manifest)
    count = 0
    if repeat:
        names = [e['file'] for e in manifest['models']]+[c['file'] for e in manifest['models'] for c in e['clips']]
        names += [p['file'] for p in manifest['paths']]+['manifest.json']
        for name in names:
            assert hashlib.sha256((data/name).read_bytes()).digest() == hashlib.sha256((repeat/name).read_bytes()).digest(), name
        count = len(names)
    print(f"PASS: {len(manifest['models'])} models, {total_clips} clips, {len(manifest['paths'])} paths, {edges} directed edges; drift rejection; {count} repeat files identical")

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data-dir',type=Path,default=ROOT/'sdmc-out/3ds/toontown3ds/data/chars')
    ap.add_argument('--repeat-dir',type=Path)
    ap.add_argument('--source-check',action='store_true',help='compare each clip with Panda CPU skinning')
    args = ap.parse_args()
    validate(args.data_dir,args.repeat_dir,args.source_check)
