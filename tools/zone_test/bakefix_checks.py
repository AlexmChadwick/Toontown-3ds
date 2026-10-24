#!/usr/bin/env python3
"""Asset-specific regressions: mover frames, conservative HQ PVS and upstream arrivals."""
import os
for key in ('OPENBLAS_NUM_THREADS','OMP_NUM_THREADS','MKL_NUM_THREADS','VECLIB_MAXIMUM_THREADS'):
    os.environ[key] = '1'
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/pipeline'))
import numpy as np
import tz
import hq_arrivals


def positions(data):
    h = struct.unpack_from('<4sHHHHII6f5I',data)
    assert h[0]==b'TTM1' and h[1]==1
    v = np.frombuffer(data,dtype=[('p','<i2',4),('uv','<i2',2),('c','u1',4)],count=h[5],offset=h[15])
    return v['p'][:,:3]*np.array(h[7:10])+np.array(h[10:13])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--resources',required=True)
    ap.add_argument('--out',required=True)
    ap.add_argument('--baseline',required=True)
    args = ap.parse_args()
    out,baseline = Path(args.out),Path(args.baseline)
    from panda3d.core import loadPrcFileData,BamCache,NodePath
    loadPrcFileData('','window-type none\naudio-library-name null\nmodel-cache-dir')
    BamCache.getGlobalPtr().setActive(False)
    from direct.showbase.ShowBase import ShowBase
    import assemble,dna,street_catalog,extract
    base = ShowBase()
    count = 0
    for zone in (1000,2000,3000,4000,5000,9000):
        _,storages,src = street_catalog.paths(zone)
        store = dna.Storage(base.loader,args.resources)
        for storage in storages:
            store.load(str(Path(args.resources)/storage))
        root,_ = assemble.assemble(dna.parse_file(Path(args.resources)/src,[]),store,seed=1)
        anchors = {p.split()[0]:list(map(float,p.split()[1:])) for p in (out/f'{zone}.pts').read_text().splitlines()}
        movers = json.loads((out/f'{zone}_moving.json').read_text())
        assert len(movers)==(3 if zone==1000 else 1)
        for m in movers:
            name = 'trolley_car' if m['anchor']=='trolley_station' else m['anchor'][5:]+'_pier'
            node = root.find('**/'+name)
            frame = node.getParent() if name=='trolley_car' else node
            np.testing.assert_allclose(m['position'],frame.getPos(root),atol=1e-5)
            np.testing.assert_allclose(m['hpr'],frame.getHpr(root),atol=1e-5)
            np.testing.assert_allclose(m['local_position'],node.getPos(),atol=1e-5)
            np.testing.assert_allclose(m['local_hpr'],node.getHpr(),atol=1e-5)
            np.testing.assert_allclose(m['parent_hpr'],node.getParent().getHpr(root),atol=1e-5)
            np.testing.assert_allclose(anchors[m['anchor']],m['position']+[m['heading']],atol=1e-5)
            holder = NodePath('identity')
            copy = node.copyTo(holder); copy.clearMat()
            ex = extract.Extractor(encode_textures=False,layer_rules=assemble.LAYER_RULES)
            ex.add(holder)
            expected = np.concatenate([b.verts[:,:3] for b in ex.batches()])
            actual = positions((out/m['model']).read_bytes())
            np.testing.assert_allclose([actual.min(axis=0),actual.max(axis=0)],
                                       [expected.min(axis=0),expected.max(axis=0)],atol=.002)
            assert m['geometry']['triangles']==sum(len(b.indices)//3 for b in ex.batches())
            if name.endswith('_pier'):
                c = (out/m['collision']).read_bytes()
                assert c[:4]==b'TTC1' and struct.unpack_from('<I',c,24)[0]==6
                assert m['collision_stats']['classes']['floor_tris']==2
                assert m['collision_stats']['classes']['wall_tris']==4
                assert m['animation_hpr_local']==([90,.25,0] if name=='east_pier' else [-90,.25,0])
            holder.removeNode(); count += 1
        root.removeNode()
    base.destroy()
    # Bounds-based PVS must keep every high mesh whose XY bounds touch camera reach.
    zone = tz.validate((out/'12000.tz').read_bytes())
    def missing_visibility(scene):
        missing = set()
        for f in scene['floors']:
            point = np.array(f[:9]).reshape(3,3).mean(axis=0)
            cell = f[9]
            for j,c in enumerate(scene['chunks']):
                if not c['size']:
                    continue
                lo,hi = map(np.array,c['bounds'])
                delta = np.maximum(np.maximum(lo[:2]-point[:2],point[:2]-hi[:2]),0)
                # 120 draw distance + 30 camera orbit; baker reserves 8 more.
                if np.linalg.norm(delta)<=150 and j not in scene['chunks'][cell]['vis']:
                    missing.add((cell,j))
        return missing
    missing = missing_visibility(zone)
    assert not missing,sorted(missing)[:10]
    old_missing = missing_visibility(tz.validate((baseline/'12000.tz').read_bytes()))
    assert old_missing, 'baseline must reproduce the missing-surface regression'
    binary = out/'hq_drop_test'
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fno-rtti','-fno-exceptions',
                    '-fsanitize=address,undefined','-g','-I',str(ROOT/'source'),
                    str(ROOT/'tools/zone_test/hq_drop_test.cpp'),str(ROOT/'source/world/zone.cpp'),
                    str(ROOT/'source/world/collision.cpp'),'-o',str(binary)],check=True)
    results = {}
    for zid in hq_arrivals.HOODS:
        p,source = hq_arrivals.upstream_point(args.resources,zid)
        path = out/f'{zid}.tz' if (out/f'{zid}.tz').exists() else baseline/f'{zid}.tz'
        result = subprocess.run([str(binary),str(path),*map(str,p[:4])],check=True,capture_output=True,text=True)
        results[str(zid)] = dict(point=p,source=source,collision=result.stdout.strip())
    import collide
    collision = zone['collision']
    h = struct.unpack_from('<4sHH3fHH4I6f8I',collision)
    records = np.frombuffer(collision,dtype=[('v','<f4',(3,3)),('n','<f4',3),('d','<f4'),('flags','<u4')],count=h[8],offset=h[18])
    mesh = collide.CollisionMesh()
    mesh.tris = [(r['v'][0],r['v'][1],r['v'][2],r['n'],int(r['flags'])) for r in records]
    hq_arrivals.check_floor(mesh,hq_arrivals.upstream_point(args.resources,12000)[0])
    try:
        hq_arrivals.check_floor(mesh,[100,-500,-48,0,0,0])
    except ValueError:
        pass
    else:
        raise AssertionError('old Cashbot arrival accepted despite missing floor')
    old = subprocess.run([str(binary),str(out/'12000.tz'),'100','-500','-48','0','diagnose'],check=True,capture_output=True,text=True)
    results['old_cashbot'] = old.stdout.strip()
    (out/'bakefix_checks.json').write_text(json.dumps(dict(moving_models=count,baseline_missing_pvs_pairs=len(old_missing),missing_pvs_pairs=len(missing),arrivals=results),indent=2)+'\n')
    print(json.dumps(dict(moving_models=count,baseline_missing_pvs_pairs=len(old_missing),missing_pvs_pairs=len(missing),arrivals=results),indent=2))


if __name__=='__main__':
    main()
