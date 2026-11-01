"""Texture-preserving far shells, without voxel snapping or deleted decals.

Rasterize each original triangle's source UVs into a guttered atlas patch. Repeat
UVs are sampled offline, so atlas merging adds no geometry at wrap boundaries.
Positions, vertex colours, winding, alpha and decal depth layers remain intact.
This is deliberately conservative: a mesh that cannot fit fails the bake.
"""
import hashlib
import numpy as np
import street_atlas
import ttm


def sample(image, uv, repeat):
    """Bilinear source sampling, matching PICA's normalized repeat/clamp UVs."""
    h, w = image.shape[:2]
    u, v = uv[..., 0], 1-uv[..., 1]
    x, y = u*w-.5, v*h-.5
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = (x-x0)[..., None], (y-y0)[..., None]
    def at(ix, iy):
        ix = ix % w if repeat[0] else np.clip(ix, 0, w-1)
        iy = iy % h if repeat[1] else np.clip(iy, 0, h-1)
        return image[iy, ix].astype(float)
    return np.rint((at(x0, y0)*(1-fx)+at(x0+1, y0)*fx)*(1-fy) +
                   (at(x0, y0+1)*(1-fx)+at(x0+1, y0+1)*fx)*fy).astype(np.uint8)


def patch(image, tri, size, repeat):
    # Store the triangle along half a square. Extend the diagonal boundary into
    # the unused half, so linear filtering and per-patch mips never sample black.
    yy, xx = np.mgrid[:size, :size].astype(float)/(size-1)
    total = np.maximum(xx+yy, 1)
    xx, yy = xx/total, yy/total
    uv = tri[0, 3:5] + xx[..., None]*(tri[1, 3:5]-tri[0, 3:5]) + yy[..., None]*(tri[2, 3:5]-tri[0, 3:5])
    return sample(image, uv, repeat)


def build(sources, chunks, *, tile=16, decal_tile=32, page_size=512, encode=True, images=None):
    """Return shared texture pages and merged shells. All dimensions are pixels.

    No geometric reduction: removing near-only ground subdivision already saves
    thousands of triangles. PVS budgets are checked by the caller before encoding.
    The page hash includes mip contents; equal payloads dedupe in TZ v3 as well.
    """
    import sign_text
    pixels = {}
    textures, result = [], []
    page_cache = {}
    direct_cache = {}
    for chunk in chunks:
        # Separate alpha/cull/layer states: one draw per page, no changed blending.
        states = {}
        direct = []
        for batch in chunk:
            is_sign = batch.sign or (batch.layer and batch.texture is not None and
                                     sign_text.is_font_texture(sources[batch.texture]))
            if is_sign:
                direct.append(batch)
                continue
            size = decal_tile if batch.layer else tile
            key = (batch.blend, bool(batch.two_sided), batch.tint, batch.layer, size)
            states.setdefault(key, []).append(batch)
        # Match near-atlas merging: if a state already needs two-sided draws,
        # combine its one-sided faces into the same page without losing faces.
        for key in list(states):
            blend, two, tint, layer, size = key
            other = (blend, True, tint, layer, size)
            if not two and other in states:
                states[other].extend(states.pop(key))
        out = []
        for (blend, two, tint, layer, size), batches in sorted(states.items()):
            gutter = 4
            span = size+2*gutter
            count = sum(len(b.indices)//3 for b in batches)
            side = next((n for n in (128, 256, page_size) if (n//span)**2 >= count), page_size)
            cols = side//span
            has_alpha = False
            for b in batches:
                if b.texture is None:
                    continue
                if b.texture not in pixels:
                    pixels[b.texture] = images[b.texture] if images is not None else street_atlas.texture_pixels(sources[b.texture], 256)[0]
                has_alpha |= bool(np.any(pixels[b.texture][:, :, 3] != 255))
            slots_per_page = cols*cols
            page, slots, vertices = None, [], []
            def finish():
                if not vertices:
                    return
                levels = street_atlas._tile_mips(page, slots, 3) if encode else None
                if encode:
                    digest = hashlib.sha256(b''.join(p.tobytes() for p in levels)).digest()
                    page_id = page_cache.get(digest)
                    if page_id is None:
                        page_id = len(textures)
                        fmt = ttm.GPU_ETC1 if np.all(page[:, :, 3] == 255) else ttm.GPU_ETC1A4
                        textures.append(ttm.Texture(f'far_shell_{page_id}', page, fmt, 0, 0,
                                                    mipmaps=False, levels=levels))
                        page_cache[digest] = page_id
                else:
                    page_id = len(textures)
                    textures.append(type('PageRef', (), {'byte_size':
                        side*side*(2 if has_alpha else 1)*21//32})())
                vs = np.concatenate(vertices)
                out.append(ttm.Batch(page_id, blend, two, tint, layer, vs, np.arange(len(vs))))
            for batch in batches:
                if batch.texture is not None and batch.texture not in pixels:
                    if images is not None:
                        pixels[batch.texture] = images[batch.texture]
                    elif encode:
                        pixels[batch.texture] = street_atlas.texture_pixels(sources[batch.texture], 256)[0]
                for tri in batch.verts[batch.indices].reshape(-1, 3, 9):
                    if len(slots) == slots_per_page:
                        finish()
                        slots, vertices = [], []
                    if not slots:
                        page = np.full((side, side, 4), 255, dtype=np.uint8)
                    slot = len(slots)
                    x, y = (slot%cols)*span+gutter, (slot//cols)*span+gutter
                    if encode and batch.texture is not None:
                        tex = sources[batch.texture]
                        repeat = (tex.getWrapU() == tex.WM_repeat, tex.getWrapV() == tex.WM_repeat)
                        image = patch(pixels[batch.texture], tri, size, repeat)
                        page[y-gutter:y+size+gutter, x-gutter:x+size+gutter] = np.pad(
                            image, ((gutter, gutter), (gutter, gutter), (0, 0)), mode='edge')
                    slots.append((x, y, size, gutter))
                    vs = tri.copy()
                    # Texel centres; the last one is size-1 away from the first.
                    vs[:, 3:5] = [(x+.5, side-y-.5), (x+size-.5, side-y-.5),
                                  (x+.5, side-y-size+.5)]
                    vs[:, 3:5] /= side
                    vertices.append(vs)
            finish()
        for batch in direct:
            src = batch.texture
            if src not in direct_cache:
                direct_cache[src] = len(textures)
                if encode:
                    import extract
                    image, nc, pf = street_atlas.texture_pixels(sources[src], 256)
                    textures.append(ttm.Texture(sources[src].getName(), image,
                        extract.choose_format(image, nc, pf), 0, 0))
                else:
                    # Font mask at <=256 with a full chain (conservative A8).
                    textures.append(type('PageRef', (), {'byte_size': 87360})())
            out.append(ttm.Batch(direct_cache[src], batch.blend, batch.two_sided, batch.tint,
                                 batch.layer, batch.verts, batch.indices, sign=True))
        out.sort(key=lambda b: (b.blend, b.layer, b.texture))
        result.append(out)
    return textures, result
