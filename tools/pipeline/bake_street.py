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

os.environ.setdefault('TT_ETC1_THREADS', '2')
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
NEAR_ATLAS = dict(tile=128, gutter=16, standalone_size=256, mip_levels=4,
                  large_ground=True, page_size=512)
OLD_LOD_NEAR = 260.0
NEW_LOD_NEAR = 260.0
# render/quality.h lodMust (Old, New): upgraded first, still inside DRAW_CAP.
LOD_MUST = {OLD_LOD_NEAR: 64.0, NEW_LOD_NEAR: 64.0}


# Sizes the baker may raise a playground or Cog HQ to. --grid-size is the floor:
# a zone whose far view exceeds the draw cap is rebinned until it fits.
GRID_LADDER = (96, 128, 160, 192, 256, 320, 384, 512)


class FarBudgetError(ValueError):
    def __init__(self, tris, draws, tex=0):
        super().__init__(f'far LOD cannot meet budgets: {tris} triangles, {draws} draws, {tex} texture bytes')
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


def _colorize_far(sources, chunks, images=None):
    """Bake far triangles to vertex colour, but keep alpha-cut hill cards textured.

    Everything else samples the texel at the corner (32px) and drops the page.
    Decals are already gone. `images` injects arrays in tests; keys are texture ids.
    Kept cards still carry the source texture id. encode_far_pages remaps it.
    """
    import numpy as np
    import ttm
    cache = {}
    result = []
    kept = 0
    for batches in chunks:
        groups = {}
        cards = {}
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
                verts = tri.reshape(-1, 9).copy()
                if image is not None:
                    height, width = image.shape[:2]
                    u = np.mod(verts[:, 3], 1.0)
                    v = np.mod(verts[:, 4], 1.0)
                    x = np.clip((u * (width - 1)).astype(np.intp), 0, width - 1)
                    y = np.clip(((1.0 - v) * (height - 1)).astype(np.intp), 0, height - 1)
                    verts[:, 5:9] *= image[y, x].astype(np.float64) / 255.0
                verts[:, 3:5] = 0.0
                np.clip(verts[:, 5:9], 0.0, 1.0, out=verts[:, 5:9])
                colored.append(verts)
            if colored:
                groups.setdefault((b.blend, bool(b.two_sided)), []).extend(colored)
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
    return result, kept


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
        image, nc, pf = street_atlas.texture_pixels(sources[src], 256)
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


def choose_far(sources, raw, chunks, images=None):
    """Preserve original textured geometry and all decals; fail rather than blob."""
    import far_atlas
    import street_lod
    import far_shell
    original = raw
    print('far shell raw:', sum(street_lod.count(bs) for bs in raw), 'tris;',
          sum(street_lod.count([b]) for bs in raw for b in bs if _is_sign(b, sources)),
          'sign tris', flush=True)
    for ratio in (1.0, .8, .65, .5, .35, .2, .1):
        candidate = original if ratio == 1 else [[far_shell.simplify(b, ratio) for b in bs] for bs in original]
        preview_tex, preview = far_atlas.build(sources, candidate, encode=False, images=images)
        worst = max((sum(street_lod.count(preview[j]) for j in c['vis']) for c in chunks), default=0)
        draws = max((sum(len(preview[j]) for j in c['vis']) for c in chunks), default=0)
        tex_bytes = max((sum(sum(preview_tex[t].byte_size for t in
            {b.texture for b in preview[j]}) for j in c['vis']) for c in chunks), default=0)
        if worst <= street_lod.TRI_BUDGET and draws <= street_lod.DRAW_CAP and tex_bytes <= 8*1024*1024:
            raw = candidate
            break
    else:
        raise FarBudgetError(worst, draws, tex_bytes)
    textures, low = far_atlas.build(sources, raw, images=images)
    return low, textures, dict(voxel_spacing=0, max_vertex_displacement=None if ratio < 1 else 0,
        quadric_ratio=ratio, max_surface_error=.75 if ratio < 1 else 0,
        before_worst_triangles=max((sum(street_lod.count(original[j]) for j in c['vis'])
                                  for c in chunks), default=0),
        low_worst_triangles=worst, low_worst_draws=draws,
        textured=True, hill_card_triangles=0,
        sign_triangles=sum(street_lod.count([b]) for bs in raw for b in bs if _is_sign(b, sources)),
        draw_cap=street_lod.DRAW_CAP)


def choose_voxel_far(sources, raw, chunks, images=None):
    """Voxel-simplify, then bake vertex colours, until every PVS fits the far budget.

    Spacing climbs until 48 units so a whole playground still fits. Hill cards
    whose alpha cuts the silhouette keep a 256px texture; a corner sample of
    those is a flat green quad. Draws are counted after that split, and must
    fit street_lod.DRAW_CAP. `images` is a test injection for _colorize_far.
    """
    import street_lod
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
    worst = draws = None
    for step in steps:
        low = [street_lod.simplify(bs, step) for bs in bodies]
        colored, kept = _colorize_far(sources, low, images)
        worst, draws = worst_of(colored)
        signed = [list(chunk) + signs for chunk, signs in zip(colored, sign_sets)]
        signed_worst, signed_draws = worst_of(signed)
        # Letters stay when they still fit. A view that is already on the cap keeps the
        # old far mesh rather than failing the bake.
        use_signs = signed_worst <= street_lod.LOW_TARGET and signed_draws <= street_lod.DRAW_CAP
        if worst <= street_lod.LOW_TARGET and draws <= street_lod.DRAW_CAP:
            if use_signs:
                colored, worst, draws = signed, signed_worst, signed_draws
            if images is None:
                colored, textures = encode_far_pages(sources, colored)
            else:
                textures = []
            return colored, textures, {
                'voxel_spacing': step, 'max_vertex_displacement': step * 3 ** .5 / 2,
                'before_worst_triangles': before, 'low_worst_triangles': worst,
                'low_worst_draws': draws, 'textured': bool(textures) or kept > 0,
                'hill_card_triangles': kept, 'sign_triangles': sign_tris if use_signs else 0,
                'draw_cap': street_lod.DRAW_CAP}
    raise FarBudgetError(worst, draws)


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
    far_raw = []
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
            far_raw.append(bs)
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
        original = ex.batches()
        split_src, part = extract.subdivide_ground(original)
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
                far_groups = split_grid(original, size)
                trial_far = [far_groups.get(tuple(c['grid']), []) for c in trial_chunks]
                trial_low, trial_tex, trial_lod = choose_far(ex.sources, trial_far, trial_chunks)
            except FarBudgetError as error:
                print(f"grid {size:g}: {error}", flush=True)
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
    if low is None:
        # Fail a street before expensive near encoding if its original shell
        # cannot fit. Never silently substitute the old voxel mesh.
        low, far_textures, lod_report = choose_far(ex.sources, far_raw, chunks)
    textures, batches=street_atlas.build(ex.sources, raw, **NEAR_ATLAS)
    batches=[[street_atlas.rebase_large_uvs(b,textures) for b in bs] for bs in batches]
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
        for chunk,bs in zip(chunks,batches):
            chunk['model'],chunk['stats']=pack(bs,tmp,textures)
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
                   'warnings':warnings,'collision':collision_stats,'atlas_pages':len(textures),
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
