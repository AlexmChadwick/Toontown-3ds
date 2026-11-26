"""bosssweep.py <out_dir> [vp|cfo|cj|ceo ...]

Boss arenas: the stock scene (source models placed like the boss bake / client) vs the baked
data/boss/<name>_arena.ttm from floor spots at eye height, four headings each.
One sheet per arena: rows = (spot, heading), columns stock | baked. A spot is only used when its
four stock frames show the arena (sweep_check.stock_ok); each baked frame is judged against its
stock frame. Writes <out_dir>/sweep-manifest.json and exits 1 on any failing frame.
'pair:<bam path under resources, no ext>=<ttm path>' compares any model with its bake.

    TT_RESOURCES=<ref/resources> .venv/bin/python -I tools/fidelity/bosssweep.py <out>
"""
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
from panda3d.core import (NodePath, SamplerState, CollisionTraverser, CollisionNode, CollisionRay,
                          CollisionHandlerQueue, GeomNode, PNMImage)
from PIL import Image
import preview_ttm
import sweep_check

BOSS = os.environ.get('TT_BOSS_DATA', str(ROOT / 'sdmc-out/3ds/toontown3ds/data/boss'))
HEADINGS = (0, 90, 180, 270)
out = sys.argv[1]
want = sys.argv[2:] or ['vp', 'cfo', 'cj', 'ceo']
os.makedirs(out, exist_ok=True)
base = ShowBase()
base.setBackgroundColor(*sweep_check.BG_FLOAT)
base.camLens.setFov(70, 46)
base.camLens.setNearFar(1, 4000)
tmp = tempfile.mkdtemp()
ld = lambda p: base.loader.loadModel(RES + p + '.bam', noCache=True)


def stock(name):
    if name.startswith('pair:'):
        return ld(name[5:].split('=')[0])
    if name == 'vp':
        a = ld('phase_9/models/cogHQ/BossRoomHQ')
        e = a.find('**/elevatorEntrance'); e.getChildren().detach(); e.setScale(1)
        ld('phase_9/models/cogHQ/cogHQ_elevator').reparentTo(e)
        c = a.find('**/cage'); c.setScale(1); c.setZ(100)
        return a
    if name == 'cfo':
        s = NodePath('cfo')
        for m, p in [('MidVault', (0, -222, -70.7)), ('EndVault', (84, -201, -6))]:
            n = ld('phase_10/models/cogHQ/' + m); n.setPos(*p); n.reparentTo(s)
            if m == 'EndVault':
                for pat in ('MagnetArms', 'Safes', 'MagnetControlsAll'):
                    n.findAllMatches('**/' + pat).detach()
            else:
                o = n.find('**/elevator_origin'); o.setScale(1)
                ld('phase_10/models/cogHQ/CFOElevator').reparentTo(o)
        for d in s.findAllMatches('**/SlidingDoor*'):
            if not d.node().isGeomNode() and 'Collision' not in d.getName():
                d.setZ(25)
        return s
    if name == 'cj':
        s = NodePath('cj')
        c = ld('phase_11/models/lawbotHQ/LawbotCourtroom3'); c.setPos(0, 0, -71.601); c.reparentTo(s)
        e = c.find('**/elevator_origin'); e.getChildren().detach(); e.setScale(1)
        ld('phase_11/models/lawbotHQ/LB_Elevator').reparentTo(e)
        for pat in ('SlidingDoor1', 'CR3_Door', 'Door_1'):
            for d in c.findAllMatches('**/' + pat):
                d.setZ(d.getZ() + 25)
        p = c.find('**/Podium')
        if not p.isEmpty():
            p.setZ(p.getZ() - 19.145)
        return s
    c = ld('phase_12/models/bossbotHQ/BanquetInterior_1')
    ld('phase_12/models/bossbotHQ/BB_Inside_Elevator').reparentTo(c.find('**/elevator_origin'))
    d = c.find('**/door3')
    if not d.isEmpty():
        d.setZ(d.getZ() + 30)
    for i in range(2):
        b = ld('phase_12/models/bossbotHQ/food_belt%d_model' % (i + 1)); b.reparentTo(c)
        b.setPos(c.find('**/conveyer_belt_start_%d' % (i + 1)).getPos(c))
    return c


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


# Where a Toon can stand, when the arena's bounds hold more: Lawbot's courtroom sits in a
# backdrop terrain about 1140 units below it, which the lowest-floor search otherwise picks.
REGION = {'cj': ((-150, -150, -100), (150, 150, 100))}


