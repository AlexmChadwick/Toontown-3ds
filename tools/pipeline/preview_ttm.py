#!/usr/bin/env python3
"""Decode a .ttm and render it offscreen with Panda3D to a PNG.

Validates the baker independently of the 3DS runtime: it reads back exactly the
bytes the 3DS will see (quantized vertices, tiled textures).

    .venv/bin/python -I tools/pipeline/preview_ttm.py romfs/baked/x.ttm out.png [--tint-demo]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import numpy as np  # noqa: E402
from panda3d.core import (AlphaTestAttrib, ColorBlendAttrib, CullFaceAttrib, Geom,  # noqa: E402
                          GeomNode, GeomTriangles, GeomVertexData, GeomVertexFormat,
                          GeomVertexWriter, NodePath, PNMImage, RenderAttrib, Texture,
                          TransparencyAttrib, loadPrcFileData)

loadPrcFileData('', 'window-type offscreen\naudio-library-name null\nnotify-level warning\n'
                    'win-size 400 240\nframebuffer-multisample 0')
from direct.showbase.ShowBase import ShowBase  # noqa: E402

import ttm  # noqa: E402

DEMO_TINTS = {1: (0.35, 0.82, 0.95, 1), 2: (0.35, 0.82, 0.95, 1), 3: (1, 1, 1, 1),
              4: (0.35, 0.82, 0.95, 1), 5: (0.93, 0.27, 0.28, 1), 6: (0.93, 0.27, 0.28, 1),
              7: (0.19, 0.56, 0.77, 1)}


def untile(data, w, h, fmt):
    bpp = ttm.BYTES_PER_PIXEL[fmt]
    raw = np.frombuffer(data[:w * h * bpp], dtype=np.uint8)
    if bpp == 2:
        v = raw.view('<u2').astype(np.uint32)
    elif bpp == 4:
        v = raw.reshape(-1, 4)
    else:
        v = raw.astype(np.uint32)
    rgba = np.zeros((w * h, 4), dtype=np.uint8)
    if fmt == ttm.GPU_RGBA8:
        rgba[:, 0], rgba[:, 1], rgba[:, 2], rgba[:, 3] = v[:, 3], v[:, 2], v[:, 1], v[:, 0]
    elif fmt == ttm.GPU_RGB565:
        rgba[:, 0], rgba[:, 1], rgba[:, 2], rgba[:, 3] = (v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3, 255
    elif fmt == ttm.GPU_RGBA5551:
        rgba[:, 0], rgba[:, 1], rgba[:, 2] = (v >> 11) << 3, ((v >> 6) & 31) << 3, ((v >> 1) & 31) << 3
        rgba[:, 3] = (v & 1) * 255
    elif fmt == ttm.GPU_RGBA4:
        rgba[:, 0], rgba[:, 1], rgba[:, 2], rgba[:, 3] = (v >> 12) * 17, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17
    elif fmt == ttm.GPU_LA8:
        rgba[:, 0] = rgba[:, 1] = rgba[:, 2] = v >> 8
        rgba[:, 3] = v & 255
    elif fmt == ttm.GPU_L8:
        rgba[:, 0] = rgba[:, 1] = rgba[:, 2] = v
        rgba[:, 3] = 255
    elif fmt == ttm.GPU_A8:
        rgba[:, :3] = 255
        rgba[:, 3] = v
    img = np.zeros((h, w, 4), dtype=np.uint8)
    tiles_x = w // 8
    morton = ttm._MORTON.reshape(64)
    for ty in range(h // 8):
        for tx in range(tiles_x):
            base = (ty * tiles_x + tx) * 64
            img[ty * 8:(ty + 1) * 8, tx * 8:(tx + 1) * 8] = rgba[base + morton].reshape(8, 8, 4)
    return img


def load(path, tints):
    buf = open(path, 'rb').read()
    (magic, ver, _, ntex, nbatch, nv, ni, sx, sy, sz, ox, oy, oz,
     tex_off, batch_off, v_off, i_off, _) = struct.unpack_from('<4sHHHHII3f3fIIIII', buf, 0)
    assert magic == ttm.MAGIC and ver == ttm.VERSION
    textures = []
    for i in range(ntex):
        w, h, fmt, mips, ws, wt, doff, dsize, name = struct.unpack_from('<HHBBBBII32s', buf, tex_off + 48 * i)
        px = untile(buf[doff:doff + dsize], w, h, fmt)
        img = PNMImage(w, h, 4)
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[y, x] / 255.0
                img.setXelA(x, y, r, g, b, a)
        tex = Texture(name.rstrip(b'\0').decode())
        tex.load(img)
        tex.setWrapU(Texture.WM_repeat if ws == ttm.GPU_REPEAT else Texture.WM_clamp)
        tex.setWrapV(Texture.WM_repeat if wt == ttm.GPU_REPEAT else Texture.WM_clamp)
        textures.append(tex)
    verts = np.frombuffer(buf, dtype=[('p', '<i2', 4), ('uv', '<i2', 2), ('c', 'u1', 4)],
                          count=nv, offset=v_off)
    idx = np.frombuffer(buf, dtype='<u2', count=ni, offset=i_off)
    root = NodePath('ttm')
    fmt = GeomVertexFormat.getV3c4t2()
    for b in range(nbatch):
        tex, blend, flags, tint, layer, _, fv, cnt, fi, icnt = struct.unpack_from(
            '<HBBBBHIIII', buf, batch_off + 24 * b)
        vd = GeomVertexData('b', fmt, Geom.UHStatic)
        wv, wc, wt = (GeomVertexWriter(vd, n) for n in ('vertex', 'color', 'texcoord'))
        tcol = np.array(tints.get(tint, (1, 1, 1, 1)))
        for v in verts[fv:fv + cnt]:
            wv.addData3(v['p'][0] * sx + ox, v['p'][1] * sy + oy, v['p'][2] * sz + oz)
            c = v['c'] / 255.0 * tcol
            wc.addData4(*c)
            wt.addData2(v['uv'][0] / ttm.UV_SCALE, v['uv'][1] / ttm.UV_SCALE)
        tris = GeomTriangles(Geom.UHStatic)
        for k in idx[fi:fi + icnt]:
            tris.addVertex(int(k))
        g = Geom(vd)
        g.addPrimitive(tris)
        gn = GeomNode(f'batch{b}')
        gn.addGeom(g)
        np_ = root.attachNewNode(gn)
        if tex != 0xFFFF:
            np_.setTexture(textures[tex])
        if blend == ttm.BLEND_ALPHA_TEST:
            np_.setAttrib(AlphaTestAttrib.make(RenderAttrib.M_greater, 0.5))
        elif blend == ttm.BLEND_ALPHA:
            np_.setTransparency(TransparencyAttrib.M_alpha)
        if flags & 1:
            np_.setTwoSided(True)
        if layer:
            np_.setBin('fixed', layer)
            np_.setDepthOffset(layer)
    return root


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ttm')
    ap.add_argument('png')
    ap.add_argument('--tint-demo', action='store_true')
    ap.add_argument('--cam', type=float, nargs=6, metavar=('X', 'Y', 'Z', 'LX', 'LY', 'LZ'))
    args = ap.parse_args()
    base = ShowBase()
    base.setBackgroundColor(0.41, 0.69, 0.85, 1)
    model = load(args.ttm, DEMO_TINTS if args.tint_demo else {})
    model.reparentTo(base.render)
    lo, hi = model.getTightBounds()
    center = (lo + hi) / 2
    radius = (hi - lo).length() / 2
    if args.cam:
        base.camera.setPos(*args.cam[:3])
        base.camera.lookAt(*args.cam[3:])
    else:
        base.camera.setPos(center + (radius * 0.9, -radius * 2.0, radius * 0.7))
        base.camera.lookAt(center)
    base.camLens.setFov(60)
    base.camLens.setNearFar(0.1, radius * 10)
    base.graphicsEngine.renderFrame()
    base.graphicsEngine.renderFrame()
    base.win.saveScreenshot(args.png)
    print('wrote', args.png)


if __name__ == '__main__':
    main()
