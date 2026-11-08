#!/usr/bin/env python3
"""Bake the street look of a Cog-owned building (DistributedBuilding.setToSuit).

Upstream, when the Cogs take a block, every ``tb<block>:`` node (the Toon landmark and the
block's flat buildings) is stashed. libtoontown has already built the Cog version next to
them: a ``sb<block>:`` flat per Toon flat (a random ``suit_wall`` scaled to the flat's width
and wall height, plus ``suit_door`` when the flat had a door) and an ``sb<block>:`` origin at
the landmark's ``suit_building_origin``. setupSuitBuilding puts ``suit_landmark_<dept><level>``
there, hangs ``suit_sign`` with "<title>, Inc.\\n<department>" in the suit font on its
``sign_origin``, and loadElevator puts the elevator (with the department icon on the cab) on
its ``*_door_origin``.

The street meshes are baked flat, so the runtime hides the Toon block by collapsing the
triangles that lie inside its boxes (``source/world/cog_buildings.cpp``). This tool writes:

  --global  data/cogbldg/suit_landmark_{c,l,m,s}.ttm, suit_wall_{0..9}.ttm, suit_door.ttm,
            cog_icon_{c,l,m,s}.ttm and suit_exterior.bin (TCX1: per department the
            sign_origin and door_origin transforms in landmark space).
  --zone Z  data/zones/<Z>.tsb (TSB1, below) and <Z>_cogsign.ttm (each block's sign for each
            department, landmark space; two batches per sign: card, then letters).

TSB1 (little endian):
  char[4] "TSB1"; u16 version (1); u16 numBlocks
  per block:
    u16 block; u16 signBatch (first batch of department c; l, m, s follow, 2 batches each)
    u16 numFlats; u16 numHide; u16 numKeep; u16 pad
    f32[12] landmark: world from suit-origin, rows x axis, y axis, z axis, origin
    f32[3] lo, f32[3] hi: world AABB of the hide boxes
    numFlats x { f32[12] world from flat (scale width, 1, wall height folded in);
                 u8 wall (0..9); u8 door; u8 pad[2] }
    numHide + numKeep x { f32[12] box from world (rows as above); f32[3] min; f32[3] max }
A triangle of a street chunk is hidden when its three corners are inside one hide box, not
all within 0.4 of that box's floor (the sidewalk under the building stays), and not all inside
one keep box (DNA props and neighbouring buildings that overlap the block).
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

DEPTS = 'clms'
DEPT_MODELS = {'c': 'corp', 'l': 'legal', 'm': 'money', 's': 'sales'}
DEPT_NAMES = {'c': 'Bossbot', 'l': 'Lawbot', 'm': 'Cashbot', 's': 'Sellbot'}  # SuitDNA.suitDeptFullnames
ICONS = {'c': 'CorpIcon', 'l': 'LegalIcon', 'm': 'MoneyIcon', 's': 'SalesIcon'}
MEDALLION = {'c': (0.863, 0.776, 0.769, 1.0), 's': (0.843, 0.745, 0.745, 1.0),
             'l': (0.749, 0.776, 0.824, 1.0), 'm': (0.749, 0.769, 0.749, 1.0)}  # Suit.medallionColors
SUIT_WALLS = [f'wall_suit_build{i}_{s}' for i in range(1, 6) for s in ('ur', 'ul')]  # storage_town.dna order
COGS_INC = 'COGS, Inc.'   # TTLocalizer.CogsInc
INC_EXT = ', Inc.'        # TTLocalizer.CogsIncExt
TAKEABLE = ('landmark_building', 'anim_building')
KEEP_MAX_EXTENT = 60.0    # keep boxes are props and building parts, not streets or ground
EPS = 0.05


def _mat_rows(m):
    """Panda row-vector LMatrix4 -> 12 floats: x axis, y axis, z axis, origin."""
    return [m.getCell(r, c) for r in range(4) for c in range(3)]


def _base():
    from panda3d.core import loadPrcFileData, BamCache
    BamCache.getGlobalPtr().setActive(False)
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                        'textures-power-2 none\nmodel-cache-dir')
    from direct.showbase.ShowBase import ShowBase
    return ShowBase()


def _load(base, resources, rel):
    model = base.loader.loadModel(str(Path(resources) / rel), noCache=True)
    if model is None or model.isEmpty():
        raise FileNotFoundError(rel)
    return model


def _write(path, ex, batches=None):
    import ttm
    batches = ex.batches() if batches is None else batches
    stats = ttm.write(str(path), ex.textures, batches)
    if stats['uv_clamped']:
        raise ValueError(f'{path}: UV overflow')
    return stats


def bake_global(base, resources, out):
    import extract
    from panda3d.core import NodePath
    out.mkdir(parents=True, exist_ok=True)
    report = {}
    anchors = []
    for d in DEPTS:
        model = _load(base, resources, f'phase_4/models/modules/suit_landmark_{DEPT_MODELS[d]}.bam')
        sign = model.find('**/sign_origin;+s')
        door = model.find('**/*_door_origin;+s')
        if sign.isEmpty() or door.isEmpty():
            raise RuntimeError(f'suit_landmark_{DEPT_MODELS[d]}: missing sign_origin or door_origin')
        anchors.append((_mat_rows(sign.getMat(model)), _mat_rows(door.getMat(model))))
        ex = extract.Extractor(max_tex=256, decals=True)
        ex.add(model)
        report[f'suit_landmark_{d}'] = _write(out / f'suit_landmark_{d}.ttm', ex)
        model.removeNode()
    walls = _load(base, resources, 'phase_5/models/modules/suit_walls.bam')
    for i, code in enumerate(SUIT_WALLS):
        node = walls.find(f'**/{code}')
        if node.isEmpty():
            raise RuntimeError(f'suit_walls: missing {code}')
        # storage copies keep the node's own transform (assemble._find / copyTo).
        holder = NodePath('wall')
        node.copyTo(holder)
        ex = extract.Extractor(max_tex=256)
        ex.add(holder)
        report[f'suit_wall_{i}'] = _write(out / f'suit_wall_{i}.ttm', ex)
        holder.removeNode()
    door = _load(base, resources, 'phase_5/models/modules/suit_door.bam')
    ex = extract.Extractor(max_tex=128)
    ex.add(door)
    report['suit_door'] = _write(out / 'suit_door.ttm', ex)
    icons = _load(base, resources, 'phase_3/models/gui/cog_icons.bam')
    for d in DEPTS:
        icon = icons.find(f'**/{ICONS[d]}')
        if icon.isEmpty():
            raise RuntimeError(f'cog_icons: missing {ICONS[d]}')
        # loadElevator: copyTo(cab), pos (0, 6.79, 6.8), scale 3, setColor(medallionColors).
        holder = NodePath('icon')
        copy = icon.copyTo(holder)
        copy.setPos(0, 6.79, 6.8)
        copy.setScale(3)
        copy.setColor(*MEDALLION[d])
        ex = extract.Extractor(max_tex=128)
        ex.add(holder)
        report[f'cog_icon_{d}'] = _write(out / f'cog_icon_{d}.ttm', ex)
        holder.removeNode()
    with open(out / 'suit_exterior.bin', 'wb') as f:
        f.write(struct.pack('<4sHH', b'TCX1', 1, len(DEPTS)))
        for sign, door_m in anchors:
            f.write(struct.pack('<24f', *_r(sign), *_r(door_m)))
    return report


def _flat_info(d):
    """DNA flat_building -> (total wall height, has a flat_door)."""
    height, door = 0.0, False
    for c in d.children:
        if c.kind == 'wall':
            height += float(c.get('height', 10.0))
            door = door or any(w.kind == 'flat_door' for w in c.walk())
    return height, door


def _r(values):
    # Panda's bounds and inverses differ in the last bit between processes; round so a zone
    # bakes to the same bytes alone or in a batch.
    return [round(float(v), 5) + 0.0 for v in values]


def _block_of(name):
    if not name or not name.startswith('tb') or ':' not in name:
        return None
    digits = name[2:name.index(':')]
    return int(digits) if digits.isdigit() else None


def _box(np_, root, pad=EPS):
    """Oriented box of np_'s geometry in np_'s own frame: (box from world rows, min, max)."""
    from panda3d.core import LMatrix4f
    bounds = np_.getTightBounds(np_)
    if bounds is None:
        return None
    lo, hi = bounds
    inv = LMatrix4f(np_.getMat(root))
    if not inv.invertInPlace():
        return None
    return (_mat_rows(inv), [lo[i] - pad for i in range(3)], [hi[i] + pad for i in range(3)])


def _world_aabb(np_, root):
    b = np_.getTightBounds(root)
    if b is None:
        return None
    return [b[0][i] for i in range(3)], [b[1][i] for i in range(3)]


def _overlap(a, b):
    return all(a[0][i] <= b[1][i] and b[0][i] <= a[1][i] for i in range(3))


def _corners_inside(box, hide):
    best = 0
    for rows, bmin, bmax in hide:
        n = 0
        for c in range(8):
            p = [box[(c >> i) & 1][i] for i in range(3)]
            q = [rows[9 + j] + sum(p[i] * rows[3 * i + j] for i in range(3)) for j in range(3)]
            n += all(bmin[j] <= q[j] <= bmax[j] for j in range(3))
        best = max(best, n)
    return best


def _wall_choice(block, name):
    return int.from_bytes(hashlib.sha1(f'{block}:{name}'.encode()).digest()[:4], 'little') % len(SUIT_WALLS)


def bake_zone(base, resources, zone, out):
    import assemble
    import dna
    import extract
    import street_catalog
    from panda3d.core import NodePath, TextNode

    store = dna.Storage(base.loader, str(resources))
    playground, storages, src = street_catalog.paths(zone)
    if playground:
        raise ValueError(f'{zone}: Cog buildings only stand on streets')
    for s in storages:
        store.load(str(Path(resources) / s))
    tree = dna.parse_file(Path(resources) / src, [])
    root, _ = assemble.assemble(tree, store, seed=1)

    # DNA facts per block: the takeable landmark (building_type '': not an HQ, shop or
    # pet / kart shop) with its title, and the flats.
    # DNA reuses flat names inside a block ("tb10:random20_DNARoot" three times), so each
    # DNA flat is matched to the assembled node of the same name by occurrence order.
    landmarks, flats, seen, matches = {}, {}, {}, {}
    for d in tree.walk():
        block = _block_of(d.name)
        if block is None:
            continue
        if d.kind in TAKEABLE and d.get('building_type', '') in ('', None):
            landmarks[block] = (d.name, str(d.get('title', '') or ''))
        elif d.kind == 'flat_building':
            if d.name not in matches:
                matches[d.name] = list(root.findAllMatches(f'**/{d.name}'))
            k = seen.get(d.name, 0)
            seen[d.name] = k + 1
            if k < len(matches[d.name]):
                flats.setdefault(block, []).append((d, matches[d.name][k]))

    tb_nodes = {}
    for n in root.findAllMatches('**/tb*:*'):
        block = _block_of(n.getName())
        if block is not None and not n.getParent().getName().startswith('tb'):
            tb_nodes.setdefault(block, []).append(n)

    # Every GeomNode outside the block that could overlap it: DNA props, other blocks.
    pieces = []
    for g in root.findAllMatches('**/+GeomNode'):
        if g.isStashed() or g.isHidden():
            continue
        box = _world_aabb(g, root)
        if box is None or max(box[1][i] - box[0][i] for i in range(2)) > KEEP_MAX_EXTENT:
            continue
        p, owner = g, None
        while not p.isEmpty() and p != root:
            owner = _block_of(p.getName()) if owner is None else owner
            p = p.getParent()
        pieces.append((g, owner, box))

    # ToontownGlobals.getSuitFont. A fresh font per zone: its glyph pages then hold only this
    # zone's letters, so a zone bakes to the same bytes alone or in a batch.
    from panda3d.core import DynamicTextFont, Filename
    font = DynamicTextFont(Filename.fromOsSpecific(str(Path(resources) / 'phase_3/models/fonts/vtRemingtonPortable.ttf')))
    font.setPixelsPerUnit(40)
    font.setSpaceAdvance(0.25)
    font.setLineHeight(1.0)
    card = _load(base, resources, 'phase_5/models/modules/suit_sign.bam')
    signs_root = NodePath('signs')
    sign_origin = {}
    for d in DEPTS:
        model = _load(base, resources, f'phase_4/models/modules/suit_landmark_{DEPT_MODELS[d]}.bam')
        sign_origin[d] = model.find('**/sign_origin;+s').getMat(model)
        model.removeNode()

    blocks, sign_nodes = [], []
    for block in sorted(landmarks):
        name, title = landmarks[block]
        lm = root.find(f'**/{name}')
        if lm.isEmpty():
            continue
        origin = lm.find('**/*suit_building_origin')
        landmark_mat = (origin if not origin.isEmpty() else lm).getMat(root)
        hide, aabbs = [], []
        for n in tb_nodes.get(block, []):
            box = _box(n, root)
            aabb = _world_aabb(n, root)
            if box is None or aabb is None:
                continue
            hide.append(box)
            aabbs.append(aabb)
        if not hide:
            continue
        lo = [min(a[0][i] for a in aabbs) for i in range(3)]
        hi = [max(a[1][i] for a in aabbs) for i in range(3)]
        keep = []
        for g, owner, box in pieces:
            # Only pieces that sit mostly inside the block's boxes can lose whole triangles.
            if owner == block or not _overlap(box, (lo, hi)) or _corners_inside(box, hide) < 4:
                continue
            k = _box(g, root, pad=EPS)
            if k is not None:
                keep.append(k)
        flat_rows = []
        for d, fnode in flats.get(block, []):
            height, has_door = _flat_info(d)
            if height <= 0:
                continue
            m = fnode.getMat(root)
            rows = _mat_rows(m)
            width = float(d.get('width', 10.0))
            rows[0:3] = [v * width for v in rows[0:3]]
            rows[6:9] = [v * height for v in rows[6:9]]
            flat_rows.append((rows, _wall_choice(block, f'{d.name}#{len(flat_rows)}'), 1 if has_door else 0))
        # setupSuitBuilding: "<title>, Inc." (or "COGS, Inc.") then the department on its own line.
        for d in DEPTS:
            text = TextNode('sign')
            text.setTextColor(1, 1, 1, 1)
            text.setFont(font)
            text.setAlign(TextNode.ACenter)
            text.setWordwrap(17.0)
            text.setText((title + INC_EXT if title else COGS_INC) + '\n' + DEPT_NAMES[d])
            height = text.getHeight()
            z_scale = (height + 2) / 3.0
            holder = signs_root.attachNewNode(f'{block}{d}')
            at = holder.attachNewNode('sign_origin')
            at.setMat(sign_origin[d])
            bg = card.copyTo(at)
            bg.setPosHprScale(0, 0, height * 0.8 / z_scale, 0, 0, 0, 8, 8, 8 * z_scale)
            letters = bg.attachNewNode(text.generate())
            letters.setPosHprScale(0, 0, -0.21 + height * 0.1 / z_scale, 0, 0, 0, 0.1, 0.1, 0.1 / z_scale)
            letters.setColor(1, 1, 1, 1)
            sign_nodes.append((holder, bg, letters))
        blocks.append((block, landmark_mat, lo, hi, flat_rows, hide, keep))

    # Every glyph is on the dynamic font's pages before any texture is read.
    ex = extract.Extractor(max_tex=256)
    sign_batches = []
    for holder, bg, letters in sign_nodes:
        letters.wrtReparentTo(holder)
        bg.wrtReparentTo(holder)
        for part, layer in ((bg, 1), (letters, 2)):
            ex._groups = {}
            tmp = NodePath('part')
            part.copyTo(tmp).setMat(part.getMat(holder))
            ex.add(tmp)
            bs = ex.batches()
            if len(bs) != 1:
                raise RuntimeError(f'{zone} sign {holder.getName()}: {len(bs)} batches for one part')
            bs[0].layer = layer
            sign_batches.append(bs[0])
    out.mkdir(parents=True, exist_ok=True)
    sign_stats = _write(out / f'{zone}_cogsign.ttm', ex, sign_batches) if sign_batches else None

    with open(out / f'{zone}.tsb', 'wb') as f:
        f.write(struct.pack('<4sHH', b'TSB1', 1, len(blocks)))
        for i, (block, landmark_mat, lo, hi, flat_rows, hide, keep) in enumerate(blocks):
            f.write(struct.pack('<HHHHHH', block, i * len(DEPTS) * 2, len(flat_rows), len(hide), len(keep), 0))
            f.write(struct.pack('<12f', *_r(_mat_rows(landmark_mat))))
            f.write(struct.pack('<6f', *_r(lo), *_r(hi)))
            for rows, wall, door in flat_rows:
                f.write(struct.pack('<12fBBBB', *_r(rows), wall, door, 0, 0))
            for rows, bmin, bmax in hide + keep:
                f.write(struct.pack('<18f', *_r(rows), *_r(bmin), *_r(bmax)))
    root.removeNode()
    return {'zone': zone, 'blocks': len(blocks), 'signs': sign_stats,
            'flats': sum(len(b[4]) for b in blocks), 'hide_boxes': sum(len(b[5]) for b in blocks),
            'keep_boxes': sum(len(b[6]) for b in blocks)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--resources', default=str(ROOT / 'ref/resources'))
    ap.add_argument('--data', default=str(ROOT / 'sdmc-out/3ds/toontown3ds/data'))
    ap.add_argument('--global', dest='do_global', action='store_true')
    ap.add_argument('--zone', default='', help='street zone ids, comma separated, or "streets"')
    args = ap.parse_args()
    import json
    import street_catalog
    zones = []
    if args.zone:
        zones = ([z for _, (_, _, _, streets) in sorted(street_catalog.HOODS.items()) for z in streets]
                 if args.zone == 'streets' else [int(z) for z in args.zone.split(',')])
    if len(zones) + args.do_global > 1:
        # One process per output, like bake_street.py: Panda's loaded-model state otherwise
        # moves box corners by an ulp depending on what was baked before.
        import subprocess
        common = ['--resources', args.resources, '--data', args.data]
        if args.do_global:
            subprocess.run([sys.executable, '-I', __file__, '--global', *common], check=True)
        for z in zones:
            subprocess.run([sys.executable, '-I', __file__, '--zone', str(z), *common], check=True)
        return
    base = _base()
    data = Path(args.data)
    if args.do_global:
        print(json.dumps(bake_global(base, args.resources, data / 'cogbldg'), indent=1), flush=True)
    for z in zones:
        print(json.dumps(bake_zone(base, args.resources, z, data / 'zones')), flush=True)
    base.destroy()


if __name__ == '__main__':
    main()
