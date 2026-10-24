#!/usr/bin/env python3
"""Preflight source animations, snapshot legacy hashes, and audit a complete Toon SD bake.

Run with Python -I -B. Reports are written under --reports (normally sdmc-out).
No runtime format is changed and no shared reference/cache is written.
"""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
sys.dont_write_bytecode = True
for key in ('OPENBLAS_NUM_THREADS','OMP_NUM_THREADS','VECLIB_MAXIMUM_THREADS','MKL_NUM_THREADS'):
    os.environ[key] = '1'
ROOT = Path(__file__).resolve().parents[2]
HEADER = struct.Struct('<4sHHIHHf3f3fIIIII')


def tables():
    tree = ast.parse((ROOT/'tools/pipeline/bake_toon.py').read_text())
    result = {}
    for n in tree.body:
        if isinstance(n,ast.Assign):
            for target in n.targets:
                if isinstance(target,ast.Name) and target.id in ('ANIMS','DOG_HEADS','TORSOS','LEGS'):
                    result[target.id] = ast.literal_eval(n.value)
        if isinstance(n,ast.Expr) and isinstance(n.value,ast.Call):
            call = n.value
            if isinstance(call.func,ast.Attribute) and isinstance(call.func.value,ast.Name) and call.func.value.id=='ANIMS' and call.func.attr=='update':
                result['LEGACY_ANIMS'] = list(result['ANIMS'])
                result['ANIMS'].update(ast.literal_eval(call.args[0]))
    return result


def available(kind,code,clip):
    if kind=='torso' and clip==('bow' if code.endswith('d') else 'curtsy'):
        return False
    if kind=='head' and clip==('bow' if code=='dss' else 'curtsy'):
        return False
    return True


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(out,reports):
    manifest = json.loads((out/'toon_manifest.json').read_text())
    hashes = {p.name:dict(bytes=p.stat().st_size,sha256=digest(p)) for p in sorted(out.iterdir())
              if p.suffix in ('.ttm','.tta','.ttx')}
    for name,value in (('toon_before_hashes.json',hashes),('toon_before_manifest.json',manifest)):
        path = reports/name
        if path.exists():
            raise ValueError(f'refusing to replace original snapshot: {path}')
        path.write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')
    print(f'Snapshotted {len(hashes)} binary files')


