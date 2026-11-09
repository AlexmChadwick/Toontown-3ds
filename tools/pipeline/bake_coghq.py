#!/usr/bin/env python3
"""Bake Cog HQ exteriors and lobbies (single .bam scenes) to streamed .tz files.

    .venv/bin/python -I tools/pipeline/bake_coghq.py [--zone all|11000,11100,...]
        [--out sdmc-out/3ds/toontown3ds/data/zones] [--preview] [--audio]

Each zone is one scene model (see coghq_catalog.py), not DNA. The baker applies the same node
renames the upstream CogHQLoader subclasses apply at load time, extracts collision, tunnel and
door triggers with collide.py, then chunks the visible geometry into a uniform XY grid exactly
like the playgrounds of bake_street.py (near atlas, ground split, vertex-coloured far
LOD with hill cards kept textured, PVS out to the New 3DS draw distance) and writes
TZ v3 with tz.py. --grid-size is a floor: the baker raises it until the far view
fits the draw cap.

Outputs per zone: <zone>.tz, <zone>.pts (tunnel/door anchors) and <zone>.json (budget report).
--audio also bakes the HQ music to <out>/../audio/<hood>.tsnd for the four HQ hoods.
At most 3 ETC1 encoder threads; no source asset is modified.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
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
MAIN_REF = Path('/Users/alexchadwick/toontown-3ds/ref/resources')


def default_resources():
    mine = ROOT / 'ref/resources'
    if (mine / 'phase_9').exists():
        return str(mine)
    ref_ref = ROOT / 'ref/ref/resources'
    if (ref_ref / 'phase_9').exists():
        return str(ref_ref)
    return str(MAIN_REF)


def build_scene(zone_id, cfg, loader, resources):
    """Load the scene and apply the CogHQLoader renames. Returns (root, report)."""
    from panda3d.core import NodePath, PandaNode
    import assemble
    root = NodePath(PandaNode(f'coghq_{zone_id}'))
    model = loader.loadModel(cfg['model'], noCache=True)
    if model is None or model.isEmpty():
        raise FileNotFoundError(cfg['model'])
    model.reparentTo(root)
    info = {'dropped': [], 'tunnels': [], 'doors': []}
    for pattern in cfg['drop']:
        for n in root.findAllMatches('**/' + pattern):
            info['dropped'].append(n.getName())
            n.stash()
    for old, new in cfg['tunnels'].items():
        n = root.find('**/' + old)
        if n.isEmpty():
            raise ValueError(f'{zone_id}: tunnel node {old} not found')
        n.setName(new)
        for s in n.findAllMatches('**/tunnel_sphere'):
            s.setName('tunnel_trigger')
        info['tunnels'].append(new)
    for cn in root.findAllMatches('**/door_trigger_*'):
        index = int(cn.getName().rsplit('_', 1)[1])
        dest = cfg['doors'].get(index)
        p = cn.getParent()
        while not p.isEmpty() and p != root and p.getName() != f'door_{index}':
            p = p.getParent()
        if dest is None or p.isEmpty() or p == root:
            raise ValueError(f'{zone_id}: door_trigger_{index} has no door node/destination')
        p.setName(f'cogdoor_{dest}_{index}_DNARoot')
        info['doors'].append(p.getName())
    # Closed doors: upstream hides the black hole decals until the door opens.
    for n in root.findAllMatches('**/doorFrameHole*'):
        n.hide()
    if zone_id == 10000:
        top = root.find('**/TunnelEntrance')
        if not top.isEmpty():
            origin = top.find('**/tunnel_origin')
            if not origin.isEmpty():
                origin.setH(-33.33)
    info['signs'] = sellbot_signs(zone_id, root, loader, resources)
    assemble.lift_decals(root)
    info['billboards_crossed'] = assemble.cross_billboards(root)
    return root, info


# SellbotCogHQLoader.loadPlaceGeom copies sign_sellBotHeadHQ onto the tunnels and factory
# doorways and writes OnscreenText (suit font, default black, centred) on it. Per zone:
# (parent node, pos, hpr, sign scale, [(text, z, text scale)]).
_SELLBOT_SIGN_SF, _SELLBOT_ELEVATOR_SF, _SELLBOT_ASPECT = 23, 15, 0.7227
SELLBOT_SIGNS = {
    11000: [('linktunnel_dg_5316_DNARoot', (0.0, -291.5, 29), 180.0, _SELLBOT_SIGN_SF,
             [('Daisy Gardens', -0.3, 0.1)]),
            ('linktunnel_sellhq_11200_DNARoot', (148.625, -155, 27), -90.0, _SELLBOT_SIGN_SF,
             [('Sellbot', -0.25, 0.075), ('Factory', -0.34, 0.12)])],
    11200: [('linktunnel_sellhq_11000_DNARoot', (0.0, -353, 27.5), -180.0, _SELLBOT_SIGN_SF,
             [('Sellbot', -0.25, 0.075), ('Headquarters', -0.34, 0.1)]),
            ('doorway1', (62.74, -87.99, 17.26), 2.72, _SELLBOT_ELEVATOR_SF,
             [('Factory', -0.25, 0.075), ('Front Entrance', -0.34, 0.1)]),
            ('doorway2', (-164.78, 26.28, 17.25), -89.89, _SELLBOT_ELEVATOR_SF,
             [('Factory', -0.25, 0.075), ('Side Entrance', -0.34, 0.1)])],
}


# Text the loaders write on signs already in the model (OnscreenText, suit font): (sign node
# path, locator path, offset from the locator, text, scale, colour, sign made a decal).
# CashbotCogHQLoader: the Dreamland tunnel; BossbotCogHQLoader.makeSigns: GlobalStreetNames
# upper-cased, setPosHpr(locator, 0, -0.1, -0.25); GZSafeZoneLoader.enterPlayground: the
# Bossbot HQ tunnel of Chip 'n Dale's MiniGolf (bake_street.py applies that one).
HQ_TEXT_SIGNS = {
    12000: [('**/EntranceFrameFront', '**/sign_origin', (0, 0, 0), "Donald's Dreamland", 3,
             (0.87, 0.87, 0.87, 1), True)],
    17000: [('**/linktunnel_bosshq_10000_DNARoot/**/Sign_5', '**/linktunnel_bosshq_10000_DNARoot/**/sign_origin',
             (0, 0, -0.3), 'BOSSBOT HQ', 1.5, (0, 0, 0, 1), True)],
    10000: [('**/' + top + '/**/' + sign, '**/' + top + '/**/sign_origin', (0, -0.1, -0.25), text, 1.12,
             (0, 0, 0, 1), False)
            for top, sign, text in (('Gate_2', 'Sign_6', 'THE BACK NINE'), ('TunnelEntrance', 'Sign_2', 'PLAYGROUND'),
                                    ('Gate_3', 'Sign_3', 'THE MIDDLE SIX'), ('Gate_4', 'Sign_4', 'THE FRONT THREE'),
                                    ('GateHouse', 'Sign_5', 'THE CLUBHOUSE'))],
}


def _suit_font(resources):
    # ToontownGlobals.getSuitFont; a fresh font per zone keeps the bake order-independent.
    from panda3d.core import DynamicTextFont, Filename
    font = DynamicTextFont(Filename.fromOsSpecific(str(Path(resources) / 'phase_3/models/fonts/vtRemingtonPortable.ttf')))
    font.setPixelsPerUnit(40)
    font.setSpaceAdvance(0.25)
    font.setLineHeight(1.0)
    return font


def _letters(parent, font, text, color):
    from panda3d.core import TextNode
    import sign_text
    tn = TextNode('sign_letters')
    tn.setFont(font)
    tn.setAlign(TextNode.ACenter)
    tn.setTextColor(*color)
    tn.setText(text)
    letters = parent.attachNewNode(tn.generate())
    for tex in letters.findAllTextures():
        sign_text._FONT_TEXTURES.add(int(tex.this))
    letters.setDepthWrite(False)
    letters.setTag('sign_text', '1')
    return letters


def hq_text_signs(zone_id, root, resources):
    """Text the Cashbot / Bossbot HQ loaders write on the model's own signs at run time."""
    from panda3d.core import DecalEffect
    made = []
    font = _suit_font(resources) if zone_id in HQ_TEXT_SIGNS else None
    for sign_path, locator_path, offset, text, scale, color, decal in HQ_TEXT_SIGNS.get(zone_id, ()):
        sign, locator = root.find(sign_path), root.find(locator_path)
        if sign.isEmpty() or locator.isEmpty():
            raise ValueError(f'{zone_id}: sign {sign_path} / {locator_path} missing')
        if decal:
            sign.node().setEffect(DecalEffect.make())
        letters = _letters(sign, font, text, color)
        letters.setScale(scale)
        letters.setPosHpr(locator, *offset, 0, 0, 0)
        made.append(text)
    return made


