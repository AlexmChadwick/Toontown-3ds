"""Shared atlas pages (256px unless page= says otherwise); repeat UV triangles are clipped at tile boundaries.

The default (what existing callers and the atlas unit test use) is 32px tiles,
four-pixel gutters and a base mip only. bake_street passes a near profile:
64px tiles, an 8px gutter and mip levels downsampled per tile so the
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


# A state drawn both one- and two-sided becomes one one-sided draw when it has at
# most this many two-sided triangles. A draw costs about as much of the frame
# budget (150 draws, 15k triangles) as a hundred triangles.
FOLD_TWO_SIDED_MAX = 96


def fold_two_sided(groups, cull):
    """Merges each state's two-sided triangles into its one-sided draw, culling exactly.

    groups: state key -> list of flat triangle vertex arrays (n*3, 9); key[cull] is
    the two-sided flag. Each two-sided triangle is added again with reversed winding,
    so the merged draw culls like the original: a one-sided face never shows its
    back. (Drawing the one-sided faces two-sided instead showed awnings and roofs from
    behind, and the inward wall of the pet shop bowl hid its fish.) States with more
    two-sided triangles than FOLD_TWO_SIDED_MAX keep both draws.
    """
    for key in list(groups):
        if not key[cull]:
            continue
        one = key[:cull] + (False,) + key[cull + 1:]
        if one not in groups or not groups[one]:
            continue
        parts = groups[key]
        if sum(len(p) for p in parts) // 3 > FOLD_TWO_SIDED_MAX:
            continue
        del groups[key]
        groups[one].extend(parts + [p.reshape(-1, 3, p.shape[-1])[:, ::-1].reshape(-1, p.shape[-1])
                                    for p in parts])


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
          large_ground=False, encode=True, page=256, repeat_standalone=False):
    """Return textures and transformed chunk batches, with shared atlas indices.

    encode=False skips pixel reads and ETC1 so a far-LOD spacing search can count
    triangles. The default arguments match the historical 32px, single-mip atlas.
    repeat_standalone puts every texture a triangle repeats across on its own
    wrapping texture instead of clipping it per repeat (one draw, no new triangles).
    """
    if tile < 8 or gutter < 1 or mip_levels < 1:
        raise ValueError('invalid atlas tile')
    span = tile + 2 * gutter
    cols = page // span
    if cols < 1:
        raise ValueError(f'tile does not fit a {page} page')
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
                if len(xr) * len(yr) > (1 if repeat_standalone else 32):
                    standalone.add(b.texture)
                    break
    textures, mapping = [], {}
    strips, rows = {}, []
    if repeat_standalone:
        stride, top = _strip_stride(tile, gutter, mip_levels)
        rows = _strip_picks(sources, chunks, standalone, stride)
        standalone -= set(rows)
    frequency = {i: sum(any(b.texture == i for b in bs) for bs in chunks) for i in range(len(sources))}
    entries = sorted((i for i in range(len(sources)) if i not in standalone and i not in rows),
                     key=lambda i: (-frequency[i], i))
    # Strip rows share the first page (bottom rows, u wrap) when the tiles leave room.
    strip_y = page - len(rows) * stride if rows else page
    shared = bool(rows) and len(entries) <= cols * (strip_y // span)
    if rows and not shared:
        strips = _strip_page(sources, rows, tile, stride, top, mip_levels, textures, encode)
    for start in range(0, max(len(entries), 1 if shared else 0), per_page):
        px = np.full((page, page, 4), 255, dtype=np.uint8)
        index = len(textures)
        slots = []
        with_strips = shared and start == 0
        if with_strips:
            strips = {idx: (index, strip_y + row * stride + top, tile, page) for row, idx in enumerate(rows)}
            if encode:
                _paint_strips(px, sources, rows, strip_y, stride, top, tile)
        for slot, idx in enumerate(entries[start:start + per_page]):
            x, y = (slot % cols) * span + gutter, (slot // cols) * span + gutter
            if encode:
                image, _, _ = texture_pixels(sources[idx], tile)
                # Upscale small source images to a predictable slot size (nearest).
                image = image[np.arange(tile) * image.shape[0] // tile][:, np.arange(tile) * image.shape[1] // tile]
                px[y - gutter:y + tile + gutter, x - gutter:x + tile + gutter] = np.pad(
                    image, ((gutter, gutter), (gutter, gutter), (0, 0)), mode='edge')
            slots.append((x, y, tile, gutter))
            mapping[idx] = (index, x, y)
        if encode:
            fmt = ttm.GPU_ETC1 if np.all(px[:, :, 3] == 255) else ttm.GPU_ETC1A4
            levels = _tile_mips(px, slots, mip_levels) if mip_levels > 1 else None
            if with_strips and levels:
                _strip_levels(levels, strip_y)
            # Tiles never sample past their gutters, so u wrap only changes the strips.
            textures.append(ttm.Texture(f'street_atlas_{index}', px, fmt,
                                        ttm.GPU_REPEAT if with_strips else 0, 0, mipmaps=False, levels=levels))
        else:
            textures.append(type('AtlasRef', (), {'wrap_s': ttm.GPU_REPEAT if with_strips else 0, 'wrap_t': 0,
                                                  'mips': mip_levels})())
    for idx in sorted(standalone):
        size = 256 if sign_text.is_font_texture(sources[idx]) else standalone_size
        mapping[idx] = (len(textures), None, None)
        if encode:
            image, nc, pf = texture_pixels(sources[idx], size)
            ws = extract._WRAP.get(sources[idx].getWrapU(), 2)
            wt = extract._WRAP.get(sources[idx].getWrapV(), 2)
            textures.append(ttm.Texture(sources[idx].getName(), image, extract.choose_format(image, nc, pf),
                                        ws, wt, levels=_repeat_mips(image, ws, wt) if repeat_standalone else None))
        else:
            wrap = extract._WRAP.get(sources[idx].getWrapU(), 2)
            textures.append(type('AtlasRef', (), {'wrap_s': wrap, 'wrap_t': wrap, 'mips': 1})())
    result = []
    for batches in chunks:
        groups = {}
        for b in batches:
            if b.texture in strips:
                pg, y, th, ph = strips[b.texture]
                p = b.verts[b.indices].copy()
                p[:, 4] = (ph - y - th + np.clip(p[:, 4], 0.0, 1.0) * th) / ph
                groups.setdefault((pg, b.blend, b.two_sided, b.tint, b.layer), []).append(p)
                continue
            pg, x, y = mapping[b.texture] if b.texture is not None else (None, None, None)
            key = (pg, b.blend, b.two_sided, b.tint, b.layer)
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
                        p[:, 3] = (x + uv[:, 0] * tile) / page
                        p[:, 4] = (page - y - tile + uv[:, 1] * tile) / page
                        parts.append(np.array([p[j] for k in range(1, len(p)-1) for j in (0, k, k+1)]))
        fold_two_sided(groups, 2)
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


STRIP_ROWS_MAX = 256


def _strip_stride(tile, gutter, mip_levels):
    """Row stride and content offset of a strip row, 4px (ETC block) aligned at every mip."""
    align = 4 << (mip_levels - 1)
    stride = -(-(tile + 2 * gutter) // align) * align
    return stride, (stride - tile) // 2


def _strip_picks(sources, chunks, candidates, stride):
    """Textures that repeat only along u, which a full-width page row can hold.

    The row is as wide as its page, so the sampler's u wrap repeats the row while v
    picks the row. All of a chunk's repeating far cutouts (fences, hedges) then share
    its far page and its draw. Textures that repeat along v stay standalone.
    """
    picked = []
    for idx in sorted(candidates):
        tex = sources[idx]
        if sign_text.is_font_texture(tex) or tex.getWrapU() != tex.WM_repeat:
            continue
        ok = True
        for batches in chunks:
            for b in batches:
                if b.texture == idx:
                    v = b.verts[b.indices][:, 4]
                    if float(v.min()) < -0.01 or float(v.max()) > 1.01:
                        ok = False
        if ok and (len(picked) + 1) * stride <= STRIP_ROWS_MAX:
            picked.append(idx)
    return picked


def _paint_strips(px, sources, rows, y0, stride, top, tile):
    """Full-width rows from y0, each a texture at tile px high with edge-copied gutters."""
    width = px.shape[1]
    for row, idx in enumerate(rows):
        image, _, _ = texture_pixels(sources[idx], width)
        h, w = image.shape[:2]
        if h > tile and h % tile == 0:
            image = image.reshape(tile, h // tile, w, 4).astype(np.uint16).mean(axis=1).round().astype(np.uint8)
        image = image[np.arange(tile) * image.shape[0] // tile][:, np.arange(width) * w // width]
        y = y0 + row * stride
        px[y:y + stride] = np.pad(image, ((top, stride - tile - top), (0, 0), (0, 0)), mode='edge')


def _strip_levels(levels, y0):
    """Box-filter the strip rows (y0 to the page bottom) of levels[0] into every level."""
    for i in range(1, len(levels)):
        q = levels[i - 1][y0 >> (i - 1):].astype(np.uint16)
        down = (q[0::2, 0::2] + q[1::2, 0::2] + q[0::2, 1::2] + q[1::2, 1::2] + 2) // 4
        levels[i][y0 >> i:] = down.astype(np.uint8)
    return levels


def _strip_page(sources, rows, tile, stride, top, mip_levels, textures, encode):
    """A page of strip rows alone (the chunk's tiles did not leave room for them)."""
    width = 2 * tile
    height = max(8 << (mip_levels - 1), 1 << (len(rows) * stride - 1).bit_length())
    index = len(textures)
    if encode:
        px = np.full((height, width, 4), 255, dtype=np.uint8)
        _paint_strips(px, sources, rows, 0, stride, top, tile)
        levels = [px]
        for _ in range(mip_levels - 1):
            levels.append(np.zeros((levels[-1].shape[0] // 2, levels[-1].shape[1] // 2, 4), dtype=np.uint8))
        _strip_levels(levels, 0)
        fmt = ttm.GPU_ETC1 if np.all(px[:, :, 3] == 255) else ttm.GPU_ETC1A4
        textures.append(ttm.Texture(f'street_strip_{index}', px, fmt, ttm.GPU_REPEAT, ttm.GPU_CLAMP_TO_EDGE,
                                    mipmaps=False, levels=levels))
    else:
        textures.append(type('AtlasRef', (), {'wrap_s': ttm.GPU_REPEAT, 'wrap_t': ttm.GPU_CLAMP_TO_EDGE,
                                              'mips': mip_levels})())
    return {idx: (index, row * stride + top, tile, height) for row, idx in enumerate(rows)}


def strip_rows_needed(sources, chunks, tile, gutter, mip_levels):
    """Pixel rows build(repeat_standalone=True) reserves for strip rows."""
    stride, _ = _strip_stride(tile, gutter, mip_levels)
    candidates = set()
    for batches in chunks:
        for b in batches:
            if b.texture is not None and any(len(xr) * len(yr) > 1 for xr, yr in
                                             (tile_ranges(t, sources[b.texture]) for t in b.verts[b.indices.reshape(-1, 3)])):
                candidates.add(b.texture)
    return len(_strip_picks(sources, chunks, candidates, stride)) * stride


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


# ---- chunk-local near atlas ----
# The shared atlas above puts every texture in a 64px tile of a 256 page holding
# nine tiles, and each chunk uploads every page it touches. A chunk that used one
# tile of a page paid for all nine, and a 256px palette (the TTC tunnel rock is a
# corner of phase_3.5_palette_3cmla_1) shrank to 64px: a smeared beige blob. Here
# each chunk gets its own pages. A tile holds only the UV rectangle that chunk
# samples, sized by how many world units it covers (texels per unit), capped by
# the source resolution. Opaque and alpha textures use separate pages so opaque
# tiles stay 4 bpp ETC1.
LOCAL_PAGES = ((128, 128), (256, 128), (256, 256), (512, 256), (512, 512))
LOCAL_MIN_TILE = 16
LOCAL_MAX_TILE = 240


def _pow2_ceil(n):
    return 1 << max(0, math.ceil(math.log2(max(n, 1))))


class _SourceCache:
    def __init__(self, sources):
        self.sources, self.px = sources, {}

    def get(self, idx):
        if idx not in self.px:
            self.px[idx] = texture_pixels(self.sources[idx], 1024)[0]
        return self.px[idx]


def _clipped_parts(tri, tex):
    """The tile-range pieces of one triangle, each with its UV moved into [0, 1]."""
    repeat = (tex.getWrapU() == tex.WM_repeat, tex.getWrapV() == tex.WM_repeat)
    xr, yr = tile_ranges(tri, tex)
    out = []
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
            for axis, k in enumerate((u, v)):
                p[:, 3 + axis] = p[:, 3 + axis] - k if repeat[axis] else np.clip(p[:, 3 + axis], 0, 1)
            out.append(np.array([p[j] for k in range(1, len(p) - 1) for j in (0, k, k + 1)]))
    return out


class ViewDensity:
    """Texels per world unit a triangle needs, from how close the camera gets.

    The top screen is 400 px over a 55 degree fov: about 384 / d px per world unit
    at distance d. view_points are walkable floor samples raised to eye height;
    quality scales that (1 = one texel per screen pixel at the closest approach).
    """
    def __init__(self, view_points, quality, near=12.0, far=160.0):
        self.pts = np.asarray(view_points, dtype=np.float64).reshape(-1, 3)
        self.quality, self.near, self.far = quality, near, far

    def __call__(self, centroids):
        if not len(self.pts):
            return np.full(len(centroids), self.quality * 384.0 / self.near)
        d = np.empty(len(centroids))
        for s in range(0, len(centroids), 256):
            c = centroids[s:s + 256]
            d[s:s + 256] = np.sqrt(((c[:, None, :] - self.pts[None, :, :]) ** 2).sum(-1).min(axis=1))
        return self.quality * 384.0 / np.clip(d, self.near, self.far)


def _texel_need(parts, density):
    """Texels per UV unit along u and v: area-weighted 90th percentile of
    (world units per UV unit) * (texels per world unit at that triangle)."""
    rows, cents = [], []
    for p in parts:
        for t in p.reshape(-1, 3, 9):
            e = np.stack((t[1, :3] - t[0, :3], t[2, :3] - t[0, :3]), axis=1)      # 3x2 world
            d = np.stack((t[1, 3:5] - t[0, 3:5], t[2, 3:5] - t[0, 3:5]), axis=1)  # 2x2 uv
            det = d[0, 0] * d[1, 1] - d[0, 1] * d[1, 0]
            area = float(np.linalg.norm(np.cross(e[:, 0], e[:, 1]))) / 2
            if abs(det) < 1e-9 or area < 1e-6:
                continue
            j = e @ np.linalg.inv(d)
            rows.append((float(np.linalg.norm(j[:, 0])), float(np.linalg.norm(j[:, 1])), area))
            cents.append(t[:, :3].mean(axis=0))
    if not rows:
        return 0.0, 0.0
    r = np.array(rows)
    k = density(np.array(cents))
    r[:, 0] *= k
    r[:, 1] *= k
    out = []
    for axis in (0, 1):
        order = np.argsort(r[:, axis])
        cum = np.cumsum(r[order, 2])
        out.append(float(r[order[np.searchsorted(cum, 0.9 * cum[-1])], axis]))
    return out[0], out[1]


def _skyline_fit(skyline, w, h, pw, ph):
    """Bottom-left position for a w x h rect on the skyline [(x, y, width)], or None."""
    best = None
    for i in range(len(skyline)):
        x = skyline[i][0]
        if x + w > pw:
            break
        y, span, k = 0, 0, i
        while span < w:
            y = max(y, skyline[k][1])
            span += skyline[k][2]
            k += 1
        if y + h <= ph and (best is None or (y + h, x) < (best[1] + h, best[0])):
            best = (x, y)
    return best


def _skyline_add(skyline, x, y, w, h):
    out = []
    for sx, sy, sw in skyline:
        if sx + sw <= x or sx >= x + w:
            out.append((sx, sy, sw))
            continue
        if sx < x:
            out.append((sx, sy, x - sx))
        if sx + sw > x + w:
            out.append((x + w, sy, sx + sw - x - w))
    out.append((x, y + h, w))
    out.sort()
    merged = []
    for seg in out:
        if merged and merged[-1][1] == seg[1] and merged[-1][0] + merged[-1][2] == seg[0]:
            merged[-1] = (merged[-1][0], seg[1], merged[-1][2] + seg[2])
        else:
            merged.append(seg)
    return merged


def _pack_page(rects, pw, ph):
    skyline, placed, left = [(0, 0, pw)], {}, []
    for key, w, h in rects:
        at = _skyline_fit(skyline, w, h, pw, ph) if w <= pw and h <= ph else None
        if at is None:
            left.append((key, w, h))
            continue
        placed[key] = at
        skyline = _skyline_add(skyline, at[0], at[1], w, h)
    return placed, left


def _shelf_pack(rects, page_sizes):
    """rects: [(key, w, h)], multiples of 16. Skyline bottom-left packing; each page
    is the smallest of page_sizes that holds everything left, else the largest.
    Returns [(page_w, page_h, {key: (x, y)})]."""
    pages = []
    todo = sorted(rects, key=lambda r: (-r[2], -r[1], str(r[0])))
    while todo:
        for pw, ph in page_sizes:
            placed, left = _pack_page(todo, pw, ph)
            if not left:
                break
        if not placed:
            raise ValueError('atlas tile larger than the biggest page')
        pages.append((pw, ph, placed))
        todo = left
    return pages


def build_local(sources, chunks, *, density=0.5, view_points=(), gutter=8, mip_levels=3, standalone_size=256,
                large_ground=True, pages=LOCAL_PAGES, view_near=12.0, view_far=160.0, name='local_atlas'):
    """Per-chunk textures and batches: chunk-local pages (see the note above).

    Returns (per-chunk texture lists, per-chunk batches indexing that chunk's list,
    stats). Standalone textures (fonts, large ground, heavy repeats) are one shared
    ttm.Texture object, so the TZ v3 pool stores each once.
    """
    from PIL import Image
    if gutter < 1 << (mip_levels - 1):
        raise ValueError('gutter too small for the mip chain')
    fixed = {i for i, t in enumerate(sources) if sign_text.is_font_texture(t)}
    ground = _large_ground_ids(chunks) if large_ground else set()
    for batches in chunks:
        for b in batches:
            if b.texture is None or b.texture in fixed:
                continue
            for tri in b.verts[b.indices.reshape(-1, 3)]:
                xr, yr = tile_ranges(tri, sources[b.texture])
                if len(xr) * len(yr) > 32:
                    fixed.add(b.texture)
                    break
    shared, repeats = {}, {}
    wall_like = _wall_like_textures(chunks)

    def standalone_texture(idx):
        if idx not in shared:
            font = sign_text.is_font_texture(sources[idx])
            size = 256 if font else standalone_size
            image, nc, pf = texture_pixels(sources[idx], size)
            ws = extract._WRAP.get(sources[idx].getWrapU(), 2)
            wt = extract._WRAP.get(sources[idx].getWrapV(), 2)
            k = 1 if font or idx not in wall_like else _pretile_factor(image, ws, wt, wall_like[idx], standalone_size)
            if k > 1:
                image = np.tile(image, (k, k, 1))
            repeats[idx] = k
            shared[idx] = ttm.Texture(sources[idx].getName(), image, extract.choose_format(image, nc, pf),
                                      ws, wt, levels=_repeat_mips(image, ws, wt))
        return shared[idx]
    need = ViewDensity(view_points, density, near=view_near, far=view_far)
    cache = _SourceCache(sources)
    alpha_of = {}
    out_tex, out_batches, stats = [], [], []
    for batches in chunks:
        # Pieces per texture, kept with their batch state. A large-ground texture
        # whose repeats would multiply its triangles stays standalone in this
        # chunk; one that is only a palette corner on a big flat face is atlased.
        pieces, tri_count = {}, {}
        for bi, b in enumerate(batches):
            if b.texture is None or b.texture in fixed:
                continue
            tex = sources[b.texture]
            for tri in b.verts[b.indices.reshape(-1, 3)]:
                parts = _clipped_parts(tri, tex)
                pieces.setdefault(b.texture, []).extend((bi, p) for p in parts)
                tri_count[b.texture] = tri_count.get(b.texture, 0) + 1
        standalone = set(fixed)
        for idx in list(pieces):
            made = sum(len(p) // 3 for _, p in pieces[idx])
            if idx in ground and made > 1.25 * tri_count[idx] + 8:
                standalone.add(idx)
                del pieces[idx]
        tiles = {}
        for idx, plist in pieces.items():
            src = cache.get(idx)
            sh, sw = src.shape[:2]
            uv = np.concatenate([p[:, 3:5] for _, p in plist])
            lo, hi = uv.min(axis=0), uv.max(axis=0)
            # One source texel of margin keeps bilinear filtering inside real pixels.
            lo = np.maximum(lo - np.array([1.0 / sw, 1.0 / sh]), 0)
            hi = np.minimum(hi + np.array([1.0 / sw, 1.0 / sh]), 1)
            ext = np.maximum(hi - lo, 1e-4)
            nu, nv = _texel_need([p for _, p in plist], need)
            size = []
            for texels, e, n in ((nu, ext[0], sw), (nv, ext[1], sh)):
                # Tiles need not be powers of two (only pages are): multiples of
                # 16, at most 240 so a tile and its gutters span 256.
                want = min(texels * e, n * e)
                size.append(int(min(max(math.ceil(want / 16) * 16, LOCAL_MIN_TILE), LOCAL_MAX_TILE)))
            tw, th = size
            box = (lo[0] * sw, (1 - hi[1]) * sh, hi[0] * sw, (1 - lo[1]) * sh)
            shrink = (box[2] - box[0]) > tw * 1.5 or (box[3] - box[1]) > th * 1.5
            image = np.asarray(Image.fromarray(src, 'RGBA').resize(
                (tw, th), Image.BOX if shrink else Image.BICUBIC, box=box))
            # By the crop, not the source: a palette page mixes opaque art and
            # cutouts, and an opaque crop belongs on a 4 bpp ETC1 page.
            alpha_of[idx] = bool(np.any(src[int(box[1]):max(int(box[1]) + 1, math.ceil(box[3])),
                                            int(box[0]):max(int(box[0]) + 1, math.ceil(box[2])), 3] < 255))
            tiles[idx] = (lo, hi, tw, th, image)
        textures = []
        mapping = {}
        for alpha in (False, True):
            keys = [i for i in sorted(tiles) if alpha_of[i] == alpha]
            if not keys:
                continue
            rects = [(i, tiles[i][2] + 2 * gutter, tiles[i][3] + 2 * gutter) for i in keys]
            for pw, ph, placed in _shelf_pack(rects, pages):
                px = np.full((ph, pw, 4), 255, dtype=np.uint8)
                slots = []
                for i, (x0, y0) in placed.items():
                    lo, hi, tw, th, im = tiles[i]
                    x, y = x0 + gutter, y0 + gutter
                    px[y0:y0 + th + 2 * gutter, x0:x0 + tw + 2 * gutter] = np.pad(
                        np.asarray(im), ((gutter, gutter), (gutter, gutter), (0, 0)), mode='edge')
                    slots.append((x, y, (tw, th), gutter))
                    mapping[i] = (len(textures), x, y, tw, th, pw, ph)
                levels = _tile_mips_rect(px, slots, mip_levels)
                fmt = ttm.GPU_ETC1A4 if alpha else ttm.GPU_ETC1
                textures.append(ttm.Texture(f'{name}_{len(out_tex)}_{len(textures)}', px, fmt, 0, 0,
                                            mipmaps=False, levels=levels))
        for idx in sorted({b.texture for b in batches if b.texture in standalone}):
            mapping[idx] = (len(textures), None, None, None, None, None, None)
            textures.append(standalone_texture(idx))
        groups = {}
        for b in batches:
            pg = mapping[b.texture][0] if b.texture is not None else None
            parts = groups.setdefault((pg, b.blend, b.two_sided, b.tint, b.layer), [])
            if b.texture is None or mapping[b.texture][1] is None:
                q = b.verts[b.indices].copy()
                q[:, 3:5] /= repeats.get(b.texture, 1)
                parts.append(q)
        for idx, plist in pieces.items():
            pg, x, y, tw, th, pw, ph = mapping[idx]
            lo, hi = tiles[idx][0], tiles[idx][1]
            for bi, p in plist:
                b = batches[bi]
                q = p.copy()
                q[:, 3] = (x + (q[:, 3] - lo[0]) / (hi[0] - lo[0]) * tw) / pw
                q[:, 4] = (ph - y - th + (q[:, 4] - lo[1]) / (hi[1] - lo[1]) * th) / ph
                groups[(pg, b.blend, b.two_sided, b.tint, b.layer)].append(q)
        fold_two_sided(groups, 2)
        merged = []
        for (tex, blend, two, tint, layer), parts in sorted(groups.items(), key=lambda kv: (kv[0][1], kv[0][4], str(kv[0]))):
            if not parts:
                continue
            verts = np.concatenate(parts)
            for start in range(0, len(verts), 65535):
                vs = verts[start:start + 65535]
                merged.append(ttm.Batch(tex, blend, two, tint, layer, vs, np.arange(len(vs))))
        out_tex.append(textures)
        out_batches.append(merged)
        stats.append({'tiles': len(tiles), 'pages': sum(1 for t in textures if t not in shared.values()),
                      'tile_px': sum(t[2] * t[3] for t in tiles.values()),
                      'sizes': sorted(((sources[i].getName(), t[2], t[3]) for i, t in tiles.items()),
                                      key=lambda r: -r[1] * r[2])})
    return out_tex, out_batches, stats


def _wall_like_textures(chunks):
    """Textures to pre-tile: mostly not flat ground by area (|normal z| <= 0.95: walls,
    roofs, awnings), and every UV along a clamped axis inside the texture (0..1)."""
    area, inside = {}, {}
    for batches in chunks:
        for b in batches:
            if b.texture is None:
                continue
            tri = b.verts[b.indices.reshape(-1, 3)]
            p = tri[:, :, :3].astype(np.float64)
            n = np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0])
            a = np.linalg.norm(n, axis=1)
            nz = np.abs(n[:, 2]) / np.maximum(a, 1e-12)
            w, g = area.get(b.texture, (0.0, 0.0))
            area[b.texture] = (w + float(a[nz <= 0.95].sum()), g + float(a[nz > 0.95].sum()))
            uv = tri[:, :, 3:5].reshape(-1, 2)
            lo, hi = inside.get(b.texture, (np.full(2, np.inf), np.full(2, -np.inf)))
            inside[b.texture] = (np.minimum(lo, uv.min(axis=0)), np.maximum(hi, uv.max(axis=0)))
    return {i: inside[i] for i, (w, g) in area.items() if w > g}


def _pretile_factor(image, wrap_s, wrap_t, uv_range, limit=256):
    """Copies per axis (power of two, at most 4) to store a small repeating texture as;
    its UVs are divided by the same factor.

    PICA mips stop at 8x8, so a 64px texture has no level past Panda's level 3 and
    its last level must stand for every coarser one (_repeat_mips: the mean colour).
    As 4x4 copies in 256px it keeps the same texel density and has two more real
    levels: the TTC trolley and gag shop awnings keep their stripes at 125 units on
    New 3DS instead of a flat band, and still end on the mean colour where Panda would
    (seen edge-on). A clamped axis is tiled too when its UVs stay inside 0..1 (the
    awnings repeat along u only); it then blends a half texel into the next copy at its
    top edge. Ground textures are left alone (_wall_like_textures): they cover most of a
    view, and 16x their bytes does not fit the retain budget.
    """
    if ttm.GPU_REPEAT not in (wrap_s, wrap_t):
        return 1
    lo, hi = uv_range
    for axis, wrap in ((0, wrap_s), (1, wrap_t)):
        if wrap != ttm.GPU_REPEAT and (lo[axis] < -0.05 or hi[axis] > 1.05):
            return 1
    k = 1
    while max(image.shape[:2]) * k * 2 <= limit and k < 4:
        k *= 2
    return k


def _repeat_mips(image, wrap_s, wrap_t):
    """Mip chain of a standalone texture; a repeating one ends on its mean colour.

    PICA levels stop at 8x8 and the sampler clamps there. Panda goes on to 1x1, so a
    texture repeated many times across a face seen edge-on (the gag shop awning end,
    awning_edge nine times along a sliver) averages out on PC but strobes its 8x8
    pattern on the 3DS. The last level takes the colour the levels below it would
    reach; alpha keeps its pattern so cutout coverage is unchanged.
    """
    levels = ttm.mip_chain(image)
    if len(levels) > 1 and ttm.GPU_REPEAT in (wrap_s, wrap_t):
        last = levels[-1].copy()
        rgb = levels[0][..., :3].reshape(-1, 3).astype(np.float64).mean(axis=0)
        last[..., :3] = np.round(rgb).astype(np.uint8)
        levels[-1] = last
    return levels


def _tile_mips_rect(page, slots, levels):
    """_tile_mips for rectangular tiles: slots are (x, y, (w, h), gutter)."""
    pages = [page]
    cur = slots
    for _ in range(levels - 1):
        prev = pages[-1]
        nxt = np.zeros((prev.shape[0] // 2, prev.shape[1] // 2, 4), dtype=np.uint8)
        halved = []
        for x, y, (tw, th), g in cur:
            x0, y0 = x - g, y - g
            region = prev[y0:y + th + g, x0:x + tw + g]
            h, w = region.shape[:2]
            if h % 2 or w % 2 or x0 % 2 or y0 % 2 or g < 1:
                raise ValueError('atlas tile region is not even')
            acc = region.astype(np.uint16)
            down = (acc.reshape(h // 2, 2, w // 2, 2, 4).sum(axis=(1, 3)) + 2) // 4
            nxt[y0 // 2:y0 // 2 + down.shape[0], x0 // 2:x0 // 2 + down.shape[1]] = down.astype(np.uint8)
            halved.append((x // 2, y // 2, (tw // 2, th // 2), g // 2))
        pages.append(nxt)
        cur = halved
    return pages
