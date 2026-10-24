#!/usr/bin/env python3
"""Reference answers from Panda's own CollisionTraverser for tools/collision_test.

    .venv/bin/python -I tools/collision_test/panda_reference.py <model.bam> <out.txt> [--n 200]
    .venv/bin/python -I tools/collision_test/panda_reference.py <zone.dna> <out.txt> --storage a.dna b.dna ...

Writes lines (deterministic, seed 1):
    G x y zFrom step found z        ground: CollisionRay from (x, y, zFrom + step) down,
                                    FloorBitmask, highest hit
    C ax ay az bx by bz found t     camera: CollisionSegment a->b, CameraBitmask, first hit
"""
import argparse
import os
import random
import sys

from panda3d.core import (BitMask32, CollisionHandlerQueue, CollisionNode, CollisionRay,
                          CollisionSegment, CollisionTraverser, Filename, loadPrcFileData)

loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning')
from direct.showbase.ShowBase import ShowBase  # noqa: E402

FLOOR, CAMERA = 2, 4  # otp/otpbase/OTPGlobals.py:6-7


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('model')
    ap.add_argument('out')
    ap.add_argument('--n', type=int, default=200)
    ap.add_argument('--storage', nargs='+', default=[])
    args = ap.parse_args()
    base = ShowBase()
    if args.model.lower().endswith('.dna'):
        # Same scene the baker sees (tools/pipeline/bake_collision.py load_scene).
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pipeline'))
        import bake_collision
        ns = argparse.Namespace(input=args.model, storage=args.storage, seed=1, node=None,
                                resources=bake_collision.DEFAULT_RESOURCES)
        model = bake_collision.load_scene(ns, base)
    else:
        model = base.loader.loadModel(Filename.fromOsSpecific(os.path.abspath(args.model)))
    model.reparentTo(base.render)
    lo, hi = model.getTightBounds()

    trav = CollisionTraverser()
    queue = CollisionHandlerQueue()
    ray = CollisionRay()
    seg = CollisionSegment()
    rn = CollisionNode('ref_ray')
    rn.addSolid(ray)
    rn.setFromCollideMask(BitMask32(FLOOR))
    rn.setIntoCollideMask(BitMask32.allOff())
    sn = CollisionNode('ref_seg')
    sn.addSolid(seg)
    sn.setFromCollideMask(BitMask32(CAMERA))
    sn.setIntoCollideMask(BitMask32.allOff())
    rnp = base.render.attachNewNode(rn)
    snp = base.render.attachNewNode(sn)

    def ground(x, y, top):
        trav.clearColliders()
        trav.addCollider(rnp, queue)
        ray.setOrigin(x, y, top)
        ray.setDirection(0, 0, -1)
        trav.traverse(base.render)
        zs = [e.getSurfacePoint(base.render).z for e in queue.entries]
        return max(zs) if zs else None

    def camera(a, b):
        trav.clearColliders()
        trav.addCollider(snp, queue)
        seg.setPointA(*a)
        seg.setPointB(*b)
        trav.traverse(base.render)
        length = sum((b[i] - a[i]) ** 2 for i in range(3)) ** 0.5
        ds = [(e.getSurfacePoint(base.render) - a).length() for e in queue.entries]
        return min(ds) / length if ds else None

    rng = random.Random(1)
    lines = []
    for k in range(2 * args.n):
        x = rng.uniform(lo.x, hi.x) * 0.6
        y = rng.uniform(lo.y, hi.y) * 0.6
        if k < args.n:
            z_from, step = 100.0, 0.0
        else:
            z_from, step = rng.uniform(-8.0, 30.0), 4.0
        z = ground(x, y, z_from + step)
        lines.append(f'G {x:.4f} {y:.4f} {z_from:.4f} {step:.4f} {int(z is not None)} '
                     f'{z if z is not None else 0.0:.5f}')
    for k in range(args.n):
        ax, ay = rng.uniform(-120, 120), rng.uniform(-120, 120)
        gz = ground(ax, ay, 100.0) or 0.0
        a = (ax, ay, gz + 3.0)
        b = (ax + rng.uniform(-30, 30), ay + rng.uniform(-30, 30), gz + rng.uniform(0.0, 15.0))
        t = camera(a, b)
        lines.append(f'C {a[0]:.4f} {a[1]:.4f} {a[2]:.4f} {b[0]:.4f} {b[1]:.4f} {b[2]:.4f} '
                     f'{int(t is not None)} {t if t is not None else 0.0:.6f}')
    with open(args.out, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print(f'wrote {len(lines)} reference queries to {args.out}')


if __name__ == '__main__':
    main()
