#!/usr/bin/env python3
"""Bake the fishing props: five rods, the bob, a line ribbon and the splash sounds.

    .venv/bin/python -I tools/pipeline/bake_fishing.py [--out sdmc-out/3ds/toontown3ds/data/fishing]
                                                       [--resources ref/resources]

Upstream (docs/design/fishing.md section 7):
  toontown/safezone/DistributedFishingSpot.py __loadStuff   rod = FishGlobals.RodFileDict[rodId],
        Actor with the 'cast' clip phase_4/models/props/fishing-pole-chan, pose('cast', 0);
        ptop = pole.find('**/joint_attachBill') is where the line starts;
        bob = phase_4/models/props/fishing_bob at scale 1.5; splash sounds TT_splash1/2.
Outputs (all deterministic):
  rod_<0..4>.ttm   pole_{treebranch,bamboo,wood,steel,gold}-mod posed at cast frame 0, in the
                   rod's own frame (butt at the origin, tip along +Y); rods.json has the
                   joint_attachBill (line start) position per rod.
  bob.ttm          fishing_bob, scaled 1.5 like upstream, origin at its centre.
  line.ttm         unit ribbon from (0,0,0) to (0,1,0) (two crossed quads, white); the runtime
                   scales it to the rod-tip -> bob distance. Upstream draws a Rope (alpha 0.4).
  splash1.tsnd, splash2.tsnd   TT_splash1/2.ogg as PCM16 (bake_audio.bake_sfx)
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

# FishGlobals.RodFileDict
RODS = ['pole_treebranch-mod', 'pole_bamboo-mod', 'pole_wood-mod', 'pole_steel-mod', 'pole_gold-mod']
BOB_SCALE = 1.5          # DistributedFishingSpot.__loadStuff: self.bob.setScale(1.5)
LINE_HALF_WIDTH = 0.02   # world units; the line is thin


def line_batch():
    import numpy as np
    import ttm
    verts, idx = [], []
    white = (1.0, 1.0, 1.0, 1.0)
    for ax, az in ((1.0, 0.0), (0.0, 1.0)):  # two crossed vertical/horizontal ribbons
        base = len(verts)
        w = LINE_HALF_WIDTH
        for y in (0.0, 1.0):
            verts.append([-ax * w, y, -az * w, 0.0, 0.0, *white])
            verts.append([ax * w, y, az * w, 0.0, 0.0, *white])
        idx += [base, base + 1, base + 3, base, base + 3, base + 2]
    return ttm.Batch(None, ttm.BLEND_OPAQUE, True, 0, 0, np.array(verts, dtype=np.float64),
                     np.array(idx, dtype=np.int64))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--resources', default=os.path.join(ROOT, 'ref', 'resources'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'sdmc-out', '3ds', 'toontown3ds', 'data', 'fishing'))
    ap.add_argument('--max-tex', type=int, default=128)
    ap.add_argument('--no-audio', action='store_true')
    args = ap.parse_args()

    from panda3d.core import NodePath, PandaNode, loadPrcFileData
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                    'textures-power-2 none\nmodel-cache-dir\n'
                    f'model-path {args.resources}\ndefault-model-extension .bam')
    from direct.actor.Actor import Actor
    from direct.showbase.ShowBase import ShowBase
    import extract
    import ttm

    base = ShowBase()
    os.makedirs(args.out, exist_ok=True)
    manifest = {'rods': [], 'bobScale': BOB_SCALE}
    props = 'phase_4/models/props/'

    for i, name in enumerate(RODS):
        pole = Actor(props + name, {'cast': props + 'fishing-pole-chan'})
        pole.pose('cast', 0)
        ex = extract.Extractor(max_tex=args.max_tex)
        ex.add(pole, animated=True)
        stats = ttm.write(os.path.join(args.out, f'rod_{i}.ttm'), ex.textures, ex.batches())
        tip = pole.find('**/joint_attachBill').getPos(pole)
        manifest['rods'].append({'file': f'rod_{i}.ttm', 'tip': [round(c, 4) for c in tip], **stats})
        pole.cleanup()
        pole.removeNode()

    bob = base.loader.loadModel(props + 'fishing_bob')
    holder = NodePath(PandaNode('bob_root'))
    inst = bob.copyTo(holder)
    inst.setScale(BOB_SCALE)
    ex = extract.Extractor(max_tex=args.max_tex)
    ex.add(holder)
    manifest['bob'] = ttm.write(os.path.join(args.out, 'bob.ttm'), ex.textures, ex.batches())

    manifest['line'] = ttm.write(os.path.join(args.out, 'line.ttm'), [], [line_batch()])

    if not args.no_audio:
        import bake_audio
        manifest['audio'] = {}
        for n, rel in (('splash1', 'phase_4/audio/sfx/TT_splash1.ogg'), ('splash2', 'phase_4/audio/sfx/TT_splash2.ogg')):
            size = bake_audio.bake_sfx(os.path.join(args.resources, rel), os.path.join(args.out, n + '.tsnd'), False)[0]
            manifest['audio'][n] = size

    with open(os.path.join(args.out, 'rods.json'), 'w') as f:
        json.dump(manifest, f, indent=1, sort_keys=True, default=str)
        f.write('\n')
    print(json.dumps(manifest, indent=1, sort_keys=True, default=str))
    base.destroy()


if __name__ == '__main__':
    main()
