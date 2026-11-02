#!/usr/bin/env python3
"""Bake all supported streets and playgrounds to self-contained .tz files.

.venv/bin/python -I tools/pipeline/bake_street.py [--zone 2100|2000|all] [--preview]
Outputs: sdmc-out/3ds/toontown3ds/data/zones/{2100,2000}.tz + budget JSON.
No source assets or existing pipeline files are modified. Use <=4 encoder threads.
"""
import argparse
import json
import math
import os
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import tempfile

os.environ['TT_ETC1_THREADS'] = '3'
os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
os.environ['VECLIB_MAXIMUM_THREADS'] = '1'
os.environ['MKL_NUM_THREADS'] = '1'
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
ROOT = HERE.parent.parent
import sign_text


# Near atlas matches source/render/quality.h: sharp ground and props up close.
# The far LOD is a coarse vertex-coloured mesh. Alpha-cut hill cards keep their
# texture: a corner sample of those is a flat green quad. Playground vis
# (--distance 760) plus a 78-unit margin covers the New 3DS draw distance.
# --grid-size is a floor; the baker raises it until the far view fits DRAW_CAP.
# Cog HQs still use the shared 64px atlas (street_atlas.build).
SHARED_NEAR_ATLAS = dict(tile=64, gutter=8, standalone_size=256, mip_levels=3, large_ground=True)
NEAR_ATLAS = dict(density=0.5, gutter=8, standalone_size=256, mip_levels=3, large_ground=True)
OLD_LOD_NEAR = 160.0
NEW_LOD_NEAR = 260.0
# render/quality.h lodMust (Old, New): upgraded first, still inside DRAW_CAP.
LOD_MUST = {OLD_LOD_NEAR: 48.0, NEW_LOD_NEAR: 64.0}


# Sizes the baker may raise a playground or Cog HQ to. --grid-size is the floor:
# a zone whose far view exceeds the draw cap is rebinned until it fits.
GRID_LADDER = (96, 128, 160, 192, 256, 320, 384, 512)


class FarBudgetError(ValueError):
    def __init__(self, tris, draws):
        super().__init__(f'far LOD cannot meet budgets: {tris} triangles, {draws} draws')
        self.tris = tris
        self.draws = draws


def grid_ladder(start):
    """Grid sizes to try, beginning at start and then the coarser rungs above it."""
    start = float(start)
    sizes = [float(s) for s in GRID_LADDER if s >= start - 1e-3]
    if not sizes or abs(sizes[0] - start) > 1e-3:
        sizes = [start] + [float(s) for s in GRID_LADDER if s > start + 1e-3]
    return sizes


def _far_draws(batches):
    """Draws after far batches that share a texture, blend and cull are merged."""
    return len({(b.texture, b.blend, b.two_sided) for b in batches})


def _silhouette_card(image, verts):
    """A backdrop hill card: one alpha-cut picture stretched over a huge quad.

    Sampling a corner texel paints the whole quad, and the alpha test then cuts
    a straight edge through it. That is the flat bright-green slab. The real
    texture has to stay.
    """
    import numpy as np
    alpha = image[:, :, 3]
    if int(alpha.min()) > 16:
        return False
    uv = verts[:, 3:5]
    if float(uv.min()) < -0.05 or float(uv.max()) > 1.05:
        return False
    if float(np.ptp(uv, axis=0).max()) < 0.5:
        return False
    tri = verts.reshape(-1, 3, 9)
    edges = np.linalg.norm(np.stack((tri[:, 1, :3] - tri[:, 0, :3], tri[:, 2, :3] - tri[:, 1, :3],
                                     tri[:, 0, :3] - tri[:, 2, :3])), axis=2)
    return float(edges.max()) >= 32.0


def _far_image(sources, cache, index, images):
    if index in cache:
        return cache[index]
    if images is not None and index in images:
        cache[index] = images[index]
        return cache[index]
    import street_atlas
    image, _, _ = street_atlas.texture_pixels(sources[index], 32)
    cache[index] = image
    return image


def _far_texel(image, uv, mips_cache, key):
    """Per-vertex colour that reads like the near mesh seen from far away.

    The corner texel of a big face is often its border (the TTC tunnel rock went
    flat beige). Each vertex samples bilinearly, with wrap, a quarter of the way
    toward the triangle's UV centroid, on the mip where the triangle spans about
    four texels. A coarser mip mixed in the neighbouring art of a palette page
    and the far tunnel rock went brown.
    """
    import numpy as np
    if key not in mips_cache:
        levels = [image.astype(np.float64)]
        while min(levels[-1].shape[:2]) > 1:
            a = levels[-1]
            h, w = a.shape[0] // 2 * 2, a.shape[1] // 2 * 2
            a = a[:h, :w]
            levels.append((a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2]) / 4)
        mips_cache[key] = levels
    levels = mips_cache[key]
    extent = float(np.ptp(uv, axis=0).max()) * max(image.shape[:2])
    level = int(np.clip(np.floor(np.log2(max(extent / 4.0, 1.0))), 0, len(levels) - 1))
    uv = uv.mean(axis=0) + (uv - uv.mean(axis=0)) * 0.75
    img = levels[level]
    h, w = img.shape[:2]
    out = np.empty((len(uv), 4))
    for k, (u, v) in enumerate(uv):
        x = (u % 1.0) * w - 0.5
        y = (1.0 - (v % 1.0)) * h - 0.5
        x0, y0 = int(np.floor(x)), int(np.floor(y))
        fx, fy = x - x0, y - y0
        c = 0.0
        for dy, wy in ((0, 1 - fy), (1, fy)):
            for dx, wx in ((0, 1 - fx), (1, fx)):
                c = c + img[(y0 + dy) % h, (x0 + dx) % w] * (wx * wy)
        out[k] = c
    return out


