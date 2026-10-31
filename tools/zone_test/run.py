#!/usr/bin/env python3
"""Host conformance/integrity tests; run with .venv/bin/python -I tools/zone_test/run.py.

Compiles the real zone.cpp and collision.cpp on host (no GPU code), validates both
real bakes, visits floor samples from every cell, and rejects corrupt fixtures.
"""
import os
os.environ['OPENBLAS_NUM_THREADS']='1'
os.environ['OMP_NUM_THREADS']='1'
os.environ['TT_ETC1_THREADS']='1'
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
import street_catalog
import street_lod
import sign_text
import dna
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

    def test_sign_layout(self):
        node=dna.parse('baseline [scale [2 1 2] kern [.2] stumble [.1] stomp [.2] wiggle [5]]').children[0]
        item=dna.Node('text')
        layout=sign_text.Layout(node)
        placed=[layout.place(item,1,'A') for _ in range(3)]
        np.testing.assert_allclose([p[0][0] for p in placed],[-.1,2.3,4.3])
        np.testing.assert_allclose([p[0][2] for p in placed],[-.2,.2,-.2])
        np.testing.assert_allclose([p[1][2] for p in placed],[5,-5,5])
        self.assertAlmostEqual(layout.centre()[0][0],-3.15)
        layout=sign_text.Layout(dna.parse('baseline [ flags ["b"] ]').children[0])
        self.assertEqual(layout.place(item,1,'A')[2][0],1.5)
        self.assertEqual(layout.place(item,1,'B')[2][0],1)
        layout.place(item,1,' ')
        self.assertEqual(layout.place(item,1,'C')[2][0],1.5)
        for width in (10,-10):
            layout=sign_text.Layout(dna.parse(f'baseline [width [{width}] height [{width}] indent [15]]').children[0])
            points=[layout.place(item,1,'A') for _ in range(4)]
            self.assertTrue(np.isfinite(np.array(points)).all())
            self.assertNotEqual(points[0][1][2],points[3][1][2])

    def test_alpha_only_font_mask(self):
        image=PNMImage(8,8,1)
        image.fill(0)
        image.setGray(4,4,1)
        tex=Texture('font_mask')
        tex.load(image)
        tex.setFormat(Texture.F_alpha)
        px,nc,fmt=street_atlas.texture_pixels(tex,8)
        self.assertEqual(fmt,Texture.F_alpha)
        self.assertEqual(int(px[:,:,3].min()),0)
        self.assertEqual(int(px[:,:,3].max()),255)
        self.assertTrue(np.all(px[:,:,:3]==255))

    def test_large_uvs(self):
        class Tex:
            wrap_s=2
            wrap_t=2
        vs=np.array([[0,0,0,0,0,1,1,1,1],[2,0,0,80,0,1,1,1,1],[0,2,0,0,80,1,1,1,1]],dtype=float)
        b=ttm.Batch(0,0,False,0,0,vs,np.arange(3))
        out=street_atlas.rebase_large_uvs(b,[Tex()])
        tri=out.verts.reshape(-1,3,9)
        self.assertAlmostEqual(float(np.cross(tri[:,1,:3]-tri[:,0,:3],tri[:,2,:3]-tri[:,0,:3])[:,2].sum()/2),2)
        self.assertLessEqual(float(out.verts[:,3:5].max()),16.00001)


