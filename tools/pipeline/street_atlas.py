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


class Extractor(extract.Extractor):
    def __init__(self):
        super().__init__(layer_rules=__import__('assemble').LAYER_RULES)
        self.sources = []

    def _texture(self, tex):
        key = tex.getFullpath().getFullpath() or tex.getName()
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


def build(sources, chunks):
    """Return textures and transformed chunk batches, with shared atlas indices."""
    # Keep pathological UV tiling separate, bounded in both memory and bake time.
    standalone = set()
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
            image, _, _ = extract.texture_pixels(sources[idx], 32)
            # Upscale small source images to a predictable slot size (nearest).
            image = image[np.arange(32) * image.shape[0] // 32][:, np.arange(32) * image.shape[1] // 32]
            x, y = (slot % 6) * 40 + 4, (slot // 6) * 40 + 4
            px[y-4:y+36, x-4:x+36] = np.pad(image, ((4, 4), (4, 4), (0, 0)), mode='edge')
            mapping[idx] = (page, x, y)
        fmt = ttm.GPU_ETC1 if np.all(px[:, :, 3] == 255) else ttm.GPU_ETC1A4
        textures.append(ttm.Texture(f'street_atlas_{page}', px, fmt, 0, 0, mipmaps=False))
    for idx in sorted(standalone):
        image, nc, pf = extract.texture_pixels(sources[idx], 128)
        mapping[idx] = (len(textures), None, None)
        textures.append(ttm.Texture(sources[idx].getName(), image,
                                    extract.choose_format(image, nc, pf),
                                    extract._WRAP.get(sources[idx].getWrapU(), 2),
                                    extract._WRAP.get(sources[idx].getWrapV(), 2)))
    result = []
    for batches in chunks:
        groups = {}
        for b in batches:
            page, x, y = mapping[b.texture] if b.texture is not None else (None, None, None)
            key = (page, b.blend, b.two_sided, b.tint, b.layer)
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
