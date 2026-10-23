"""Panda3D scene graph -> ttm.Batch/ttm.Texture lists.

Collapses every visible Geom under a root into batches keyed by render state
(texture, blend mode, two-sidedness, tint slot, layer), with transforms, texture
matrices, flat colors and color scales baked into the vertices.
"""
import fnmatch

import numpy as np
from panda3d.core import (ColorAttrib, ColorScaleAttrib, CullFaceAttrib, GeomVertexReader,
                          PNMImage, SamplerState, Texture, TextureAttrib, TexMatrixAttrib,
                          Thread, TransparencyAttrib)

import ttm

_WRAP = {SamplerState.WM_repeat: ttm.GPU_REPEAT, SamplerState.WM_clamp: ttm.GPU_CLAMP_TO_EDGE,
         SamplerState.WM_border_color: ttm.GPU_CLAMP_TO_EDGE}


def _pow2_floor(n):
    p = 8
    while p * 2 <= n:
        p *= 2
    return p


def texture_pixels(tex, max_size):
    """Panda Texture -> ((h, w, 4) uint8 RGBA, num source components)."""
    img = PNMImage()
    if not tex.store(img):
        raise RuntimeError(f'cannot read texture {tex.getName()}')
    w = min(_pow2_floor(img.getXSize()), max_size)
    h = min(_pow2_floor(img.getYSize()), max_size)
    if (w, h) != (img.getXSize(), img.getYSize()):
        small = PNMImage(w, h, img.getNumChannels(), img.getMaxval())
        small.gaussianFilterFrom(1.0, img)
        img = small
    n = img.getNumChannels()
    px = np.zeros((h, w, 4), dtype=np.uint8)
    for y in range(h):
        for x in range(w):
            if n >= 3:
                c = img.getXel(x, y)
                px[y, x, :3] = (c[0] * 255, c[1] * 255, c[2] * 255)
            else:
                g = img.getGray(x, y) * 255
                px[y, x, :3] = (g, g, g)
            px[y, x, 3] = img.getAlpha(x, y) * 255 if img.hasAlpha() else 255
    return px, n, tex.getFormat()


def choose_format(px, ncomp, panda_fmt):
    alpha = px[:, :, 3]
    if ncomp <= 2:
        if panda_fmt == Texture.F_alpha:
            return ttm.GPU_A8
        if ncomp == 1:
            return ttm.GPU_L8
        return ttm.GPU_LA8
    if np.all(alpha == 255):
        return ttm.GPU_RGB565
    if np.all((alpha == 0) | (alpha == 255)):
        return ttm.GPU_RGBA5551
    return ttm.GPU_RGBA4