def run_real(out, zones=None):
    with tempfile.TemporaryDirectory() as tmp:
        tmp=Path(tmp)
        binary=tmp/'zone_test'
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fno-rtti','-fno-exceptions',
                        '-fsanitize=address,undefined','-g','-I',str(ROOT/'source'),
                        str(ROOT/'tools/zone_test/zone_test.cpp'),str(ROOT/'source/world/zone.cpp'),
                        str(ROOT/'source/world/collision.cpp'),'-o',str(binary)],check=True)
        if zones is None:
            found = [int(p.stem) for p in sorted(out.glob('*.tz')) if p.stem.isdigit()]
            if any(z >= 10000 for z in found):
                zones = tuple(found)
            else:
                zones = street_catalog.ZONES
        for zid in zones:
            assert (out/f"{zid}.tz").exists(),f'missing bake {zid}'
            path=out/f'{zid}.tz'
            anchor=path.with_suffix('.pts')
            assert anchor.exists()
            for line in anchor.read_text().splitlines():
                fields=line.split()
                assert len(fields)==5 and len(fields[0])<=63 and all(np.isfinite(float(x)) for x in fields[1:])
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
            if not points:
                p=np.array(zone['floors'][0][:9]).reshape(3,3).mean(axis=0)
                points.append((float(p[0]),float(p[1]),float(p[2]),zid))
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
            d=bytearray(data); struct.pack_into('<I',d,co+(88 if zone['version']>=2 else 20),7*1024*1024); bad('budget',d)
            d=bytearray(data); struct.pack_into('<I',d,co+12,len(data)+128); bad('model-offset',d)
            c=next(c for c in zone['chunks'] if c['size'])
            d=bytearray(data); d[c['offset']:c['offset']+4]=b'FAIL'; bad('model-magic',d)
            d=bytearray(data); struct.pack_into('<I',d,to+24,99); bad('trigger',d)
            if zone['version']>=2:
                d=bytearray(data); struct.pack_into('<I',d,co+80,len(data)+128); bad('low-offset',d)
                d=bytearray(data); struct.pack_into('<I',d,co+100,15001); bad('tri-budget',d)
            if zone['version']==3:
                # A pooled texture offset past the pool, and a pool running past the file.
                m=next(m for m in zone['chunks'] if m['size'] and struct.unpack_from('<H',data,m['offset']+8)[0])
                d=bytearray(data); tt=struct.unpack_from('<I',data,m['offset']+44)[0]
                struct.pack_into('<I',d,m['offset']+tt+8,len(data)); bad('pool-texture',d)
                d=bytearray(data); struct.pack_into('<I',d,60,len(data)); bad('pool-size',d)
                # Repack the same data in v1 to prove the old layout still loads.
                legacy=[]
                for i,c in enumerate(zone['chunks']):
                    legacy.append(dict(c,vis=[i],stats=dict(c['stats'],bounds=c['bounds']),
                                       model=tz.model_bytes(data,c['offset'],c['size'])))
                legacy_path=tmp/'legacy.tz'
                floors=[(np.array(f[:9]).reshape(3,3),f[9],f[10]) for f in zone['floors']]
                triggers=[{'min':t[:3],'max':t[3:6],'kind':t[6],
                           'name':t[7].split(b'\0')[0].decode(),'owner':t[8].split(b'\0')[0].decode()} for t in zone['triggers']]
                tz.write(legacy_path,zid,legacy,floors,triggers,zone['collision'],version=1)
                subprocess.run([str(binary),str(legacy_path),str(fixture)],check=True)
                for f in zone['floors'][::max(1,len(zone['floors'])//100)]:
                    p=np.array(f[:9]).reshape(3,3).mean(axis=0)
                    _,metrics=street_lod.plan(zone['chunks'],f[9],p)
                    assert metrics['triangles']<=15000 and metrics['batches']<=150
            subprocess.run([str(binary),str(path),str(fixture),*corrupt],check=True)
            print(json.dumps({'zone':zid,'sampled_floor_cells':len(covered),'floor_samples':len(points),'chunks':len(zone['chunks']),
                              'floor_triangles':len(zone['floors']),'triggers':len(zone['triggers']),
                              'malformed_files':len(corrupt)},sort_keys=True),flush=True)


if __name__=='__main__':
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(FormatTests)
    if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
        raise SystemExit(1)
    if '--unit' not in sys.argv:
        zones = None
        for arg in sys.argv[1:]:
            if arg == '--coghq':
                import coghq_catalog
                zones = coghq_catalog.ZONES
            elif arg.startswith('--zone='):
                zones = tuple(int(z) for z in arg.split('=')[1].split(','))
            elif arg.startswith('--zones='):
                zones = tuple(int(z) for z in arg.split('=')[1].split(','))
        run_real(ROOT/'sdmc-out/3ds/toontown3ds/data/zones', zones=zones)