def sellbot_signs(zone_id, root, loader, resources):
    """The Sellbot HQ / factory exterior signs the stock loader adds at run time."""
    if zone_id not in SELLBOT_SIGNS:
        return hq_text_signs(zone_id, root, resources)
    from panda3d.core import DecalEffect
    card = loader.loadModel(str(Path(resources) / 'phase_4/models/props/sign_sellBotHeadHQ.bam'), noCache=True)
    sign = card.find('**/sign_sellBotHeadHQ')
    if sign.isEmpty():
        raise ValueError('sign_sellBotHeadHQ missing')
    font = _suit_font(resources)
    made = []
    for parent_name, pos, h, sf, lines in SELLBOT_SIGNS[zone_id]:
        parent = root.find('**/' + parent_name)
        if parent.isEmpty():
            raise ValueError(f'{zone_id}: sign parent {parent_name} missing')
        copy = sign.copyTo(parent)
        copy.setPosHprScale(*pos, h, 0, 0, sf, sf, sf * _SELLBOT_ASPECT)
        copy.node().setEffect(DecalEffect.make())
        for text, z, scale in lines:
            letters = _letters(copy, font, text, (0, 0, 0, 1))
            letters.setPos(0, 0, z)
            letters.setScale(scale)
        made.append(parent_name)
    card.removeNode()
    return made


