"""Shared 256px atlas pages; repeat UV triangles are clipped at tile boundaries.

32px source tiles have four-pixel gutters. Pages have only a base mip: automatic
whole-page mips would bleed across unrelated tiles. Large tiled triangles retain
standalone textures instead of creating unbounded geometry. All batches retain
blend/cull/decal state. No existing pipeline modules are modified.
"""
import math
import numpy as np
import extract
import ttm
import sign_text


def texture_pixels(tex, size):
    """Normalize Panda alpha-only sources before atlas/TTM encoding.

    Texture.store exposes an F_alpha texture as a one-channel grayscale PNMImage.
    extract.texture_pixels consequently puts its mask in RGB with alpha=255.
    Recover the mask explicitly; otherwise glyphs become solid rectangles.
    """
    from panda3d.core import Texture
    pixels,components,fmt=extract.texture_pixels(tex,size)
    if fmt==Texture.F_alpha:
        pixels[:,:,3]=pixels[:,:,0]
        pixels[:,:,:3]=255
    return pixels,components,fmt


class Extractor(extract.Extractor):
    def __init__(self):
        super().__init__(layer_rules=__import__('assemble').LAYER_RULES)
        self.sources = []

    def _texture(self, tex):
        key = ('sign_font', int(tex.this)) if sign_text.is_font_texture(tex) else (tex.getFullpath().getFullpath() or tex.getName())
        if key not in self._tex_index:
            self._tex_index[key] = len(self.sources)
            self.sources.append(tex)
        return self._tex_index[key]


def clip(poly, axis, edge, above):
    """Sutherland-Hodgman, interpolating all nine vertex attributes."""
    out = []
    for a, b in zip(poly, poly[1:] + poly[:1]):
        da, db = a[axis] - edge, b[axis] - edge
        ina, inb = (da >= -1e-10, db >= -1e-10) if above else (da <= 1e-10, db <= 1e-10)
        if ina:
            out.append(a)
        if ina != inb:
            out.append(a + (b - a) * (da / (da - db)))
    return out


def tile_ranges(tri, tex):
    uv = tri[:, 3:5]
    out = []
    for axis, wrap in enumerate((tex.getWrapU(), tex.getWrapV())):
        # Panda repeat = 1, whereas PICA repeat = 2.
        if wrap == tex.WM_repeat:
            lo = math.floor(float(uv[:, axis].min()) + 1e-8)
            hi = max(lo, math.ceil(float(uv[:, axis].max()) - 1e-8) - 1)
            out.append(range(lo, hi + 1))
        else:
            # Clamped UVs outside the unit square need separate constant-edge pieces.
            out.append(range(-1 if uv[:, axis].min() < 0 else 0,
                             2 if uv[:, axis].max() > 1 else 1))
    return out


def _max_tile_span(idx, sources, chunks):
    """Widest repeat count of one triangle. 1 means the texture is a single unwrap."""
    tex = sources[idx]
    span = 1
    for batches in chunks:
        for b in batches:
            if b.texture != idx:
                continue
            for tri in b.verts[b.indices.reshape(-1, 3)]:
                xr, yr = tile_ranges(tri, tex)
                span = max(span, len(xr), len(yr))
                if span > 1:
                    return span
    return span


def _signish(tex):
    if sign_text.is_font_texture(tex):
        return True
    return 'sign' in (tex.getName() or '').lower()


