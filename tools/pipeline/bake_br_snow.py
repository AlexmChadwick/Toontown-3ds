#!/usr/bin/env python3
"""Bake The Brrrgh's falling snow (BRSafeZoneLoader's snowdisk.ptf) to data/zones/3000_snow.ttm.

Upstream emits about 50 snowflake_particle sprites a second on a 50-unit disc 5 above the
camera; each lives 4.5 s, falls about 15 units under gravity 1.5 with a jitter force, grows
from 0.03 to 0.5 times its 3-unit card and fades in (PRALPHAIN). There is no particle system here: this bakes one 64 x 64 x 30 tile of flakes
at that density (two 15-unit falls tall), each a two-sided flake card turned to a random
heading, sized and faded as a random moment of its life. source/world/br_snow.cpp draws 2 x 2 tiles
around the camera, anchored to the world, scrolling down at the mean fall speed.
"""
import argparse
import json
import math
import random
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

TILE = 64.0       # source/world/br_snow.cpp kTile
HEIGHT = 30.0     # two 15-unit falls (kFall)
DENSITY = 225.0 / (math.pi * 50.0 ** 2)  # live flakes per unit^2: 50/s * 4.5 s on a radius-50 disc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--resources', default=str(ROOT / 'ref/resources'))
    ap.add_argument('--data', default=str(ROOT / 'sdmc-out/3ds/toontown3ds/data'))
    args = ap.parse_args()
    from panda3d.core import loadPrcFileData, BamCache, Filename
    BamCache.getGlobalPtr().setActive(False)
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                        'textures-power-2 none\nmodel-cache-dir')
    from direct.showbase.ShowBase import ShowBase
    import extract
    import ttm
    base = ShowBase()
    model = base.loader.loadModel(Filename.fromOsSpecific(str(Path(args.resources) / 'phase_8/models/props/snowflake_particle.bam')),
                                  noCache=True)
    card = model.find('**/p1_2')
    card.clearEffects()  # the BillboardEffect; flakes are turned per card below
    ex = extract.Extractor(max_tex=64)
    ex.add(card)
    (src,) = ex.batches()
    tris = src.verts[src.indices]                      # (n, 9) card corners
    lo, hi = tris[:, :3].min(0), tris[:, :3].max(0)
    centre = (lo + hi) / 2
    rng = random.Random(3000)
    count = int(round(DENSITY * TILE * TILE * 2))     # two falls stacked
    verts = []
    for _ in range(count):
        x, y, z = rng.uniform(0, TILE), rng.uniform(0, TILE), rng.uniform(0, HEIGHT)
        life = rng.random()
        size = 0.03 + 0.47 * life                         # setInitialXScale 0.03 -> setFinalXScale 0.5
        h = rng.uniform(0, 2 * math.pi)
        c, s = math.cos(h), math.sin(h)
        for v in tris:
            px, py, pz = (v[:3] - centre) * size
            nv = v.copy()
            nv[:3] = (x + px * c - py * s, y + px * s + py * c, z + pz)
            nv[8] *= life                                 # PRALPHAIN: alpha = age / lifespan
            verts.append(nv)
    verts = np.array(verts)
    batch = ttm.Batch(src.texture, src.blend, True, 0, 0, verts, np.arange(len(verts)))
    out = Path(args.data) / 'zones'
    out.mkdir(parents=True, exist_ok=True)
    stats = ttm.write(str(out / '3000_snow.ttm'), ex.textures, [batch])
    print(json.dumps(dict(stats, flakes=count)), flush=True)


if __name__ == '__main__':
    main()