def sources(resources,upstream,reports):
    t = tables()
    # Confirm suffix/phase mappings against Toon.py's want-new-anims lists using AST.
    tree = ast.parse((upstream/'toontown/toon/Toon.py').read_text())
    declared = {}
    for n in tree.body:
        if isinstance(n,ast.Assign):
            for target in n.targets:
                if isinstance(target,ast.Name) and target.id.startswith('Phase') and target.id.endswith('AnimList'):
                    phase = float(target.id[5:-8].replace('_','.'))
                    for name,suffix in ast.literal_eval(n.value):
                        declared[name] = (suffix,phase)
    assert all(tuple(value)==declared[name] for name,value in t['ANIMS'].items())
    paths,records,absent = [],[],[]
    for clip,(suffix,phase) in t['ANIMS'].items():
        for kind,key in (('legs','LEGS'),('torso','TORSOS'),('head','DOG_HEADS')):
            for code,prefix in t[key].items():
                path = resources/f'phase_{phase:g}/models/char/{prefix}{suffix}.bam'
                if not available(kind,code,clip):
                    assert not path.exists(),str(path)
                    absent.append(dict(clip=clip,kind=kind,code=code,path=str(path.relative_to(resources))))
                    continue
                paths.append(str(path))
                records.append(dict(clip=clip,kind=kind,code=code,path=str(path.relative_to(resources))))
    result = subprocess.run(['ls',*paths],capture_output=True,text=True)
    (reports/'toon-source-ls.log').write_text(result.stdout+result.stderr)
    if result.returncode:
        raise ValueError(result.stderr)
    from panda3d.core import BamCache,Loader,LoaderOptions,Filename,loadPrcFileData
    BamCache.getGlobalPtr().setActive(False)
    loadPrcFileData('','model-cache-dir\naudio-library-name null')
    loader = Loader.getGlobalPtr()
    options = LoaderOptions(LoaderOptions.LFNoCache)
    from panda3d.core import NodePath
    for record,path in zip(records,paths):
        node = NodePath(loader.loadSync(Filename.fromOsSpecific(path),options))
        bundle = node.find('**/+AnimBundleNode')
        if bundle.isEmpty() and node.node().getType().getName()=='AnimBundleNode':
            bundle = node
        assert not bundle.isEmpty(),path
        animation = bundle.node().getBundle()
        record.update(frames=animation.getNumFrames(),fps=animation.getBaseFrameRate())
        node.removeNode()
    result = dict(sources=records,intentionally_absent=absent)
    (reports/'toon_sources.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'{len(records)} source BAMs verified by ls and AnimBundle headers; {len(absent)} sparse bow/curtsy combinations')


def verify(out,reports):
    t = tables()
    before = json.loads((reports/'toon_before_hashes.json').read_text())
    old = json.loads((reports/'toon_before_manifest.json').read_text())
    manifest = json.loads((out/'toon_manifest.json').read_text())
    mismatches = [name for name,value in before.items() if not (out/name).exists() or digest(out/name)!=value['sha256']]
    assert not mismatches,mismatches
    assert list(manifest['anims'])==list(t['ANIMS'])
    assert manifest['lods']==['500','250']
    for key in ('clothes','body_scale','lods','version','bake_fps'):
        assert old[key]==manifest[key],key
    for kind,parts in old['parts'].items():
        for code,lods in parts.items():
            if kind=='lashes':
                assert lods==manifest['parts'][kind][code]
                continue
            for lod,part in lods.items():
                actual = manifest['parts'][kind][code][lod]
                for key in part:
                    if key=='clips':
                        assert all(actual[key][name]==path for name,path in part[key].items())
                    else:
                        assert actual[key]==part[key]
    sources = json.loads((reports/'toon_sources.json').read_text())['sources']
    source_map = {(r['kind'],r['code'],r['clip']):r for r in sources}
    rows = {name:dict(new=name not in t['LEGACY_ANIMS'],source_suffix=suffix,phase=phase,
                     files=0,sd_bytes=0,linear_min=10**20,linear_max=0,attach_heap_max=0,
                     frames=set(),source_frames=set(),fps=set(),default_linear={})
            for name,(suffix,phase) in t['ANIMS'].items()}
    hashes = {}
    for name,size in manifest['files'].items():
        path = out/name
        assert path.stat().st_size==size
        hashes[name] = digest(path)
    for kind in ('legs','torso','head'):
        for code,lods in manifest['parts'][kind].items():
            for lod,part in lods.items():
                expected = list(t['ANIMS']) if kind!='head' or code.startswith('d') else []
                expected = [a for a in expected if available(kind,code,a)]
                assert set(part['clips'])==set(expected),(kind,code,lod)
                for clip,name in part['clips'].items():
                    data = (out/name).read_bytes()
                    h = HEADER.unpack_from(data)
                    magic,version,flags,nv,nf,reserved,fps = h[:7]
                    fo,stride,ao = h[13:16]
                    assert magic==b'TTA1' and version==1 and reserved==0
                    assert nv==part['vertices'] and stride==nv*8 and nf>0 and fo==128
                    assert flags==(1 if kind in ('legs','torso') else 0)
                    assert fo+nf*stride<=len(data) and len(data)%128==0
                    assert not ao if not flags else ao>=fo+nf*stride and ao+nf*48<=len(data)
                    src = source_map[kind,code,clip]
                    step = max(1,round(src['fps']/12))
                    assert nf==len(range(0,src['frames'],step)) and abs(fps-src['fps']/step)<1e-5
                    import numpy as np
                    assert np.isfinite(h[6:13]).all() and min(h[7:10])>0
                    frame_data = np.frombuffer(data,dtype='<i2',count=nf*nv*4,offset=fo).reshape(nf,nv,4)
                    assert np.all(frame_data[:,:,3]==0)
                    if ao:
                        attach = np.frombuffer(data,dtype='<f4',count=nf*12,offset=ao)
                        assert np.isfinite(attach).all()
                    row = rows[clip]
                    row['files']+=1; row['sd_bytes']+=len(data)
                    row['linear_min']=min(row['linear_min'],nf*stride)
                    row['linear_max']=max(row['linear_max'],nf*stride)
                    row['attach_heap_max']=max(row['attach_heap_max'],nf*48 if flags else 0)
                    row['frames'].add(nf); row['source_frames'].add(src['frames']); row['fps'].add(fps)
                    if (kind,code) in (('legs','m'),('torso','ms'),('head','dls')):
                        row['default_linear'][lod] = row['default_linear'].get(lod,0)+nf*stride
    for row in rows.values():
        for key in ('frames','source_frames','fps'):
            row[key] = sorted(row[key])
    new = {name:size for name,size in manifest['files'].items() if name not in before}
    assert len(new)==sum(r['files'] for r in rows.values() if r['new'])
    result = dict(unchanged_files=len(before),unchanged_bytes=sum(x['bytes'] for x in before.values()),
                  mismatches=mismatches,new_files=len(new),extra_sd_bytes=sum(new.values()),
                  total_binary_bytes=manifest['totals']['bytes'],clips=rows)
    (reports/'toon_anim_audit.json').write_text(json.dumps(result,indent=2)+'\n')
    (reports/'toon_after_hashes.json').write_text(json.dumps(hashes,indent=2,sort_keys=True)+'\n')
    (reports/'toon_clips.tsv').write_text('clip\tsource frames\tbaked frames\tSD bytes\tmax part linear bytes\tdefault 500 linear bytes\n'+''.join(
        f"{name}\t{r['source_frames']}\t{r['frames']}\t{r['sd_bytes']}\t{r['linear_max']}\t{r['default_linear'].get('500',0)}\n" for name,r in rows.items()))
    repeat_path = reports/'toon_repeat_before_hashes.json'
    if repeat_path.exists():
        repeated = json.loads(repeat_path.read_text())
        assert repeated==hashes, 'repeat bake differs from the complete prior bake'
        (reports/'toon_determinism.json').write_text(json.dumps(dict(byte_identical_files=len(hashes),mismatches=[]),indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='clips'},indent=2))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action',choices=('snapshot','repeat-snapshot','sources','verify'))
    ap.add_argument('--out',type=Path,default=ROOT/'sdmc-out/3ds/toontown3ds/data/toon')
    ap.add_argument('--reports',type=Path,default=ROOT/'sdmc-out')
    ap.add_argument('--resources',type=Path,default=ROOT/'ref/ref/resources')
    ap.add_argument('--upstream',type=Path,default=ROOT/'ref/ref/open-toontown')
    args = ap.parse_args()
    args.reports.mkdir(parents=True,exist_ok=True)
    if args.action=='sources':
        sources(args.resources.resolve(),args.upstream.resolve(),args.reports)
    elif args.action=='repeat-snapshot':
        path = args.reports/'toon_repeat_before_hashes.json'
        if path.exists():
            raise ValueError(f'refusing to replace repeat snapshot: {path}')
        hashes = {p.name:digest(p) for p in sorted(args.out.iterdir()) if p.suffix in ('.ttm','.tta','.ttx')}
        path.write_text(json.dumps(hashes,indent=2,sort_keys=True)+'\n')
        print(f'Snapshotted {len(hashes)} files for repeat verification')
    elif args.action=='snapshot':
        snapshot(args.out,args.reports)
    else:
        verify(args.out,args.reports)


if __name__=='__main__':
    main()