def _far_detail(image, b, tri):
    """A far triangle that must keep its texture: an alpha cutout or a decal.

    A corner texel of a cutout (balloons, flowers, leaves) is usually clear, so a
    vertex-coloured copy vanishes under the alpha test. A decal (window, door,
    shop sign) sampled at its corners is a flat frame-coloured patch. Only unit
    UVs qualify: a far atlas tile never repeats, so nothing is clipped.
    """
    import ttm
    if image is None:
        return False
    uv = tri[:, 3:5]
    if float(uv.min()) < -0.01 or float(uv.max()) > 1.01:
        return False
    if b.layer:
        return True
    return b.blend != ttm.BLEND_OPAQUE and int(image[:, :, 3].min()) <= 16


def _far_body(sources, image, b, tri):
    """A far wall, roof or landmark face that keeps a small cropped texture.

    Vertex colour turned the TTC tunnel rock, Toon Hall and the shop fronts into
    flat patches on Old 3DS past 160. Ground-like faces (|normal z| > 0.8) and
    faces whose UVs cross a texture repeat stay coloured: those are big, already
    flat at that range, and clipping their repeats pushed TTC past the far
    triangle budget.
    """
    import numpy as np
    import ttm
    import street_atlas
    if image is None or sources is None or not isinstance(sources, (list, tuple)) or b.layer:
        return False
    if b.blend == ttm.BLEND_ALPHA:
        return False
    if b.blend == ttm.BLEND_ALPHA_TEST and int(image[:, :, 3].min()) <= 16:
        return False
    p = tri[:, :3]
    n = np.cross(p[1] - p[0], p[2] - p[0])
    length = float(np.linalg.norm(n))
    if length < 1e-9 or abs(n[2]) / length > 0.8:
        return False
    xr, yr = street_atlas.tile_ranges(tri, sources[b.texture])
    return len(xr) == 1 and len(yr) == 1


# Far body pages: past lodNear (160 on Old 3DS) a texel per two screen pixels is
# 0.5 * 384 / 160 = 1.2 texels per world unit. Two mips stop minified tiles from
# shimmering; one opaque ETC1 page per chunk costs about one draw.
FAR_BODY_ATLAS = dict(density=0.5, gutter=2, mip_levels=2, view_near=160.0, view_far=160.0,
                      large_ground=False, pages=((64, 64), (128, 64), (128, 128), (256, 128), (256, 256),
                                                 (512, 256), (512, 512)), name='far_body')


def build_far_bodies(sources, chunks):
    """Per-chunk opaque pages for the textured far faces (_far_body)."""
    import street_atlas
    if not any(chunks):
        return [[] for _ in chunks], [[] for _ in chunks]
    textures, batches, _ = street_atlas.build_local(sources, chunks, **FAR_BODY_ATLAS)
    return textures, batches


