#!/usr/bin/env python3
"""Decode every baked TTM/TTA and make one contact sheet per model.

.venv/bin/python -I tools/dev/preview_char.py [--data-dir DIR] [--out-dir DIR]
Each clip gets three frames (start, middle, last), read from the actual SD bytes.
"""
import sys
sys.dont_write_bytecode = True
import os
for _key in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','VECLIB_MAXIMUM_THREADS'):
    os.environ[_key] = '1'
import argparse
import json
import struct
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/pipeline'))
import numpy as np
import preview_ttm
from panda3d.core import GeomVertexWriter, PNMImage, TextNode, loadPrcFileData
loadPrcFileData('', 'load-display p3tinydisplay\nwin-size 240 280\n')
from direct.showbase.ShowBase import ShowBase


def load_frames(path, vertices):
    buf = path.read_bytes()
    magic, version, flags, nv, nf, _, fps, *tail = struct.unpack_from('<4sHHIHHf3f3fIIIII',buf)
    assert magic == b'TTA1' and version == 1 and flags == 0
    assert nv == vertices and nf > 0 and fps == 12
    sx,sy,sz,ox,oy,oz,offset,stride,attach,_,_ = tail
    assert stride == nv*8 and attach == 0
    assert offset+nf*stride <= len(buf)
    q = np.frombuffer(buf,dtype='<i2',count=nf*nv*4,offset=offset).reshape(nf,nv,4)
    assert np.all(q[:,:,3] == 0)
    return q[:,:,:3]*np.array([sx,sy,sz])+np.array([ox,oy,oz])


def apply_positions(model, positions):
    cursor = 0
    for child in model.getChildren():
        data = child.node().modifyGeom(0).modifyVertexData()
        writer = GeomVertexWriter(data,'vertex')
        for pos in positions[cursor:cursor+data.getNumRows()]:
            writer.setData3(*pos)
        cursor += data.getNumRows()
    assert cursor == len(positions)


def snapshot(base):
    base.graphicsEngine.renderFrame()
    base.graphicsEngine.renderFrame()
    img = PNMImage()
    assert base.win.getScreenshot().store(img)
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data-dir',type=Path,default=ROOT/'sdmc-out/3ds/toontown3ds/data/chars')
    ap.add_argument('--out-dir',type=Path,default=ROOT/'sdmc-out/previews/chars')
    args = ap.parse_args()
    args.out_dir.mkdir(parents=True,exist_ok=True)
    manifest = json.loads((args.data_dir/'manifest.json').read_text())
    base = ShowBase()
    base.setBackgroundColor(.58,.75,.87,1)
    base.camLens.setFov(38)
    overview = []
    for entry in manifest['models']:
        model = preview_ttm.load(str(args.data_dir/entry['file']),{})
        model.reparentTo(base.render)
        lo,hi = np.array(entry['bounds'])
        center = (lo+hi)/2
        extent = max(hi[2]-lo[2],(hi[0]-lo[0])*280/240,1)
        base.camera.setPos(center[0]-extent*.18,center[1]+extent*1.85,center[2]+extent*.1)
        base.camera.lookAt(*center)
        clips = entry['clips'] or [dict(name='static',frames=1)]
        sheet = PNMImage(720,len(clips)*280,3)
        sheet.fill(.58,.75,.87)
        for row,clip in enumerate(clips):
            frames = load_frames(args.data_dir/clip['file'],entry['vertices']) if 'file' in clip else None
            if frames is not None:
                # Sitting neutral bounds can be much smaller than standing/walk.
                lo,hi = frames.min(axis=(0,1)),frames.max(axis=(0,1))
                center = (lo+hi)/2
                extent = max(hi[2]-lo[2],(hi[0]-lo[0])*280/240,1)
                base.camera.setPos(center[0]-extent*.18,center[1]+extent*1.85+(hi[1]-lo[1])*.5,center[2]+extent*.1)
                base.camera.lookAt(*center)
            for col,frame in enumerate((0,clip['frames']//2,clip['frames']-1)):
                if frames is not None:
                    apply_positions(model,frames[frame])
                text = TextNode('label')
                text.setText(f"{entry['character']} {entry['lod']}\n{clip['name']} [{frame}]")
                text.setAlign(TextNode.ACenter)
                text.setTextColor(0,0,0,1)
                label = base.aspect2d.attachNewNode(text)
                label.setScale(.065)
                label.setPos(0,0,-.83)
                img = snapshot(base)
                sheet.copySubImage(img,col*240,row*280)
                if row == 0 and col == 0:
                    overview.append(img)
                label.removeNode()
        target = args.out_dir/(Path(entry['file']).stem+'.png')
        assert sheet.write(str(target))
        model.removeNode()
        print(target.name,flush=True)
    cols = 6
    sheet = PNMImage(cols*240,math_ceil(len(overview),cols)*280,3)
    sheet.fill(.58,.75,.87)
    for i,img in enumerate(overview):
        sheet.copySubImage(img,i%cols*240,i//cols*280)
    assert sheet.write(str(args.out_dir/'overview.png'))
    base.destroy()


def math_ceil(n,d):
    return (n+d-1)//d

if __name__ == '__main__':
    main()
