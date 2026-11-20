"""levelsweep.py <out_dir> [model_substr ...]

Facility rooms: the original room model plus its spec's model entities (Panda, full textures) vs the baked level .tz (near
chunks, and far chunks past 160 as Old 3DS) from eye-height floor spots, four headings each.
One sheet per room: rows = (spot, heading), columns stock | baked near | baked auto (Old).
A spot is only used when all four stock frames show the room (sweep_check.stock_ok); every
baked frame is judged against its stock frame. Writes <out_dir>/sweep-manifest.json and exits 1
when any room has a failing frame or no usable spot.

    TT_RESOURCES=<ref/resources> .venv/bin/python -I tools/fidelity/levelsweep.py <out>
"""
import glob
import json
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/pipeline'))
sys.path.insert(0, str(ROOT / 'tools/fidelity'))
RES = os.environ.get('TT_RESOURCES', str(ROOT / 'ref/resources')).rstrip('/') + '/'
from panda3d.core import loadPrcFileData
loadPrcFileData('', 'window-type offscreen\naudio-library-name null\nwin-size 400 240\n'
                f'model-path {RES}\nnotify-level error\nmodel-cache-dir\n')
from direct.showbase.ShowBase import ShowBase
from panda3d.core import (NodePath, SamplerState, CollisionTraverser, CollisionNode,
                          CollisionRay, CollisionHandlerQueue, GeomNode, PNMImage)
from PIL import Image
import tz
import preview_ttm
import sweep_check

LV = os.environ.get('TT_LEVEL_DATA', str(ROOT / 'sdmc-out/3ds/toontown3ds/data/level'))
HEADINGS = (0, 90, 180, 270)
out = sys.argv[1]
subs = sys.argv[2:]
os.makedirs(out, exist_ok=True)
base = ShowBase()
base.setBackgroundColor(*sweep_check.BG_FLOAT)
base.camLens.setFov(70, 46)
base.camLens.setNearFar(1, 2000)
tmp = tempfile.mkdtemp()

models = {}
for f in sorted(glob.glob(LV + '/*.json')):
    j = json.load(open(f))
    models.setdefault(j['model'], os.path.basename(f)[:-5])


def load_tz(path, mode, eye):
    data = open(path, 'rb').read()
    h = tz.HEADER.unpack_from(data)
    nc, co = h[4], h[8]
    root = NodePath('tz')
    for i in range(nc):
        off = co + i * tz.CHUNK_V2_SIZE
        c = tz.CHUNK.unpack_from(data, off)
        low = tz.LOW.unpack_from(data, off + 80)
        lo, hi = c[9:12], c[12:15]
        d = sum((max(lo[k], min(eye[k], hi[k])) - eye[k]) ** 2 for k in range(3)) ** .5
        use_near = mode == 'near' or d < 160
        o, n = (c[3], c[4]) if use_near else (low[0], low[1])
        if not n:
            o, n = c[3], c[4]
        if not n:
            continue
        p = os.path.join(tmp, f'{i}.ttm')
        open(p, 'wb').write(tz.model_bytes(data, o, n))
        preview_ttm.load(p, {}).reparentTo(root)
    return root


def shot(np_, eye, h):
    np_.reparentTo(base.render)
    base.camera.setPos(*eye)
    base.camera.setHpr(h, sweep_check.PITCH, 0)
    base.graphicsEngine.renderFrame()
    base.graphicsEngine.renderFrame()
    img = PNMImage()
    base.win.getScreenshot(img)
    p = os.path.join(tmp, 's.png')
    img.write(p)
    np_.detachNode()
    return Image.open(p).convert('RGB')


def candidates(geo):
    """Eye-height points over the lowest floor with 10 units of headroom, centre first."""
    lo, hi = geo.getTightBounds()
    trav = CollisionTraverser()
    q = CollisionHandlerQueue()
    cn = CollisionNode('ray')
    ray = CollisionRay()
    cn.addSolid(ray)
    cn.setFromCollideMask(GeomNode.getDefaultCollideMask())
    cn.setIntoCollideMask(0)
    rn = base.render.attachNewNode(cn)
    trav.addCollider(rn, q)
    res = []
    order = sorted(((i / 12, j / 12) for i in range(1, 12) for j in range(1, 12)),
                   key=lambda f: (f[0] - .5) ** 2 + (f[1] - .5) ** 2)
    for fx, fy in order:
        x, y = lo.x + (hi.x - lo.x) * fx, lo.y + (hi.y - lo.y) * fy
        ray.setOrigin(x, y, hi.z + 1)
        ray.setDirection(0, 0, -1)
        trav.traverse(base.render)
        zs = sorted(q.getEntry(i).getSurfacePoint(base.render).z for i in range(q.getNumEntries()))
        for k, z in enumerate(zs):
            if k + 1 == len(zs) or zs[k + 1] > z + 10:
                res.append((x, y, z + 5))
                break
    rn.removeNode()
    return res, hi.x - lo.x


