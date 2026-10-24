#!/usr/bin/env python3
"""Host conformance/integrity tests; run with .venv/bin/python -I tools/zone_test/run.py.

Compiles the real zone.cpp and collision.cpp on host (no GPU code), validates both
real bakes, visits floor samples from every cell, and rejects corrupt fixtures.
"""
import os
os.environ['OPENBLAS_NUM_THREADS']='1'
os.environ['OMP_NUM_THREADS']='1'
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/pipeline'))
import numpy as np
import tz
import ttm
import street_atlas
from panda3d.core import Texture,PNMImage


class FormatTests(unittest.TestCase):
    def test_layout(self):
        self.assertEqual([tz.HEADER.size,tz.CHUNK.size,tz.FLOOR.size,tz.TRIGGER.size],[64,80,40,156])

    def test_atlas_repeat_clamp(self):
        for repeat in (True,False):
            tex=Texture('asymmetric')
            image=PNMImage(32,32,4)
            image.fill(1,0,0); image.alphaFill(1)
            tex.load(image)
            tex.setWrapU(Texture.WM_repeat if repeat else Texture.WM_clamp)
            tex.setWrapV(Texture.WM_repeat if repeat else Texture.WM_clamp)
            vs=np.array([[0,0,0,-.5,-.5,1,1,1,1],[2,0,0,2.5,-.5,1,1,1,1],[0,2,0,-.5,2.5,1,1,1,1]],dtype=float)
            b=ttm.Batch(0,0,False,0,0,vs,np.arange(3))
            textures,chunks=street_atlas.build([tex],[[b]])
            tri=chunks[0][0].verts.reshape(-1,3,9)
            areas=np.cross(tri[:,1,:3]-tri[:,0,:3],tri[:,2,:3]-tri[:,0,:3])[:,2]/2
            self.assertAlmostEqual(float(areas.sum()),2,places=6)
            self.assertTrue(np.all(areas>=-1e-10))
            uv=tri[:,:,3:5]
            self.assertTrue(np.all(uv[:,:,0]>=4/256-1e-9))
            self.assertTrue(np.all(uv[:,:,0]<=36/256+1e-9))
            self.assertTrue(np.all(uv[:,:,1]>=220/256-1e-9))
            self.assertTrue(np.all(uv[:,:,1]<=252/256+1e-9))
            self.assertEqual(textures[0].mips,1)


def run_real(out):
    with tempfile.TemporaryDirectory() as tmp:
        tmp=Path(tmp)
        binary=tmp/'zone_test'
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fno-rtti','-fno-exceptions',
                        '-fsanitize=address,undefined','-g','-I',str(ROOT/'source'),
                        str(ROOT/'tools/zone_test/zone_test.cpp'),str(ROOT/'source/world/zone.cpp'),
                        str(ROOT/'source/world/collision.cpp'),'-o',str(binary)],check=True)
        for zid in (2100,2000):
            path=out/f'{zid}.tz'
            data=path.read_bytes()
            zone=tz.validate(data)
            points=[]; covered=set()
            if zid==2100:
                for x,y,expected in ((-130,-155,2101),(-225,-140,2103),(-290,-115,2105),(-315,-140,2107)):
                    cell=tz.cell_at(zone,x,y,0)
                    assert cell is not None and zone["chunks"][cell]["zone"]==expected
                    points.append((x,y,0,expected))
            for f in zone['floors']:
                p=np.array(f[:9]).reshape(3,3).mean(axis=0)
                cell=tz.cell_at(zone,*p)
                if cell is not None and cell not in covered:
                    covered.add(cell)
                    points.append((*p,zone['chunks'][cell]['zone']))
            assert covered=={f[9] for f in zone['floors']}, 'some floor cells unreachable'
            assert len(points)>=3
            fixture=tmp/'points.txt'
            fixture.write_text(''.join(f'{x:.7f} {y:.7f} {z:.7f} {zoneid}\n' for x,y,z,zoneid in points))
            corrupt=[]
            def bad(name, blob):
                p=tmp/f'{zid}-{name}.tz'; p.write_bytes(blob); corrupt.append(str(p))
                try:
                    tz.validate(blob)
                except (ValueError,struct.error):
                    return
                raise AssertionError('host validator accepted '+name)
            bad('short',data[:32])
            d=bytearray(data); d[4:6]=struct.pack('<H',99); bad('version',d)
            h=tz.HEADER.unpack_from(data)
            co,vo,fo,to=h[8:12]
            d=bytearray(data); struct.pack_into('<H',d,vo,65535); bad('vis',d)
            d=bytearray(data); struct.pack_into('<H',d,fo+36,65535); bad('floor',d)
            d=bytearray(data); struct.pack_into('<I',d,co+20,7*1024*1024); bad('budget',d)
            d=bytearray(data); struct.pack_into('<I',d,co+12,len(data)+128); bad('model-offset',d)
            c=next(c for c in zone['chunks'] if c['size'])
            d=bytearray(data); d[c['offset']:c['offset']+4]=b'FAIL'; bad('model-magic',d)
            d=bytearray(data); struct.pack_into('<I',d,to+24,99); bad('trigger',d)
            subprocess.run([str(binary),str(path),str(tmp/'scratch.ttm'),str(fixture),*corrupt],check=True)
            print(json.dumps({'zone':zid,'sampled_floor_cells':len(covered),'floor_samples':len(points),'chunks':len(zone['chunks']),
                              'floor_triangles':len(zone['floors']),'triggers':len(zone['triggers']),
                              'malformed_files':len(corrupt)},sort_keys=True),flush=True)


if __name__=='__main__':
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(FormatTests)
    if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
        raise SystemExit(1)
    run_real(ROOT/'sdmc-out/3ds/toontown3ds/data/zones')