def write_points(root, cfg, path):
    """Anchors in the .pts format Place parses: name x y z h (zone space)."""
    lines = []
    for new in cfg['tunnels'].values():
        n = root.find('**/' + new)
        origin = n.find('**/tunnel_origin')
        ref = origin if not origin.isEmpty() else n
        p, h = ref.getPos(root), ref.getH(root)
        lines.append(f'{new} {p[0]:.3f} {p[1]:.3f} {p[2]:.3f} {h:.2f}')
    for index, dest in sorted(cfg['doors'].items()):
        o = root.find(f'**/cogdoor_{dest}_{index}_DNARoot/**/door_origin_{index}')
        if o.isEmpty():
            o = root.find(f'**/door_origin_{index}')
        if o.isEmpty():
            continue
        p, h = o.getPos(root), o.getH(root)
        lines.append(f'cogdoor_{dest}_{index} {p[0]:.3f} {p[1]:.3f} {p[2]:.3f} {h:.2f}')
    Path(path).write_text('\n'.join(lines) + ('\n' if lines else ''))
    return len(lines)


def bake(zone_id, args, base):
    import numpy as np
    import collide
    import coghq_catalog
    import street_atlas
    import extract
    import street_lod
    import ttm
    import tz
    from bake_street import (FAR_BODY, LOD_MUST, SHARED_NEAR_ATLAS as NEAR_ATLAS, NEW_LOD_NEAR, OLD_LOD_NEAR, FarBudgetError,
                             choose_far, grid_ladder, lower_draw_bound, prefer_far, split_zone_grid,
                             straighter_far)
    cfg = coghq_catalog.CATALOG[zone_id]
    root, info = build_scene(zone_id, cfg, base.loader, args.resources)
    ex = street_atlas.Extractor()
    whole = collide.extract(root, fallback_floor=False)
    collision, collision_stats = whole.pack()
    ex.add(root)
    raw_batches, ground_stats = extract.subdivide_ground(ex.batches())
    floor_tris = [np.array([a, b, c]) for a, b, c, normal, flags in whole.tris
                  if flags & collide.FLOOR and not flags & collide.TRIGGER and normal[2] >= 0.707]
    if not floor_tris:
        raise ValueError(f'{zone_id}: no floor collision')
    # Same far path as playgrounds. --grid-size is the floor. Coarser rungs are
    # tried until the far view fits the draw cap, and a fit that voxel-deleted
    # the mesh (under FAR_BODY triangles) does not stop the search. The vis
    # reaches the New 3DS draw distance (distance plus the 78-unit margin).
    chosen = None
    for size in grid_ladder(args.grid_size):
        trial_raw, trial_chunks, trial_floors = split_zone_grid(
            raw_batches, floor_tris, zone_id, size, args.distance)
        if len(trial_chunks) > 256 or lower_draw_bound(trial_raw, trial_chunks) > street_lod.DRAW_CAP:
            continue
        try:
            trial_low, trial_tex, trial_lod = choose_far(ex.sources, trial_raw, trial_chunks)
        except FarBudgetError:
            continue
        if chosen is not None and not prefer_far(trial_lod, chosen[6]):
            continue
        chosen = (size, trial_raw, trial_chunks, trial_floors, trial_low, trial_tex, trial_lod)
        if trial_lod['low_worst_triangles'] >= FAR_BODY:
            break
    if chosen is None:
        raise ValueError(f'{zone_id}: no grid size keeps the far LOD within {street_lod.DRAW_CAP} draws')
    size, raw, chunks, floors, low, far_textures, lod_report = chosen
    better = straighter_far(ex.sources, raw_batches, floor_tris, zone_id, size, args.distance, lod_report)
    if better is not None:
        raw, chunks, floors, low, far_textures, lod_report = better
        lod_report = dict(lod_report, straight_seams=True)
    textures, batches = street_atlas.build(ex.sources, raw, **NEAR_ATLAS)
    batches = [[street_atlas.rebase_large_uvs(b, textures) for b in bs] for bs in batches]
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    def pack(bs, tmp, tex_list):
        if not bs:
            return b'', {'batches': 0, 'triangles': 0, 'geometry_bytes': 0, 'texture_bytes': 0,
                         'bounds': [[0, 0, 0], [0, 0, 0]]}
        used = sorted({b.texture for b in bs if b.texture is not None})
        mapping = {old: new for new, old in enumerate(used)}
        copies = [ttm.Batch(mapping[b.texture] if b.texture is not None else None,
                            b.blend, b.two_sided, b.tint, b.layer, b.verts, b.indices) for b in bs]
        path = Path(tmp) / 'chunk.ttm'
        stats = ttm.write(path, [tex_list[j] for j in used], copies)
        if stats['uv_clamped']:
            raise ValueError('UV overflow')
        return path.read_bytes(), stats
    with tempfile.TemporaryDirectory() as tmp:
        for chunk, bs in zip(chunks, batches):
            chunk['model'], chunk['stats'] = pack(bs, tmp, textures)
    with tempfile.TemporaryDirectory() as tmp:
        for chunk, bs in zip(chunks, low):
            chunk['low_model'], chunk['low_stats'] = pack(bs, tmp, far_textures)
            if chunk['low_model']:
                chunk['stats']['bounds'] = [
                    np.minimum(chunk['stats']['bounds'][0], chunk['low_stats']['bounds'][0]).tolist(),
                    np.maximum(chunk['stats']['bounds'][1], chunk['low_stats']['bounds'][1]).tolist()]
    before = tz.report(chunks)
    low_report = tz.report([dict(c, stats=c['low_stats']) for c in chunks])
    samples = []
    for i, c in enumerate(chunks):
        points = [np.mean(f[0], axis=0) for f in floors if f[1] == i]
        if not points:
            points = [np.mean(c['stats']['bounds'], axis=0)]
        for p in points[::max(1, len(points) // 8)]:
            for near in (OLD_LOD_NEAR, NEW_LOD_NEAR):
                _, metrics = street_lod.plan(chunks, i, p, near=near, must=LOD_MUST[near])
                samples.append(metrics)
    planned = {k: max(row[k] for row in samples) for k in ('batches', 'triangles', 'geometry_bytes', 'texture_bytes')}
    triggers = []
    for t in whole.triggers:
        tunnel = 'tunnel' in (t.name + ' ' + t.owner).lower()
        triggers.append({'min': list(t.lo), 'max': list(t.hi),
                         'kind': 2 if tunnel else 1 if 'door' in t.name.lower() else 0,
                         'name': t.name, 'owner': t.owner})
    report = tz.write(out / f'{zone_id}.tz', zone_id, chunks, floors, triggers, collision)
    num_points = write_points(root, cfg, out / f'{zone_id}.pts')
    import hq_arrivals
    arrival = hq_arrivals.write_anchor(args.resources, zone_id, out/f'{zone_id}.pts', whole)
    num_points += int(arrival is not None)
    report.update({
        'arrival': arrival, 'version': 3, 'zone': zone_id, 'name': cfg['name'], 'model': cfg['model'],
        'before': before['worst'], 'low': low_report['worst'], 'planned_worst': planned, 'lod': lod_report,
        'points': num_points, 'floor_triangles': len(floors), 'triggers': len(triggers),
        'trigger_list': [{k: t[k] for k in ('kind', 'name', 'owner')} for t in triggers],
        'file_bytes': (out / f'{zone_id}.tz').stat().st_size, 'atlas_pages': len(textures),
        'grid_size': size, 'distance': args.distance, 'draw_cap': street_lod.DRAW_CAP,
        'near_atlas': NEAR_ATLAS, 'ground_split': ground_stats, 'collision': collision_stats, **info,
        'source_triangles': sum(len(b.indices) // 3 for b in raw_batches),
        'source_batches': len(raw_batches)})
    (out / f'{zone_id}.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k not in ('visible_sets', 'collision', 'trigger_list')}, indent=2),
          flush=True)
    root.removeNode()
    return out / f'{zone_id}.tz'


def bake_music(out_dir, resources):
    import shutil
    import bake_audio
    import coghq_catalog
    audio = Path(out_dir).parent / 'audio'
    audio.mkdir(parents=True, exist_ok=True)
    for hood, src in sorted(coghq_catalog.MUSIC.items()):
        dst = audio / f'{hood}.tsnd'
        bake_audio.bake_music(str(Path(resources) / src), str(dst), True)
        print(f'music {hood}: {src} -> {dst} ({dst.stat().st_size} bytes)', flush=True)
        for zid in coghq_catalog.HOOD_ZONES.get(hood, ()):
            z_dst = audio / f'{zid}.tsnd'
            if z_dst != dst:
                shutil.copyfile(dst, z_dst)
                print(f'music {zid}: copied from {hood}.tsnd ({z_dst.stat().st_size} bytes)', flush=True)


def main():
    import coghq_catalog
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--zone', default='all', help='all or comma-separated zone ids')
    ap.add_argument('--resources', default=default_resources())
    ap.add_argument('--out', default=str(ROOT / 'sdmc-out/3ds/toontown3ds/data/zones'))
    ap.add_argument('--grid-size', type=float, default=None,
                    help='minimum cell size; the baker raises it until the far draw cap fits '
                         '(Cashbot 12000 starts at 192, other HQ zones at 96)')
    ap.add_argument('--distance', type=float, default=760,
                    help='cell vis radius; plus 78 covers the New 3DS draw distance')
    ap.add_argument('--preview', action='store_true')
    ap.add_argument('--audio', action='store_true', help='also bake the four HQ music tracks')
    args = ap.parse_args()
    if (args.grid_size is not None and args.grid_size <= 0) or args.distance <= 0:
        ap.error('grid size and distance must be positive')
    zones = coghq_catalog.ZONES if args.zone == 'all' else tuple(int(z) for z in args.zone.split(','))
    for z in zones:
        if z not in coghq_catalog.CATALOG:
            ap.error(f'unsupported zone {z}')
    if len(zones) > 1:
        for z in zones:
            command = [sys.executable, '-I', str(Path(__file__).resolve()), '--zone', str(z),
                       '--resources', args.resources, '--out', args.out,
                       '--grid-size', str(args.grid_size or (192 if z==12000 else 96)), '--distance', str(args.distance)]
            if args.preview:
                command.append('--preview')
            subprocess.run(command, check=True)
        if args.audio:
            bake_music(args.out, args.resources)
        return
    args.grid_size = args.grid_size or (192 if zones[0]==12000 else 96)
    from panda3d.core import loadPrcFileData, Filename, getModelPath, BamCache
    BamCache.getGlobalPtr().setActive(False)
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                    'textures-power-2 none\nmodel-cache-dir')
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()
    getModelPath().prependDirectory(Filename.fromOsSpecific(os.path.abspath(args.resources)))
    print(f'Baking zone {zones[0]}', flush=True)
    path = bake(zones[0], args, base)
    base.destroy()
    if args.preview:
        subprocess.run([sys.executable, '-I', str(HERE / 'street_preview.py'), str(path),
                        str(ROOT / 'sdmc-out/previews/coghq')], check=True)
    if args.audio:
        bake_music(args.out, args.resources)


if __name__ == '__main__':
    main()