def _colorize_far(sources, chunks, images=None, details=False, with_bodies=False):
    """Bake far triangles to vertex colour, but keep alpha-cut hill cards textured.

    Everything else samples the texel at the corner (32px) and drops the page.
    With details, cutouts and decals with unit UVs stay textured instead, as
    batches on their source texture ids for build_far_atlas; other decals drop.
    Without it decals are already gone. `images` injects arrays in tests; keys are
    texture ids. Kept cards still carry the source texture id. encode_far_pages
    remaps it. Returns (chunks, kept card triangles, per-chunk detail batches).
    """
    import numpy as np
    import ttm
    cache = {}
    mips_cache = {}
    result = []
    extra = []
    bodies = []
    kept = 0
    for batches in chunks:
        groups = {}
        cards = {}
        detail = {}
        body = {}
        for b in batches:
            if b.indices is None or len(b.indices) < 3:
                continue
            tris = np.asarray(b.verts, dtype=np.float64)[np.asarray(b.indices, dtype=np.int64)]
            tris = tris.reshape(-1, 3, 9)
            image = _far_image(sources, cache, b.texture, images) if b.texture is not None else None
            colored = []
            for tri in tris:
                if (image is not None and b.blend == ttm.BLEND_ALPHA_TEST
                        and _silhouette_card(image, tri.reshape(-1, 9))):
                    cards.setdefault((b.texture, bool(b.two_sided)), []).append(tri.copy())
                    continue
                if details and _far_detail(image, b, tri.reshape(-1, 9)):
                    detail.setdefault((b.texture, b.blend, bool(b.two_sided), int(b.layer)), []).append(tri.copy())
                    continue
                if details and _far_body(sources, image, b, tri):
                    body.setdefault((b.texture, bool(b.two_sided)), []).append(tri.copy())
                    continue
                if b.layer:
                    continue  # a repeating decal: a flat patch would cover its wall
                verts = tri.reshape(-1, 9).copy()
                if image is not None:
                    verts[:, 5:9] *= _far_texel(image, verts[:, 3:5], mips_cache, b.texture) / 255.0
                verts[:, 3:5] = 0.0
                np.clip(verts[:, 5:9], 0.0, 1.0, out=verts[:, 5:9])
                colored.append(verts)
            blend = b.blend
            if colored and details and blend == ttm.BLEND_ALPHA_TEST:
                # A coloured alpha-test triangle is a solid patch or nothing: under the
                # 0.5 test it shows where its corners are opaque. As opaque it shares
                # the opaque draw instead of costing one more.
                tri_alpha = np.stack(colored).reshape(-1, 3, 9)[:, :, 8].mean(axis=1)
                colored = [v for v, a in zip(colored, tri_alpha) if a >= 0.5]
                for v in colored:
                    v[:, 8] = 1.0
                blend = ttm.BLEND_OPAQUE
            if colored:
                groups.setdefault((blend, bool(b.two_sided)), []).extend(colored)
        if details:
            # Like the near atlas: a state that already has two-sided faces takes its
            # one-sided ones too, which cannot hide a face.
            for blend, two in list(groups):
                if not two and (blend, True) in groups:
                    groups[(blend, True)].extend(groups.pop((blend, False)))
        merged = []
        for (blend, two), parts in sorted(groups.items(), key=lambda kv: (kv[0][0], kv[0][1])):
            block = np.concatenate(parts)
            for start in range(0, len(block), ttm.MAX_BATCH_VERTS):
                vs = block[start:start + ttm.MAX_BATCH_VERTS]
                merged.append(ttm.Batch(None, blend, two, 0, 0, vs, np.arange(len(vs), dtype=np.int64)))
        for (tex, two), parts in sorted(cards.items(), key=lambda kv: (kv[0][0], kv[0][1])):
            block = np.concatenate([p.reshape(-1, 9) for p in parts])
            kept += len(block) // 3
            for start in range(0, len(block), ttm.MAX_BATCH_VERTS):
                vs = block[start:start + ttm.MAX_BATCH_VERTS]
                merged.append(ttm.Batch(tex, ttm.BLEND_ALPHA_TEST, two, 0, 0, vs,
                                        np.arange(len(vs), dtype=np.int64)))
        result.append(merged)
        extra.append([ttm.Batch(tex, blend, two, 0, layer, np.concatenate([p.reshape(-1, 9) for p in parts]),
                                np.arange(3 * len(parts), dtype=np.int64))
                      for (tex, blend, two, layer), parts in sorted(detail.items())])
        bodies.append([ttm.Batch(tex, ttm.BLEND_OPAQUE, two, 0, 0, np.concatenate([p.reshape(-1, 9) for p in parts]),
                                 np.arange(3 * len(parts), dtype=np.int64))
                       for (tex, two), parts in sorted(body.items())])
    if with_bodies:
        return result, kept, extra, bodies
    return result, kept, extra


# Far detail atlas: one small page per chunk. Past lodNear (160 Old, 260 New) a
# 10-unit shop sign covers about 14 px of the top screen, so a 16px tile holds all
# the detail it can show; two mips stop minified tiles from shimmering. Far pages
# stay resident with every planned chunk, so they must stay small: a 256px page
# per chunk kept Old 3DS from fitting Toon Hall's near mesh in its retain budget.
FAR_ATLAS = dict(tile=16, gutter=2, mip_levels=2)
FAR_PAGES = (64, 128, 256)


