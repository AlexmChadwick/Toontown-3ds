#!/usr/bin/env python3
"""Render decoded VP parts with the exact TTA attachment matrices used by the 3DS."""
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/pipeline'))
import numpy as np
import preview_ttm
from panda3d.core import BamCache, GeomVertexWriter, Mat4, loadPrcFileData
BamCache.getGlobalPtr().setActive(False)
loadPrcFileData('', 'model-cache-dir\n')
from direct.showbase.ShowBase import ShowBase
OUT = ROOT / 'sdmc-out/3ds/toontown3ds/data/boss'
base = ShowBase()
base.setBackgroundColor(.35, .5, .7, 1)
base.camLens.setFov(55)
base.camLens.setNearFar(1, 500)

def part(name, parent, clip, frame):
    node = preview_ttm.load(str(OUT / (name + '.ttm')), {})
    node.reparentTo(parent)
    blob = (OUT / f'{name}_{clip}.tta').read_bytes()
    header = struct.unpack_from('<4sHHIHHf3f3fIIIII', blob)
    frame %= header[4]
    positions = np.frombuffer(blob, '<i2', count=header[3] * 4,
                              offset=header[13] + frame * header[14]).reshape(-1, 4)[:, :3]
    positions = positions * np.array(header[7:10]) + np.array(header[10:13])
    index = 0
    for geometry in node.getChildren():
        data = geometry.node().modifyGeom(0).modifyVertexData()
        writer = GeomVertexWriter(data, 'vertex')
        for _ in range(data.getNumRows()):
            writer.setData3(*positions[index])
            index += 1
    assert index == header[3]
    if header[2] & 1:
        matrix = np.eye(4)
        matrix[:3] = np.array(struct.unpack_from('<12f', blob, header[15] + frame * 48)).reshape(3, 4)
        child = node.attachNewNode('attachment')
        child.setMat(Mat4(*matrix.T.reshape(-1).tolist()))
        return child
    return node

legs = part('vp_legs', base.render, 'Ff_speech', 8)
torso = part('vp_torso', legs, 'Ff_speech', 8)
part('vp_head', torso, 'Ff_speech', 8)
part('vp_treads', base.render, 'Ff_speech', 8)
base.camera.setPos(38, -65, 28)
base.camera.lookAt(0, 0, 13)
base.graphicsEngine.renderFrame()
base.graphicsEngine.renderFrame()
base.win.saveScreenshot(str(ROOT / 'sdmc-out/bosses2/vp_preview.png'))
print('decoded VP preview: sdmc-out/bosses2/vp_preview.png')
