#!/usr/bin/env python3
"""Ground truth for source/world/suit_path.cpp from the real libtoontown.

Needs the Open Toontown Panda3D fork that tools/server/setup.sh builds (it has
panda3d.toontown: DNAStorage.getSuitPath and SuitLegList); the stock wheel in .venv does
not. Run it with that interpreter:

    tools/server/.venv/bin/python -I tools/suit_test/reference.py OUT.txt [--queries N]

For every street it loads the DNA exactly like the AI (loadDNAFileAI, discoverContinuity),
asks getSuitPath for random (start, end, minLen, maxLen) queries and builds the SuitLegList
with SuitBase.makeLegList's arguments. Output, one record per query:

    Q zone start end minLen maxLen n p0 p1 ... p(n-1)      (n = 0: getSuitPath returned None)
    L type startTime legTime zone pointA pointB block       (one per leg, only when n > 0)
    W type fraction x y z                                    (SuitLeg.getPosAtTime, walk legs)
    E

Point numbers are DNA indices. Deterministic (seeded).
"""
import argparse
import os
import random
import sys

from panda3d.core import getModelPath
from panda3d.toontown import DNAStorage, DNASuitPoint, SuitLegList, loadDNAFileAI

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RES = os.environ.get('TT_RESOURCES', os.path.join(ROOT, 'ref', 'resources'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'pipeline'))
import street_catalog  # noqa: E402  (pure python, no panda needed)

STREETS = (2100, 2200, 2300, 1100, 1200, 1300, 3100, 3200, 3300, 4100, 4200, 4300,
           5100, 5200, 5300, 9100, 9200)
LIMITS = ((1, 300), (3, 300), (10, 300), (40, 300), (5, 30), (40, 60), (100, 300))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--queries', type=int, default=400)
    ap.add_argument('--zones', default=','.join(map(str, STREETS)))
    args = ap.parse_args()
    getModelPath().appendDirectory(RES)
    rng = random.Random(1234)
    with open(args.out, 'w') as out:
        for zone in (int(z) for z in args.zones.split(',')):
            dna_path = os.path.join(RES, street_catalog.paths(zone)[2])
            store = DNAStorage()
            loadDNAFileAI(store, dna_path)
            store.discoverContinuity()
            pts = [store.getSuitPointAtIndex(i) for i in range(store.getNumSuitPoints())]
            ids = [p.getIndex() for p in pts]
            found = 0
            for _ in range(args.queries):
                s, e = rng.choice(ids), rng.choice(ids)
                lo, hi = rng.choice(LIMITS)
                path = store.getSuitPath(store.getSuitPointWithIndex(s), store.getSuitPointWithIndex(e), lo, hi)
                if path is None or path.getNumPoints() < 2:
                    out.write(f'Q {zone} {s} {e} {lo} {hi} 0\nE\n')
                    continue
                found += 1
                n = path.getNumPoints()
                out.write(f'Q {zone} {s} {e} {lo} {hi} {n} ' +
                          ' '.join(str(path.getPointIndex(i)) for i in range(n)) + '\n')
                legs = SuitLegList(path, store, 4.8, 6.5, 6.5, 2.0, 2.5, 2.5)
                for i in range(legs.getNumLegs()):
                    leg = legs.getLeg(i)
                    out.write(f'L {leg.getType()} {leg.getStartTime():.6f} {leg.getLegTime():.6f} '
                              f'{leg.getZoneId()} {leg.getPointA()} {leg.getPointB()} {leg.getBlockNumber()}\n')
                    if leg.getType() in (0, 1, 2) and leg.getLegTime() > 0:
                        for frac in (0.0, 0.37, 1.0):
                            p = leg.getPosAtTime(leg.getLegTime() * frac)
                            out.write(f'W {leg.getType()} {frac} {p[0]:.5f} {p[1]:.5f} {p[2]:.5f}\n')
                out.write('E\n')
            print(f'{zone}: {found}/{args.queries} queries found a path')


if __name__ == '__main__':
    main()