def build_far_atlas(sources, chunks, encode=True):
    """Per-chunk atlas pages for the detail batches of each chunk.

    Returns (per-chunk textures, per-chunk batches whose texture indexes that
    chunk's list). The page is the smallest of FAR_PAGES that holds the chunk's
    textures; only a chunk that overflows a 256 page uses more than one.
    """
    import street_atlas
    import ttm
    span = FAR_ATLAS['tile'] + 2 * FAR_ATLAS['gutter']
    pages, out = [], []
    for batches in chunks:
        used = sorted({b.texture for b in batches})
        if not used:
            pages.append([])
            out.append([])
            continue
        local = {src: i for i, src in enumerate(used)}
        page = next((p for p in FAR_PAGES if len(used) <= (p // span) ** 2), FAR_PAGES[-1])
        # One draw per page: far detail is alpha tested (a blended decal past lodNear
        # reads the same at >0.5), two-sided and on one decal layer. Split by state it
        # cost about three draws a chunk, and a TTC view past the 150 submit cap
        # dropped a far ground chunk (a sky hole).
        remapped = [ttm.Batch(local[b.texture], ttm.BLEND_ALPHA_TEST, True, 0, 1, b.verts, b.indices)
                    for b in batches]
        textures, result = street_atlas.build([sources[i] for i in used], [remapped], encode=encode,
                                              standalone_size=32, page=page, **FAR_ATLAS)
        pages.append(textures)
        out.append(result[0])
    return pages, out


FAR_CARD_SIZE = 128


def encode_far_pages(sources, chunks):
    """Encode the hill cards that _colorize_far kept, and remap their texture ids."""
    import extract
    import street_atlas
    import ttm
    used = []
    for batches in chunks:
        for b in batches:
            if b.texture is not None and b.texture not in used:
                used.append(b.texture)
    textures = []
    mapping = {}
    for src in used:
        mapping[src] = len(textures)
        # A hill card past lodNear spans about a 128px slice of the top screen. The
        # 256px page cost each far chunk ~32 KB of the retain budget per card; sign
        # fonts keep 256 so the letters stay legible.
        size = 256 if sign_text.is_font_texture(sources[src]) else FAR_CARD_SIZE
        image, nc, pf = street_atlas.texture_pixels(sources[src], size)
        textures.append(ttm.Texture(
            sources[src].getName(), image, extract.choose_format(image, nc, pf),
            extract._WRAP.get(sources[src].getWrapU(), ttm.GPU_CLAMP_TO_EDGE),
            extract._WRAP.get(sources[src].getWrapV(), ttm.GPU_CLAMP_TO_EDGE)))
    remapped = []
    for batches in chunks:
        out = []
        for b in batches:
            tex = None if b.texture is None else mapping[b.texture]
            out.append(ttm.Batch(tex, b.blend, b.two_sided, b.tint, b.layer, b.verts, b.indices))
        remapped.append(out)
    return remapped, textures


def _is_sign(batch, sources):
    """A DNA sign-letter decal. The far mesh keeps it textured; other decals still drop."""
    if getattr(batch, 'sign', False):
        return True
    if not getattr(batch, 'layer', 0) or batch.texture is None or sources is None:
        return False
    if not isinstance(sources, (list, tuple)) or batch.texture >= len(sources):
        return False
    tex = sources[batch.texture]
    return tex is not None and sign_text.is_font_texture(tex)


def _split_signs(sources, batches):
    signs, rest = [], []
    for batch in batches:
        if _is_sign(batch, sources):
            signs.append(batch)
        else:
            rest.append(batch)
    return signs, rest


def _merge_states(batches):
    """Batches with one texture, blend, cull and layer as one draw (DNA signs come per sign)."""
    import numpy as np
    import ttm
    groups = {}
    for b in batches:
        key = (b.texture, b.blend, bool(b.two_sided), b.tint, b.layer, getattr(b, 'sign', False))
        groups.setdefault(key, []).append(np.asarray(b.verts)[np.asarray(b.indices)])
    out = []
    for (tex, blend, two, tint, layer, sign), parts in groups.items():
        block = np.concatenate(parts)
        for start in range(0, len(block), ttm.MAX_BATCH_VERTS):
            vs = block[start:start + ttm.MAX_BATCH_VERTS]
            out.append(ttm.Batch(tex, blend, two, tint, layer, vs, np.arange(len(vs)), sign))
    return out


def choose_far(sources, raw, chunks, images=None):
    """Voxel-simplify, then bake vertex colours, until every PVS fits the far budget.

    Spacing climbs until 48 units so a whole playground still fits. Hill cards
    whose alpha cuts the silhouette keep a 128px texture; a corner sample of
    those is a flat green quad. Draws are counted after that split, and must
    fit street_lod.DRAW_CAP. `images` is a test injection for _colorize_far.
    Then cutouts and decals are kept textured on per-chunk far atlas pages when
    that still fits within one voxel rung of the plain mesh.
    """
    import numpy as np
    import street_lod
    import ttm
    steps = (0, .25, .5, .75, 1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48)
    def worst_of(low):
        tris = max((sum(street_lod.count(low[j]) for j in c['vis']) for c in chunks), default=0)
        draws = max((sum(_far_draws(low[j]) for j in c['vis']) for c in chunks), default=0)
        return tris, draws
    before, _ = worst_of(raw)
    sign_sets, bodies = [], []
    for batches in raw:
        signs, rest = _split_signs(sources, batches)
        sign_sets.append(signs)
        bodies.append(rest)
    sign_tris = sum(street_lod.count(s) for s in sign_sets)
    def attempt(step, details):
        """The far mesh at one voxel step, or None when a PVS misses the budget."""
        merged_signs = [_merge_states(signs) for signs in sign_sets] if details else sign_sets
        if step:
            # Letters are not snapped either: lift them with the kept decals.
            merged_signs = [[ttm.Batch(b.texture, b.blend, b.two_sided, b.tint, b.layer,
                                       street_lod.lift(np.asarray(b.verts)[np.asarray(b.indices)], step),
                                       np.arange(len(b.indices)), getattr(b, 'sign', False))
                             for b in signs] for signs in merged_signs]
        low = [street_lod.simplify(bs, step, keep_layers=details) for bs in bodies]
        colored, kept, extra, faces = _colorize_far(sources, low, images, details=details,
                                                    with_bodies=True)
        body_pages, body_batches = (build_far_bodies(sources, faces) if details and images is None
                                    else ([[] for _ in low], [[] for _ in low]))
        if details:
            _, atlased = build_far_atlas(sources, extra, encode=False)
            # Count each chunk's pages as their own textures; nothing is shared.
            placed = [list(c) + [ttm.Batch(('far', i, b.texture), b.blend, b.two_sided, 0, b.layer,
                                           b.verts, b.indices) for b in a]
                      + [ttm.Batch(('body', i, b.texture), b.blend, b.two_sided, 0, b.layer,
                                   b.verts, b.indices) for b in bb]
                      for i, (c, a, bb) in enumerate(zip(colored, atlased, body_batches))]
        else:
            placed = colored
        worst, draws = worst_of(placed)
        if os.environ.get('TT_FAR_DEBUG'):
            print(f'far attempt step={step} details={details} worst={worst} draws={draws} '
                  f'body={street_lod.count(sum(body_batches, []))}', flush=True)
        if worst > street_lod.LOW_TARGET or draws > street_lod.DRAW_CAP:
            return None, (worst, draws)
        signed = [list(chunk) + signs for chunk, signs in zip(placed, merged_signs)]
        signed_worst, signed_draws = worst_of(signed)
        # Letters stay when they still fit. A view that is already on the cap keeps the
        # old far mesh rather than failing the bake.
        use_signs = signed_worst <= street_lod.LOW_TARGET and signed_draws <= street_lod.DRAW_CAP
        if use_signs:
            colored = [list(chunk) + signs for chunk, signs in zip(colored, merged_signs)]
            worst, draws = signed_worst, signed_draws
        detail_tris = sum(street_lod.count(e) for e in extra)
        if images is None:
            colored, textures = encode_far_pages(sources, colored)
            if details:
                pages, atlased = build_far_atlas(sources, extra)
                for i, (chunk_pages, batches) in enumerate(zip(pages, atlased)):
                    base = len(textures)
                    textures.extend(chunk_pages)
                    colored[i] = list(colored[i]) + [
                        ttm.Batch(base + b.texture, b.blend, b.two_sided, 0, b.layer, b.verts, b.indices)
                        for b in batches]
                for i, (chunk_pages, batches) in enumerate(zip(body_pages, body_batches)):
                    base = len(textures)
                    textures.extend(chunk_pages)
                    colored[i] = list(colored[i]) + [
                        ttm.Batch(base + b.texture, b.blend, b.two_sided, 0, b.layer, b.verts, b.indices)
                        for b in batches]
        else:
            textures = []
        return (colored, textures, {
            'voxel_spacing': step, 'max_vertex_displacement': step * 3 ** .5 / 2,
            'before_worst_triangles': before, 'low_worst_triangles': worst,
            'low_worst_draws': draws, 'textured': bool(textures) or kept > 0,
            'hill_card_triangles': kept, 'sign_triangles': sign_tris if use_signs else 0,
            'detail_triangles': detail_tris if details else 0,
            'body_triangles': sum(street_lod.count(b) for b in body_batches) if details else 0,
            'draw_cap': street_lod.DRAW_CAP}), (worst, draws)

    worst = draws = None
    plain = None
    for rung, step in enumerate(steps):
        plain, (worst, draws) = attempt(step, False)
        if plain is not None:
            break
    if plain is None:
        raise FarBudgetError(worst, draws)
    if images is not None:
        return plain
    # Textured cutouts and decals cost triangles and draws. They are worth at most one
    # coarser voxel rung than the plain far mesh; past that the shape suffers more.
    # Sign letters come first: a detailed mesh that loses letters the plain one kept is
    # not taken.
    for step in steps[rung:rung + 2]:
        detailed, _ = attempt(step, True)
        if detailed is not None and (detailed[2]['sign_triangles'] or not plain[2]['sign_triangles']):
            return detailed
    return plain


def split_grid(batches, size):
    import numpy as np
    import ttm
    groups = {}
    for b in batches:
        triangles = b.verts[b.indices.reshape(-1,3)]
        cells = np.floor(triangles[:,:,:2].mean(axis=1)/size).astype(int)
        for key in sorted(set(map(tuple,cells))):
            verts = triangles[np.all(cells==key,axis=1)].reshape(-1,9)
            groups.setdefault(key,[]).append(ttm.Batch(b.texture,b.blend,b.two_sided,b.tint,b.layer,
                                                     verts,np.arange(len(verts)), getattr(b, 'sign', False)))
    return groups


# A fit under this kept almost no far shape: voxel spacing emptied the cells to
# buy draws. Coarser grids are tried until the far mesh has some body.
FAR_BODY = 4000


def lower_draw_bound(raw, chunks):
    """One draw per non-empty chunk in the busiest vis. The real count is at least this."""
    return max((sum(1 for j in c['vis'] if raw[j]) for c in chunks), default=0)


def prefer_far(report, best):
    """True when report keeps more of the far mesh than the current best."""
    if best is None:
        return True
    return report['low_worst_triangles'] > best['low_worst_triangles']


def split_zone_grid(batches, floor_tris, zone_id, size, distance, margin=78.0):
    """Bin render and floor triangles into grid cells and build a distance vis.

    floor_tris are (3, 3) world triangles. An empty cell's bounds are its grid
    square, so a floor sliver still joins the vis. Returns raw, chunks, floors.
    """
    import numpy as np
    import street_atlas
    groups = split_grid(batches, size)

    def cells_of(p):
        return [(cx, cy)
                for cy in range(math.floor(float(p[:, 1].min()) / size), math.floor(float(p[:, 1].max()) / size) + 1)
                for cx in range(math.floor(float(p[:, 0].min()) / size), math.floor(float(p[:, 0].max()) / size) + 1)]

    def clip_cell(p, cx, cy):
        poly = list(np.pad(p, ((0, 0), (0, 6))))
        for axis, key in ((0, cx), (1, cy)):
            poly = street_atlas.clip(
                street_atlas.clip(poly, axis, key * size, True), axis, (key + 1) * size, False) if poly else []
        return poly

    for p in floor_tris:
        for cx, cy in cells_of(p):
            if len(clip_cell(p, cx, cy)) >= 3:
                groups.setdefault((cx, cy), [])
    keys = sorted(groups)
    raw, chunks, floors, bounds = [], [], [], []
    for i, key in enumerate(keys):
        bs = groups[key]
        raw.append(bs)
        chunks.append({'name': f'g{i}', 'zone': zone_id, 'vis': [], 'grid': key})
        pts = [b.verts[:, :3] for b in bs if len(b.verts)]
        if pts:
            stacked = np.concatenate(pts)
            bounds.append((stacked.min(0), stacked.max(0)))
        else:
            lo = np.array([key[0], key[1], 0.0], dtype=np.float64) * size
            bounds.append((lo, lo + np.array([size, size, 0.0])))
    for p in floor_tris:
        for i, (cx, cy) in enumerate(keys):
            poly = clip_cell(p, cx, cy)
            for j in range(1, len(poly) - 1):
                tri = np.array([poly[k][:3] for k in (0, j, j + 1)])
                if np.linalg.norm(np.cross(tri[1] - tri[0], tri[2] - tri[0])) > 1e-6:
                    floors.append((tri, i, zone_id))
    floors.sort(key=lambda f: f[1])
    reach = distance + margin
    for i, c in enumerate(chunks):
        lo = np.array(c['grid'], dtype=np.float64) * size
        hi = lo + size
        vis = []
        for j, (bmin, bmax) in enumerate(bounds):
            delta = np.maximum(np.maximum(bmin[:2] - hi, lo - bmax[:2]), 0)
            if i == j or float(np.linalg.norm(delta)) <= reach:
                vis.append(j)
        c['vis'] = vis
    return raw, chunks, floors


def view_points(floors, eye=4.0, step=6.0):
    """Walkable floor samples at eye height for street_atlas.ViewDensity.

    Each floor triangle gives its corners, centroid and a step-spaced fill so a
    big plaza triangle still has points near the props on it.
    """
    import numpy as np
    pts = []
    for f in floors:
        a, b, c = np.asarray(f[0], dtype=np.float64)
        n = max(1, int(max(np.linalg.norm(b - a), np.linalg.norm(c - a)) // step))
        for i in range(n + 1):
            for j in range(n + 1 - i):
                pts.append(a + (b - a) * i / n + (c - a) * j / n)
    pts = np.array(pts).reshape(-1, 3)
    if len(pts):
        pts[:, 2] += eye
        pts = np.unique(np.round(pts / 2) * 2, axis=0)
    return pts


def bake(zone_id, args, base):
    import numpy as np
    import assemble
    import collide
    import dna
    import street_atlas
    import ttm
    import tz
    import street_catalog
    import extract
    import street_lod
    resources = Path(args.resources)
    if not (resources / 'phase_4/dna/storage.dna').exists():
        resources = ROOT / 'ref/ref/resources'
    store = dna.Storage(base.loader, str(resources))
    playground,storages,src=street_catalog.paths(zone_id)
    for s in storages:
        store.load(str(resources/s))
    warnings = []
    tree = dna.parse_file(resources/src,warnings)
    root,asm = assemble.assemble(tree,store,seed=1)
    if args.separate_boat:
        # Donald's Dock boat is the moving DistributedBoat (bake_boat.py). Stashed nodes are
        # skipped by findAllMatches, so neither its geometry nor its collision reach the .tz.
        for n in root.findAllMatches('**/donalds_boat'):
            n.stash()
    import bake_moving_props
    movers = bake_moving_props.split(root, zone_id, args.out)
    if args.points_only:  # refresh the .pts anchors without re-encoding the zone
        from bake_zone import write_points
        num_points = write_points(root, str(Path(args.out) / f'{zone_id}.pts'))
        num_points += bake_moving_props.append_points(Path(args.out) / f'{zone_id}.pts', movers)
        print(f'{zone_id}: {num_points} points', flush=True)
        return {'zone': zone_id, 'points': num_points}
    ex = street_atlas.Extractor()
    chunks, raw, floors = [],[],[]
    ground_stats = {'triangles_in': 0, 'triangles_out': 0, 'split': 0}
    low, far_textures, lod_report, grid_used = None, [], None, None
    whole_collision = collide.extract(root)
    collision, collision_stats = whole_collision.pack()
    if not playground:
        nodes = sorted(root.findAllMatches('**/='+assemble.VISGROUP_TAG),
                       key=lambda n:int(n.getTag(assemble.VISGROUP_TAG)))
        indices = {n.getTag(assemble.VISGROUP_TAG):i for i,n in enumerate(nodes)}
        definitions = {n.name:n for n in tree.walk() if n.kind=='visgroup'}
        # Extract each subtree once; visgroup transforms are applied below.
        for i,n in enumerate(nodes):
            name=n.getTag(assemble.VISGROUP_TAG)
            ex._groups={}
            ex.add(n)
            bs=ex.batches()
            mat=n.getMat(root)
            if not mat.isIdentity():
                for b in bs:
                    b.verts[:,:3]=[tuple(mat.xformPoint(tuple(p))) for p in b.verts[:,:3]]
            bs, part = extract.subdivide_ground(bs)
            for key in ground_stats:
                ground_stats[key] += part[key]
            raw.append(bs)
            visible = definitions[name].get('vis',[])
            if not isinstance(visible,list):
                visible=[visible]
            missing = [str(v) for v in visible if str(v) not in indices]
            if missing:
                raise ValueError(f'{name}: missing PVS cells {missing}')
            chunks.append({'name':name,'zone':int(name),'vis':sorted({i}|{indices[str(v)] for v in visible})})
            mesh=collide.extract(n,fallback_floor=False)
            for a,b,c,normal,flags in mesh.tris:
                if flags & collide.FLOOR and not flags & collide.TRIGGER and normal[2]>=0.707:
                    floors.append((np.array([a,b,c]),i,int(name)))
    else:
        ex.add(root)
        split_src, part = extract.subdivide_ground(ex.batches())
        for key in ground_stats:
            ground_stats[key] += part[key]
        floor_tris = [np.array([a, b, c]) for a, b, c, normal, flags in whole_collision.tris
                      if flags & collide.FLOOR and not flags & collide.TRIGGER and normal[2] >= 0.707]
        # --grid-size is the floor. A playground whose far view is one draw per cell
        # (17000 at 128 was 197) is rebinned coarser until the draw cap fits. A fit
        # that only survives by deleting the mesh keeps the search going.
        for size in grid_ladder(args.grid_size):
            trial_raw, trial_chunks, trial_floors = split_zone_grid(
                split_src, floor_tris, zone_id, size, args.distance)
            if len(trial_chunks) > 256 or lower_draw_bound(trial_raw, trial_chunks) > street_lod.DRAW_CAP:
                continue
            try:
                trial_low, trial_tex, trial_lod = choose_far(ex.sources, trial_raw, trial_chunks)
            except FarBudgetError:
                continue
            if not prefer_far(trial_lod, lod_report):
                continue
            grid_used = size
            raw, chunks, floors = trial_raw, trial_chunks, trial_floors
            low, far_textures, lod_report = trial_low, trial_tex, trial_lod
            if trial_lod['low_worst_triangles'] >= FAR_BODY:
                break
        if low is None:
            raise ValueError(
                f'{zone_id}: no grid size keeps the far LOD within {street_lod.DRAW_CAP} draws')
    chunk_textures, batches, atlas_stats=street_atlas.build_local(ex.sources, raw, view_points=view_points(floors),
        **dict(NEAR_ATLAS, density=float(os.environ.get('TT_ATLAS_DENSITY', NEAR_ATLAS['density']))))
    batches=[[street_atlas.rebase_large_uvs(b,t) for b in bs] for bs,t in zip(batches,chunk_textures)]
    if os.environ.get('TT_ATLAS_DEBUG'):
        Path(os.environ['TT_ATLAS_DEBUG']).write_text(json.dumps(atlas_stats,indent=1))
    out=Path(args.out)
    out.mkdir(parents=True,exist_ok=True)
    def pack(bs,tmp,tex_list):
        if not bs:
            return b'',{'batches':0,'triangles':0,'geometry_bytes':0,'texture_bytes':0,'bounds':[[0,0,0],[0,0,0]]}
        used=sorted({b.texture for b in bs if b.texture is not None})
        mapping={old:new for new,old in enumerate(used)}
        copies=[ttm.Batch(mapping[b.texture] if b.texture is not None else None,
                          b.blend,b.two_sided,b.tint,b.layer,b.verts,b.indices) for b in bs]
        path=Path(tmp)/'chunk.ttm'
        stats=ttm.write(path,[tex_list[j] for j in used],copies)
        if stats['uv_clamped']:
            raise ValueError('UV overflow')
        return path.read_bytes(),stats
    with tempfile.TemporaryDirectory() as tmp:
        for chunk,bs,tex_list in zip(chunks,batches,chunk_textures):
            chunk['model'],chunk['stats']=pack(bs,tmp,tex_list)
    if low is None:
        # Streets: the DNA vis is already on each chunk. Same far path as playgrounds.
        low, far_textures, lod_report = choose_far(ex.sources, raw, chunks)
    with tempfile.TemporaryDirectory() as tmp:
        for chunk,bs in zip(chunks,low):
            # Hill cards keep their page. Everything else is vertex coloured.
            chunk['low_model'],chunk['low_stats']=pack(bs,tmp,far_textures)
            if chunk['low_model']:
                chunk['stats']['bounds']=[np.minimum(chunk['stats']['bounds'][0],chunk['low_stats']['bounds'][0]).tolist(),
                                          np.maximum(chunk['stats']['bounds'][1],chunk['low_stats']['bounds'][1]).tolist()]
    before=tz.report(chunks)
    low_chunks=[dict(c,stats=c['low_stats']) for c in chunks]
    low_report=tz.report(low_chunks)
    samples=[]
    for i,c in enumerate(chunks):
        points=[np.mean(f[0],axis=0) for f in floors if f[1]==i]
        if not points:
            points=[np.mean(c['stats']['bounds'],axis=0)]
        # Floor centroids span each cell; every plan is hard capped independently.
        for p in points[::max(1,len(points)//8)]:
            for near in (OLD_LOD_NEAR, NEW_LOD_NEAR):
                _,metrics=street_lod.plan(chunks,i,p,near=near,must=LOD_MUST[near])
                samples.append(metrics)
    planned={k:max(row[k] for row in samples) for k in ('batches','triangles','geometry_bytes','texture_bytes')}
    triggers=[]
    for t in whole_collision.triggers:
        tunnel='tunnel' in (t.name+' '+t.owner).lower()
        triggers.append({'min':list(t.lo),'max':list(t.hi),'kind':2 if tunnel else 1 if 'door' in t.name.lower() else 0,
                         'name':t.name,'owner':t.owner})
    # Tunnel exit anchors have no trigger in some BAMs. Small named approach volumes
    # let the integration queue a transition; actual teleporting remains game policy.
    for n in root.findAllMatches('**/linktunnel_*'):
        p=np.array(n.getPos(root))
        triggers.append({'min':list(p-[6,6,3]),'max':list(p+[6,6,8]),'kind':2,
                         'name':n.getName(),'owner':n.getParent().getName()})
    report=tz.write(out/f'{zone_id}.tz',zone_id,chunks,floors,triggers,collision)
    sign_views=[]
    for n in root.findAllMatches('**/=sign_baseline'):
        letters=n.getTag('letters')
        bounds=n.getTightBounds(root)
        if not letters.strip() or bounds is None:
            continue
        lo,hi=map(np.array,bounds)
        centre=(lo+hi)/2
        normal=np.array(n.getMat(root).xformVec((0,-1,0)))
        normal/=max(float(np.linalg.norm(normal)),1e-8)
        eye=centre+normal*max(8,float(np.linalg.norm(hi-lo))*1.2)
        sign_views.append({'letters':letters,'look':centre.tolist(),'eye':eye.tolist()})
    from bake_zone import write_points
    num_points=write_points(root,str(out/f'{zone_id}.pts'))
    num_points += bake_moving_props.append_points(out/f'{zone_id}.pts', movers)
    report.update({'moving_props':movers,'version':3,'before':before['worst'],'low':low_report['worst'],'planned_worst':planned,
                   'lod':lod_report,'sign_views':sign_views,'points':num_points,'sign_baselines':asm.sign_baselines,'sign_letters':asm.sign_letters,'zone':zone_id,'floor_triangles':len(floors),'triggers':len(triggers),
                   'file_bytes':(out/f'{zone_id}.tz').stat().st_size,'storage_errors':store.errors,
                   'unresolved_codes':dict(asm.unresolved),'skipped':dict(asm.skipped),
                   'warnings':warnings,'collision':collision_stats,'atlas_pages':sum(a['pages'] for a in atlas_stats),
                   'near_atlas':NEAR_ATLAS,'ground_split':ground_stats,
                   'grid_size':grid_used,'draw_cap':street_lod.DRAW_CAP})
    (out/f'{zone_id}.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('visible_sets','collision','sign_views')},indent=2),flush=True)
    root.removeNode()
    return out/f'{zone_id}.tz'


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--zone',default='all',help='all or comma-separated supported zone ids')
    ap.add_argument('--resources',default=str(ROOT/'ref/resources'))
    ap.add_argument('--out',default=str(ROOT/'sdmc-out/3ds/toontown3ds/data/zones'))
    ap.add_argument('--grid-size',type=float,default=128)
    ap.add_argument('--distance',type=float,default=760,
                    help='playground cell vis radius (plus 78 covers the New 3DS draw; Old loads less)')
    ap.add_argument('--preview',action='store_true')
    ap.add_argument('--points-only',action='store_true',help='only rewrite the .pts anchor sidecars')
    ap.add_argument('--separate-boat',action='store_true',
                    help="leave donalds_boat out of zone 1000 (bake_boat.py bakes it as 1000_boat.ttm)")
    args=ap.parse_args()
    if args.grid_size<=0 or args.distance<=0:
        ap.error('grid size and distance must be positive')
    if args.zone=='all' or ',' in args.zone:
        import subprocess
        import street_catalog
        requested=street_catalog.ZONES if args.zone=='all' else tuple(int(z) for z in args.zone.split(','))
        for z in requested:
            command=[sys.executable,'-I',str(Path(__file__).resolve()),'--zone',str(z),
                     '--resources',args.resources,'--out',args.out,'--grid-size',str(args.grid_size),
                     '--distance',str(args.distance)]
            if args.preview:
                command.append('--preview')
            if args.separate_boat:
                command.append('--separate-boat')
            if args.points_only:
                command.append('--points-only')
            subprocess.run(command,check=True)
        return
    from panda3d.core import loadPrcFileData, BamCache
    BamCache.getGlobalPtr().setActive(False)
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\ntextures-power-2 none\nmodel-cache-dir')
    from direct.showbase.ShowBase import ShowBase
    base=ShowBase()
    import street_catalog
    zones=street_catalog.ZONES if args.zone=='all' else tuple(int(z) for z in args.zone.split(','))
    paths=[]
    for z in zones:
        print(f'Baking zone {z}',flush=True)
        paths.append(bake(z,args,base))
    base.destroy()
    if args.preview:
        import subprocess
        for path in paths:
            subprocess.run([sys.executable,'-I',str(HERE/'street_preview.py'),str(path),
                            str(ROOT/'sdmc-out/previews/streets')],check=True)


if __name__=='__main__':
    main()