def candidates(geo, region=None):
    """Eye-height points over the lowest floor with 12 units of headroom, centre first."""
    lo, hi = geo.getTightBounds()
    zmin, zmax = -1e9, 1e9
    if region:
        (lo.x, lo.y, zmin), (hi.x, hi.y, zmax) = region
    trav = CollisionTraverser(); q = CollisionHandlerQueue()
    cn = CollisionNode('ray'); ray = CollisionRay(); cn.addSolid(ray)
    cn.setFromCollideMask(GeomNode.getDefaultCollideMask()); cn.setIntoCollideMask(0)
    rn = base.render.attachNewNode(cn); trav.addCollider(rn, q)
    res = []
    order = sorted(((i / 8, j / 8) for i in range(1, 8) for j in range(1, 8)),
                   key=lambda f: (f[0] - .5) ** 2 + (f[1] - .5) ** 2)
    for fx, fy in order:
        x, y = lo.x + (hi.x - lo.x) * fx, lo.y + (hi.y - lo.y) * fy
        ray.setOrigin(x, y, hi.z + 1); ray.setDirection(0, 0, -1)
        trav.traverse(base.render)
        zs = sorted(z for z in (q.getEntry(i).getSurfacePoint(base.render).z for i in range(q.getNumEntries()))
                    if zmin < z < zmax)
        for k, z in enumerate(zs):
            if k + 1 == len(zs) or zs[k + 1] > z + 12:
                res.append((x, y, z + 6))
                break
    rn.removeNode()
    return res, hi.x - lo.x


def pick_spots(orig, cands, span, n=4):
    """Spots whose four stock frames all show the arena, at least 0.18 of its width apart."""
    res = []
    for eye in cands:
        if any((eye[0] - a) ** 2 + (eye[1] - b) ** 2 <= (0.18 * span) ** 2 for a, b, _ in res):
            continue
        if all(sweep_check.stock_ok(sweep_check.stats(shot(orig, eye, h))) for h in HEADINGS):
            res.append(eye)
            if len(res) == n:
                break
    return res


manifest = {'kind': 'boss', 'background': list(sweep_check.BG), 'columns': ['stock', 'baked'],
            'camera': f'fov 70x46, pitch {sweep_check.PITCH}, eye 6 above the floor, headings 0/90/180/270',
            'items': []}
for name in want:
    tag = name.split('=')[0].split('/')[-1]
    bake = name.split('=')[1] if name.startswith('pair:') else f'{BOSS}/{name}_arena.ttm'
    item = {'name': tag, 'bake': os.path.relpath(bake, BOSS) if bake.startswith(BOSS) else bake,
            'sheet': f'{tag}.png', 'expected': f'{tag}: stock arena (models placed as the client does); bake matches stock',
            'spots': [], 'frames': []}
    manifest['items'].append(item)
    orig = stock(name)
    for t in orig.findAllTextures():
        t.setMinfilter(SamplerState.FT_linear_mipmap_linear)
    baked = preview_ttm.load(bake, {})
    baked.reparentTo(base.render)
    cands, span = candidates(baked, REGION.get(name))
    baked.detachNode()
    sp = pick_spots(orig, cands, span)
    item['spots'] = [[round(v, 2) for v in s] for s in sp]
    rows = []
    for eye in sp:
        for h in HEADINGS:
            row = Image.new('RGB', (800, 240))
            a, b = shot(orig, eye, h), shot(baked, eye, h)
            row.paste(a, (0, 0)); row.paste(b, (400, 0))
            item['frames'].append(sweep_check.frame_entry(
                item['sheet'], len(rows), eye, h, [('stock', sweep_check.stats(a)), ('baked', sweep_check.stats(b))]))
            rows.append(row)
    orig.removeNode(); baked.removeNode()
    if not rows:
        item['error'] = 'no camera spot where the stock arena is visible'
        print(name, 'no spots', flush=True)
        continue
    sheet = Image.new('RGB', (800, 240 * len(rows)))
    for i, r in enumerate(rows):
        sheet.paste(r, (0, 240 * i))
    sheet.save(f'{out}/{tag}.png')
    print(name, len(sp), 'spots', item['spots'], flush=True)

with open(f'{out}/sweep-manifest.json', 'w') as f:
    json.dump(manifest, f, indent=1)
sys.exit(1 if sweep_check.recheck(manifest, out) else 0)
