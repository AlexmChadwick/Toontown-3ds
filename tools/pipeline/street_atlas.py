"""Shared 256px atlas pages; repeat UV triangles are clipped at tile boundaries.

The default (what existing callers and the atlas unit test use) is 32px tiles,
four-pixel gutters and a base mip only. bake_street passes a near profile:
64px tiles, an 8px gutter and three mip levels downsampled per tile so the
gutter still isolates neighbours. Large ground textures are optionally kept as
standalone pages (256 near, 128 far) instead of the prop atlas. Automatic
whole-page mips are never used: they would bleed across tiles. Large tiled
triangles retain standalone textures. All batches retain blend/cull/decal state.
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


def _large_ground_ids(chunks, min_edge=8.0, min_up=0.5):
    """Textures used by an upward face with an edge of at least min_edge.

    After ground splitting those edges are about 20 units, so the ground page is
    standalone instead of a prop-atlas tile. Small flat props stay in the atlas.
    """
    ids = set()
    for batches in chunks:
        for b in batches:
            if b.texture is None or b.texture in ids:
                continue
            tris = b.verts[b.indices.reshape(-1, 3)]
            for tri in tris:
                n = np.cross(tri[1, :3] - tri[0, :3], tri[2, :3] - tri[0, :3])
                ln = float(np.linalg.norm(n))
                if ln < 1e-8 or n[2] / ln < min_up:
                    continue
                edges = np.linalg.norm(np.stack((tri[1, :3] - tri[0, :3], tri[2, :3] - tri[1, :3],
                                                 tri[0, :3] - tri[2, :3])), axis=1)
                if float(edges.max()) >= min_edge:
                    ids.add(b.texture)
                    break
    return ids


def _tile_mips(page, slots, levels):
    """Box-filter each tile (content plus its edge gutter) into the next mip.

    slots are (x, y, tile, gutter) in this page's pixels. Gutters are a copy of
    the tile edge, so the filter never reaches the neighbouring tile.
    """
    pages = [page]
    cur = slots
    for _ in range(levels - 1):
        prev = pages[-1]
        nxt = np.zeros((prev.shape[0] // 2, prev.shape[1] // 2, 4), dtype=np.uint8)
        halved = []
        for x, y, tile, gutter in cur:
            if gutter < 1 or tile < 2:
                raise ValueError('atlas mip would bleed across tiles; use a wider gutter')
            x0, y0 = x - gutter, y - gutter
            x1, y1 = x + tile + gutter, y + tile + gutter
            region = prev[y0:y1, x0:x1]
            h, w = region.shape[:2]
            if h % 2 or w % 2:
                raise ValueError('atlas tile region is not even')
            acc = region.astype(np.uint16)
            down = (acc.reshape(h // 2, 2, w // 2, 2, 4).sum(axis=(1, 3)) + 2) // 4
            nx, ny = x0 // 2, y0 // 2
            nxt[ny:ny + down.shape[0], nx:nx + down.shape[1]] = down.astype(np.uint8)
            halved.append((x // 2, y // 2, tile // 2, gutter // 2))
        pages.append(nxt)
        cur = halved
    return pages


def build(sources, chunks, *, tile=32, gutter=4, standalone_size=128, mip_levels=1,
          large_ground=False, encode=True, page_size=256):
    """Return textures and transformed chunk batches, with shared atlas indices.

    encode=False skips pixel reads and ETC1 so a far-LOD spacing search can count
    triangles. The default arguments match the historical 32px, single-mip atlas.
    """
    if tile < 8 or gutter < 1 or mip_levels < 1:
        raise ValueError('invalid atlas tile')
    span = tile + 2 * gutter
    if page_size not in (256, 512):
        raise ValueError("atlas page must be 256 or 512")
    cols = page_size // span
    if cols < 1:
        raise ValueError('tile does not fit page')
    per_page = cols * cols
    # Keep pathological UV tiling separate, bounded in both memory and bake time.
    standalone = {i for i, t in enumerate(sources) if sign_text.is_font_texture(t)}
    if large_ground:
        standalone |= _large_ground_ids(chunks)
    for batches in chunks:
        for b in batches:
            if b.texture is None or b.texture in standalone:
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
    for start in range(0, len(entries), per_page):
        px = np.full((page_size, page_size, 4), 255, dtype=np.uint8)
        page = len(textures)
        slots = []
        for slot, idx in enumerate(entries[start:start + per_page]):
            x, y = (slot % cols) * span + gutter, (slot // cols) * span + gutter
            if encode:
                image, _, _ = texture_pixels(sources[idx], tile)
                # Upscale small source images to a predictable slot size (nearest).
                image = image[np.arange(tile) * image.shape[0] // tile][:, np.arange(tile) * image.shape[1] // tile]
                px[y - gutter:y + tile + gutter, x - gutter:x + tile + gutter] = np.pad(
                    image, ((gutter, gutter), (gutter, gutter), (0, 0)), mode='edge')
            slots.append((x, y, tile, gutter))
            mapping[idx] = (page, x, y)
        if encode:
            fmt = ttm.GPU_ETC1 if np.all(px[:, :, 3] == 255) else ttm.GPU_ETC1A4
            levels = _tile_mips(px, slots, mip_levels) if mip_levels > 1 else None
            textures.append(ttm.Texture(f'street_atlas_{page}', px, fmt, 0, 0,
                                        mipmaps=False, levels=levels))
        else:
            textures.append(type('AtlasRef', (), {'wrap_s': 0, 'wrap_t': 0, 'mips': mip_levels})())
    for idx in sorted(standalone):
        size = 256 if sign_text.is_font_texture(sources[idx]) else standalone_size
        mapping[idx] = (len(textures), None, None)
        if encode:
            image, nc, pf = texture_pixels(sources[idx], size)
            textures.append(ttm.Texture(sources[idx].getName(), image,
                                        extract.choose_format(image, nc, pf),
                                        extract._WRAP.get(sources[idx].getWrapU(), 2),
                                        extract._WRAP.get(sources[idx].getWrapV(), 2)))
        else:
            wrap = extract._WRAP.get(sources[idx].getWrapU(), 2)
            textures.append(type('AtlasRef', (), {'wrap_s': wrap, 'wrap_t': wrap, 'mips': 1})())
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
                        p[:, 3] = (x + uv[:, 0] * tile) / page_size
                        p[:, 4] = (page_size - y - tile + uv[:, 1] * tile) / page_size
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