def pick_spots(orig, cands, span, n=3):
    """Spots whose four stock frames all show the room, spread at least 0.2 of the room apart."""
    res = []
    for eye in cands:
        if any((eye[0] - a) ** 2 + (eye[1] - b) ** 2 <= (0.2 * span) ** 2 for a, b, _ in res):
            continue
        frames = [shot(orig, eye, h) for h in HEADINGS]
        if all(sweep_check.stock_ok(sweep_check.stats(f)) for f in frames):
            res.append(eye)
            if len(res) == n:
                break
    return res


def stock_scene(level, model):
    """The room as upstream builds it: the level model plus its spec's static model entities
    (bake_level.prepare_level runs the stock spec; the factory's cog_sky and crates are spec
    entities, not part of the .bam). Lift guards and spinner props are split out of both."""
    import argparse
    import bake_level
    args = argparse.Namespace(resources=RES.rstrip('/'))
    if level not in bake_level.LEVELS:
        bake_level.LEVELS.update(bake_level.room_levels())
    scene, _, _ = bake_level.prepare_level(level, args, base, Path(tmp), write=False)
    return scene


manifest = {'kind': 'facility', 'background': list(sweep_check.BG), 'columns': ['stock', 'near', 'auto_old'],
            'camera': f'fov 70x46, pitch {sweep_check.PITCH}, eye 5 above the floor, headings 0/90/180/270',
            'items': []}
for model, level in models.items():
    if subs and not any(s in model or s in level for s in subs):
        continue
    name = model.split('/')[-1]
    item = {'name': name, 'level': level, 'stock_model': model + '.bam', 'bake': f'level/{level}.tz',
            'sheet': f'{name}.png', 'expected': f'{name}: stock room geometry and textures; bake matches stock',
            'spots': [], 'frames': []}
    manifest['items'].append(item)
    try:
        orig = stock_scene(level, model)
    except (IOError, OSError, KeyError, ValueError) as error:
        item['error'] = f'no stock scene: {error}'
        print(level, 'no stock scene', model, error, flush=True)
        continue
    for t in orig.findAllTextures():
        t.setMinfilter(SamplerState.FT_linear_mipmap_linear)
    probe = load_tz(f'{LV}/{level}.tz', 'near', (0, 0, 0))
    probe.reparentTo(base.render)
    cands, span = candidates(probe)
    probe.removeNode()
    # The level's ENTRANCE locator (where a Toon arrives) is the first spot to try.
    entrance = orig.find('**/ENTRANCE')
    if not entrance.isEmpty():
        p = entrance.getPos(orig)
        cands.insert(0, (p.x, p.y, p.z + 5))
    sp = pick_spots(orig, cands, span)
    item['spots'] = [[round(v, 2) for v in s] for s in sp]
    rows = []
    for eye in sp:
        near = load_tz(f'{LV}/{level}.tz', 'near', eye)
        auto = load_tz(f'{LV}/{level}.tz', 'auto', eye)
        for h in HEADINGS:
            row = Image.new('RGB', (1200, 240))
            cols = []
            for k, (tag, n) in enumerate((('stock', orig), ('near', near), ('auto_old', auto))):
                im = shot(n, eye, h)
                row.paste(im, (400 * k, 0))
                cols.append((tag, sweep_check.stats(im)))
            item['frames'].append(sweep_check.frame_entry(item['sheet'], len(rows), eye, h, cols))
            rows.append(row)
        near.removeNode()
        auto.removeNode()
    orig.removeNode()
    if not rows:
        item['error'] = 'no camera spot where the stock room is visible'
        print(level, 'no spots', flush=True)
        continue
    sheet = Image.new('RGB', (1200, 240 * len(rows)))
    for i, r in enumerate(rows):
        sheet.paste(r, (0, 240 * i))
    sheet.save(f'{out}/{name}.png')
    bad = sum(f['result'] != 'PASS' for f in item['frames'])
    print(level, name, len(rows), 'rows', bad, 'failing', flush=True)

with open(f'{out}/sweep-manifest.json', 'w') as f:
    json.dump(manifest, f, indent=1)
sys.exit(1 if sweep_check.recheck(manifest) else 0)