def build(sources, chunks):
    """Return textures and transformed chunk batches, with shared atlas indices."""
    # Keep pathological UV tiling separate, bounded in both memory and bake time.
    standalone = {i for i, t in enumerate(sources) if sign_text.is_font_texture(t)}
    # A shop roof or sign is one copy of a large texture (UV span about 1). The 32px
    # atlas slot stretched that copy across the whole face, and ETC1 turned the thin
    # sign strokes into a black smear. Those stay standalone, capped at 128 (256 for
    # a sign font) so a visible set still fits the 8 MB texture budget. Repeating
    # trim stays in the atlas.
    for i, tex in enumerate(sources):
        if i in standalone:
            continue
        src = max(tex.getXSize(), tex.getYSize())
        if _signish(tex) or (src > 48 and _max_tile_span(i, sources, chunks) <= 1):
            standalone.add(i)
    for batches in chunks:
        for b in batches:
            if b.texture is None:
                continue
            tex = sources[b.texture]
            for tri in b.verts[b.indices.reshape(-1, 3)]:
                xr, yr = tile_ranges(tri, tex)
                if len(xr) * len(yr) > 32:
                    standalone.add(b.texture)
                    break
    textures, mapping = [], {}
    frequency = {i: sum(any(b.texture == i for b in bs) for bs in chunks) for i in range(len(sources))}
    entries = sorted((i for i in range(len(sources)) if i not in standalone), key=lambda i: (-frequency[i], i))
    for start in range(0, len(entries), 36):
        px = np.full((256, 256, 4), 255, dtype=np.uint8)
        page = len(textures)
        for slot, idx in enumerate(entries[start:start + 36]):
            image, _, _ = texture_pixels(sources[idx], 32)
            # Upscale small source images to a predictable slot size (nearest).
            image = image[np.arange(32) * image.shape[0] // 32][:, np.arange(32) * image.shape[1] // 32]
            x, y = (slot % 6) * 40 + 4, (slot // 6) * 40 + 4
            px[y-4:y+36, x-4:x+36] = np.pad(image, ((4, 4), (4, 4), (0, 0)), mode='edge')
            mapping[idx] = (page, x, y)
        fmt = ttm.GPU_ETC1 if np.all(px[:, :, 3] == 255) else ttm.GPU_ETC1A4
        textures.append(ttm.Texture(f'street_atlas_{page}', px, fmt, 0, 0, mipmaps=False))
    for idx in sorted(standalone):
        tex = sources[idx]
        sign = _signish(tex)
        # Sign lettering needs the extra pixels; a facade or roof is sharp at 128.
        image, nc, pf = texture_pixels(tex, 256 if sign else 128)
        mapping[idx] = (len(textures), None, None)
        # Sign art with alpha stays 16 bpp (RGBA5551 or RGBA4). ETC1A4's 4x4 blocks
        # turn a thin stroke into a black slash. Opaque faces stay ETC1. No mips on
        # anything with a real alpha channel; a lower mip is where the dark fringe lived.
        opaque = bool(np.all(image[:, :, 3] == 255))
        fmt = extract.choose_format(image, nc, pf, etc1=not (sign and not opaque),
                                    binary_alpha_5551=sign and not opaque)
        textures.append(ttm.Texture(tex.getName(), image, fmt,
                                    extract._WRAP.get(tex.getWrapU(), 2),
                                    extract._WRAP.get(tex.getWrapV(), 2),
                                    mipmaps=opaque))
    result = []
    for batches in chunks:
        groups = {}
        for b in batches:
            page, x, y = mapping[b.texture] if b.texture is not None else (None, None, None)
            # Sign quads are often wound for one side only. A culled half reads as a
            # black diagonal across the board; draw them from both sides.
            two = b.two_sided or (b.texture is not None and _signish(sources[b.texture]))
            key = (page, b.blend, two, b.tint, b.layer)
            parts = groups.setdefault(key, [])
            if x is None:
                parts.append(b.verts[b.indices].copy())
                continue
            tex = sources[b.texture]
            repeat = (tex.getWrapU() == tex.WM_repeat, tex.getWrapV() == tex.WM_repeat)
            for tri in b.verts[b.indices.reshape(-1, 3)]:
                xr, yr = tile_ranges(tri, tex)
                for u in xr:
                    for v in yr:
                        poly = list(tri)
                        for a, k, rep in ((3, u, repeat[0]), (4, v, repeat[1])):
                            if rep or k == 0:
                                poly = clip(clip(poly, a, k, True), a, k + 1, False) if poly else []
                            elif k < 0:
                                poly = clip(poly, a, 0, False) if poly else []
                            else:
                                poly = clip(poly, a, 1, True) if poly else []
                        if len(poly) < 3:
                            continue
                        p = np.array(poly)
                        uv = p[:, 3:5].copy()
                        for axis, k in enumerate((u, v)):
                            uv[:, axis] = uv[:, axis] - k if repeat[axis] else np.clip(uv[:, axis], 0, 1)
                        p[:, 3] = (x + uv[:, 0] * 32) / 256
                        p[:, 4] = (256 - y - 32 + uv[:, 1] * 32) / 256
                        parts.append(np.array([p[j] for k in range(1, len(p)-1) for j in (0, k, k+1)]))
        # Merge mixed cull-state groups by enabling two-sided rendering only where
        # an atlas/state already contains both kinds. This cannot hide any faces.
        for key in list(groups):
            tex, blend, two, tint, layer = key
            other = (tex, blend, True, tint, layer)
            if not two and other in groups:
                groups[other].extend(groups.pop(key))
        merged = []
        for (tex, blend, two, tint, layer), parts in sorted(groups.items(), key=lambda kv: (kv[0][1], kv[0][4], str(kv[0]))):
            if not parts:
                continue
            verts = np.concatenate(parts)
            for start in range(0, len(verts), 65535):
                vs = verts[start:start+65535]
                merged.append(ttm.Batch(tex, blend, two, tint, layer, vs, np.arange(len(vs))))
        result.append(merged)
    return textures, result


def rebase_large_uvs(batch, textures):
    """Split oversized repeats into <=16-tile pieces before TTM's int16 UV packing.

    Unlike clamping, clipping and subtracting whole repeats preserves the image.
    This handles Burrrgh terrain whose individual triangles span >32 repeats.
    """
    b=batch
    if b.texture is None:
        return b
    tex=textures[b.texture]
    verts=b.verts.copy()
    for axis,wrap in ((3,tex.wrap_s),(4,tex.wrap_t)):
        if wrap==0:
            verts[:,axis]=np.clip(verts[:,axis],0,1)
    pieces=[]
    for tri in verts[b.indices.reshape(-1,3)]:
        lo,hi=tri[:,3:5].min(axis=0),tri[:,3:5].max(axis=0)
        if np.all(hi-lo<=30):
            p=tri.copy()
            p[:,3:5]-=np.floor(lo)
            pieces.append(p)
            continue
        ranges=[range(math.floor(a/16),max(math.floor(a/16),math.ceil(z/16)-1)+1) for a,z in zip(lo,hi)]
        if len(ranges[0])*len(ranges[1])>4096:
            raise ValueError('texture repeat span is too large to preserve safely')
        for u in ranges[0]:
            for v in ranges[1]:
                poly=list(tri)
                for axis,k in ((3,u),(4,v)):
                    poly=clip(clip(poly,axis,k*16,True),axis,(k+1)*16,False) if poly else []
                if len(poly)<3:
                    continue
                p=np.array(poly)
                p[:,3:5]-=np.floor(p[:,3:5].min(axis=0))
                pieces.extend(np.array([p[0],p[k],p[k+1]]) for k in range(1,len(p)-1))
    if not pieces:
        return b
    verts=np.concatenate(pieces)
    return ttm.Batch(b.texture,b.blend,b.two_sided,b.tint,b.layer,verts,np.arange(len(verts)))
