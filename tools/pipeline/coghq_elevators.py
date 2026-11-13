#!/usr/bin/env python3
"""Append the Cog HQ facility elevator anchors to the baked `.pts` sidecars.

Mint and DA office elevators stand at `elevator_origin_<n>` nodes of the HQ model
(DistributedMintElevatorExt.setupElevator / DistributedLawOfficeElevatorExt.setEntranceId);
the factory's two are hardcoded (DistributedFactoryElevatorExt.setEntranceId) and the boss
elevator (the VP/CFO/CJ/CEO lobbies) uses `elevator_origin` style nodes too when present.
Existing `elevator_origin_*` lines are replaced, everything else is kept, so reruns are
idempotent. bake_coghq.py calls this for every zone it writes, so a re-bake keeps them.

    .venv/bin/python -I tools/pipeline/coghq_elevators.py [--zones sdmc-out/3ds/toontown3ds/data/zones]
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)

from panda3d.core import Filename, NodePath, loadPrcFileData  # noqa: E402

import coghq_catalog  # noqa: E402

# DistributedFactoryElevatorExt.setEntranceId: entrance -> (x, y, z, h).
FACTORY = {11200: [(62.74, -85.31, 0.0, 2.0), (-162.25, 26.43, 0.0, 269.0)]}


def anchors(zone, info, resources, loader):
    """(name, (x, y, z, h)) for every facility elevator of a Cog HQ zone."""
    if zone in FACTORY:
        return [('elevator_origin_%d' % i, p) for i, p in enumerate(FACTORY[zone])]
    found = []
    model = loader.loadModel(Filename.fromOsSpecific(os.path.join(resources, info['model'])))
    root = NodePath('root')
    model.reparentTo(root)
    for np in model.findAllMatches('**/elevator_origin*'):
        name = np.getName()
        if name.startswith('elevator_origin'):
            p = np.getPos(root)
            found.append((name if name != 'elevator_origin' else 'elevator_origin_0',
                          (p.x, p.y, p.z, np.getH(root))))
    model.removeNode()
    return found


def write(pts, found):
    """Replace the elevator_origin_* lines of a .pts sidecar with `found`."""
    keep = [l for l in open(pts) if not l.startswith('elevator_origin')]
    with open(pts, 'w') as f:
        f.writelines(keep)
        for name, (x, y, z, h) in sorted(found):
            f.write('%s %.3f %.3f %.3f %.2f\n' % (name, x, y, z, h))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zones', default=os.path.join(ROOT, 'sdmc-out', '3ds', 'toontown3ds', 'data', 'zones'))
    ap.add_argument('--resources', default=os.path.join(ROOT, 'ref', 'resources'))
    args = ap.parse_args()
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n')
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase(windowType='none')
    for zone, info in sorted(coghq_catalog.CATALOG.items()):
        pts = os.path.join(args.zones, '%d.pts' % zone)
        if not os.path.exists(pts):
            continue
        found = anchors(zone, info, args.resources, base.loader)
        write(pts, found)
        print('%d: %d elevator anchors' % (zone, len(found)))

if __name__ == '__main__':
    main()