class Extractor:
    def __init__(self, max_tex=128, tint_rules=(), layer_rules=()):
        self.max_tex = max_tex
        self.tint_rules = tint_rules     # [(glob on node name, slot)]
        self.layer_rules = layer_rules   # [(glob on node name, layer)]
        self.textures = []
        self._tex_index = {}
        self._groups = {}                # state key -> list of (verts, indices)

    def _texture(self, tex):
        key = tex.getFullpath().getFullpath() or tex.getName()
        if key not in self._tex_index:
            px, ncomp, pfmt = texture_pixels(tex, self.max_tex)
            fmt = choose_format(px, ncomp, pfmt)
            self._tex_index[key] = len(self.textures)
            self.textures.append(ttm.Texture(tex.getName(), px, fmt,
                                             _WRAP.get(tex.getWrapU(), ttm.GPU_REPEAT),
                                             _WRAP.get(tex.getWrapV(), ttm.GPU_REPEAT)))
        return self._tex_index[key]

    def _match(self, rules, node_path, root):
        p = node_path
        while not p.isEmpty() and p != root:
            for pattern, value in rules:
                if fnmatch.fnmatch(p.getName(), pattern):
                    return value
            p = p.getParent()
        return 0

    def add(self, root, animated=False):
        for gnp in root.findAllMatches('**/+GeomNode'):
            if gnp.isHidden() or gnp.isStashed():
                continue
            mat = gnp.getMat(root)
            net_state = gnp.getNetState()
            tint = self._match(self.tint_rules, gnp, root)
            layer = self._match(self.layer_rules, gnp, root)
            gn = gnp.node()
            for gi in range(gn.getNumGeoms()):
                self._add_geom(gn.getGeom(gi), net_state.compose(gn.getGeomState(gi)),
                               mat, tint, layer, animated)

    def _add_geom(self, geom, state, mat, tint, layer, animated):
        vdata = geom.getVertexData()
        if animated:
            vdata = vdata.animateVertices(True, Thread.getCurrentThread())

        tex_idx = None
        ta = state.getAttrib(TextureAttrib)
        tex_mat = None
        if ta and ta.getNumOnStages():
            stage = ta.getOnStage(0)
            tex_idx = self._texture(ta.getOnTexture(stage))
            tma = state.getAttrib(TexMatrixAttrib)
            if tma and tma.hasStage(stage):
                tex_mat = tma.getMat(stage)

        flat = None
        ca = state.getAttrib(ColorAttrib)
        if ca and ca.getColorType() == ColorAttrib.T_flat:
            flat = ca.getColor()
        elif ca and ca.getColorType() == ColorAttrib.T_off:
            flat = (1, 1, 1, 1)
        cs = state.getAttrib(ColorScaleAttrib)
        scale = cs.getScale() if cs and cs.hasScale() else (1, 1, 1, 1)

        tr = state.getAttrib(TransparencyAttrib)
        mode = tr.getMode() if tr else TransparencyAttrib.M_none
        if mode == TransparencyAttrib.M_none:
            blend = ttm.BLEND_OPAQUE
        elif mode in (TransparencyAttrib.M_binary, TransparencyAttrib.M_dual):
            # M_dual approximated as alpha test: no sorting needed, cheapest on PICA.
            blend = ttm.BLEND_ALPHA_TEST
        else:
            blend = ttm.BLEND_ALPHA
        cf = state.getAttrib(CullFaceAttrib)
        two_sided = bool(cf and cf.getEffectiveMode() == CullFaceAttrib.M_cull_none)

        n = vdata.getNumRows()
        verts = np.zeros((n, 9), dtype=np.float64)
        rv = GeomVertexReader(vdata, 'vertex')
        has_uv = vdata.hasColumn('texcoord')
        ruv = GeomVertexReader(vdata, 'texcoord') if has_uv else None
        has_col = vdata.hasColumn('color') and flat is None
        rc = GeomVertexReader(vdata, 'color') if has_col else None
        for i in range(n):
            p = mat.xformPoint(rv.getData3())
            verts[i, 0:3] = (p[0], p[1], p[2])
            if ruv:
                uv = ruv.getData2()
                if tex_mat is not None:
                    uv = tex_mat.xformPoint(uv)
                verts[i, 3:5] = (uv[0], uv[1])
            if rc:
                c = rc.getData4()
                verts[i, 5:9] = (c[0], c[1], c[2], c[3])
            else:
                verts[i, 5:9] = tuple(flat) if flat is not None else (1, 1, 1, 1)
        verts[:, 5:9] *= np.array(tuple(scale))
        if tint:
            # Tinted parts take their color from a runtime uniform (Panda setColor
            # *replaces* vertex color), so neutralize RGB here.
            verts[:, 5:8] = 1.0

        tris = []
        for prim in geom.getPrimitives():
            prim = prim.decompose()
            if prim.getNumVerticesPerPrimitive() != 3:
                continue
            for k in range(prim.getNumPrimitives()):
                s = prim.getPrimitiveStart(k)
                tris.append((prim.getVertex(s), prim.getVertex(s + 1), prim.getVertex(s + 2)))
        if not tris:
            return
        if mat.getUpper3().determinant() < 0:
            tris = [(a, c, b) for a, b, c in tris]
        key = (tex_idx, blend, two_sided, tint, layer)
        self._groups.setdefault(key, []).append((verts, np.array(tris, dtype=np.int64).reshape(-1)))

    def batches(self):
        out = []
        for (tex, blend, two_sided, tint, layer), parts in sorted(
                self._groups.items(), key=lambda kv: (kv[0][4], kv[0][1], str(kv[0]))):
            vs, ids, base = [], [], 0
            for verts, idx in parts:
                if base + len(verts) > ttm.MAX_BATCH_VERTS:
                    out.append(ttm.Batch(tex, blend, two_sided, tint, layer,
                                         np.concatenate(vs), np.concatenate(ids)))
                    vs, ids, base = [], [], 0
                vs.append(verts)
                ids.append(idx + base)
                base += len(verts)
            out.append(ttm.Batch(tex, blend, two_sided, tint, layer,
                                 np.concatenate(vs), np.concatenate(ids)))
        return out
