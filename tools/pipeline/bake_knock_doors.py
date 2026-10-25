#!/usr/bin/env python3
"""Knock-knock door trigger spheres for every baked exterior zone → data/zones/<zone>.kkd.

DistributedKnockKnockDoor (one per toon building, propId = block) starts its joke when the
local toon enters the collision sphere 'KnockKnockDoorSphere_<block>'. libtoontown's
DNAFlatDoor makes that sphere on every flat_door of the block's flat buildings; it is not in
any .bam and our own assembler does not reproduce it. So this tool loads each zone with the
real libtoontown DNA loader and records the spheres.

Needs the Open Toontown Panda3D fork that tools/server/setup.sh builds (panda3d.toontown);
the stock wheel in .venv does not have it. Run:

    tools/server/.venv/bin/python -I tools/pipeline/bake_knock_doors.py \\
        --out sdmc-out/3ds/toontown3ds/data/zones

Output, one text line per sphere, sorted (deterministic):

    KKD1
    <propId> <x> <y> <z> <radius>

Positions are in zone (render) space, like the .pts anchors.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from panda3d.core import NodePath, getModelPath, loadPrcFileData  # noqa: E402

loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level-gobj error')

from direct.showbase.ShowBase import ShowBase  # noqa: E402
from panda3d.toontown import DNAStorage, loadDNAFile  # noqa: E402

import street_catalog  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RESOURCES = os.environ.get('TT_RESOURCES', os.path.join(ROOT, 'ref', 'resources'))
SPHERE_PREFIX = 'KnockKnockDoorSphere_'


def zone_spheres(zone):
    _, storages, dna = street_catalog.paths(zone)
    store = DNAStorage()
    for path in storages:
        loadDNAFile(store, path)
    root = NodePath(loadDNAFile(store, dna))
    spheres = []
    for np_ in root.findAllMatches('**/' + SPHERE_PREFIX + '*'):
        prop_id = int(np_.getName()[len(SPHERE_PREFIX):])
        solid = np_.node().getSolid(0)
        mat = np_.getMat(root)
        center = mat.xformPoint(solid.getCenter())
        radius = solid.getRadius() * mat.getRow3(0).length()
        spheres.append((prop_id, center[0], center[1], center[2], radius))
    root.removeNode()
    return sorted(spheres)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--zone', type=int, action='append', help='default: every baked exterior zone')
    args = ap.parse_args()
    ShowBase()
    getModelPath().prependDirectory(RESOURCES)
    os.makedirs(args.out, exist_ok=True)
    total = 0
    for zone in args.zone or street_catalog.ZONES:
        spheres = zone_spheres(zone)
        lines = ['KKD1'] + [f'{p} {x:.3f} {y:.3f} {z:.3f} {r:.3f}' for p, x, y, z, r in spheres]
        with open(os.path.join(args.out, f'{zone}.kkd'), 'w') as f:
            f.write('\n'.join(lines) + '\n')
        print(f'{zone}: {len(spheres)} knock-knock door spheres')
        total += len(spheres)
    print(f'total {total}')


if __name__ == '__main__':
    main()
