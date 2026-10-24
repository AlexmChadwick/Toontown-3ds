#!/usr/bin/env python3
"""Append the Cog HQ facility elevator anchors to the baked `.pts` sidecars.

Mint and DA office elevators stand at `elevator_origin_<n>` nodes of the HQ model
(DistributedMintElevatorExt.setupElevator / DistributedLawOfficeElevatorExt.setEntranceId);
the factory's two are hardcoded (DistributedFactoryElevatorExt.setEntranceId) and the boss
elevator (the VP/CFO/CJ/CEO lobbies) uses `elevator_origin` style nodes too when present.
Existing `elevator_origin_*` lines are replaced, everything else is kept, so reruns are
idempotent.

    .venv/bin/python -I tools/pipeline/coghq_elevators.py [--zones sdmc-out/3ds/toontown3ds/data/zones]
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)

from panda3d.core import Filename, NodePath, loadPrcFileData  # noqa: E402

loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n')
from direct.showbase.ShowBase import ShowBase  # noqa: E402

import coghq_catalog  # noqa: E402

# DistributedFactoryElevatorExt.setEntranceId: entrance -> (x, y, z, h).
FACTORY = {11200: [(62.74, -85.31, 0.0, 2.0), (-162.25, 26.43, 0.0, 269.0)]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zones', default=os.path.join(ROOT, 'sdmc-out', '3ds', 'toontown3ds', 'data', 'zones'))
    ap.add_argument('--resources', default=os.path.join(ROOT, 'ref', 'resources'))
    args = ap.parse_args()
    base = ShowBase(windowType='none')
    zones = coghq_catalog.CATALOG
    for zone, info in sorted(zones.items()):
        pts = os.path.join(args.zones, '%d.pts' % zone)
        if not os.path.exists(pts):
            continue
        anchors = []
        if zone in FACTORY:
            anchors = [('elevator_origin_%d' % i, p) for i, p in enumerate(FACTORY[zone])]
        else:
            model = base.loader.loadModel(Filename.fromOsSpecific(os.path.join(args.resources, info['model'])))
            root = NodePath('root')
            model.reparentTo(root)
            for np in model.findAllMatches('**/elevator_origin*'):
                name = np.getName()
                if name.startswith('elevator_origin'):
                    p = np.getPos(root)
                    anchors.append((name if name != 'elevator_origin' else 'elevator_origin_0',
                                    (p.x, p.y, p.z, np.getH(root))))
            model.removeNode()
        keep = [l for l in open(pts) if not l.startswith('elevator_origin')]
        with open(pts, 'w') as f:
            f.writelines(keep)
            for name, (x, y, z, h) in sorted(anchors):
                f.write('%s %.3f %.3f %.3f %.2f\n' % (name, x, y, z, h))
        print('%d: %d elevator anchors' % (zone, len(anchors)))


if __name__ == '__main__':
    main()
