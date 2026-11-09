#!/usr/bin/env python3
"""Bake Acorn Acres' waterfall (OZSafeZoneLoader.enter) to data/zones/6000_waterfall.ttm.

Upstream loads golf_waterfall_model as an Actor at the playground's ``waterfall*`` placer and
loops golf_waterfall at play rate 3.5. Its mesh does not move: the animation only slides the
``uvj_WakeWhiteTide1`` joint, which drives the texture through setTexProjector, by +1.0 in v
per 200 frames (24 fps), so the falling water scrolls. The zone bake never had it (a bare rock
cleft over the pond). The mesh is baked in zone space at frame 0 with the projector's frame-0
offset folded into its UVs; source/world/waterfall.cpp scrolls v at the same rate.
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

ZONE = 6000
SCROLL_PER_FRAME = 1.0 / 200   # measured below; the runtime constant must match
PLAY_RATE = 3.5


def bake(resources, out):
    from panda3d.core import (loadPrcFileData, BamCache, Filename, LMatrix4f, NodePath, TexMatrixAttrib,
                              TextureStage, TransformState)
    BamCache.getGlobalPtr().setActive(False)
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                        'textures-power-2 none\nmodel-cache-dir')
    from direct.showbase.ShowBase import ShowBase
    from direct.actor.Actor import Actor
    import assemble
    import dna
    import extract
    import street_catalog
    import ttm
    base = ShowBase()
    store = dna.Storage(base.loader, str(resources))
    _, storages, src = street_catalog.paths(ZONE)
    for s in storages:
        store.load(str(Path(resources) / s))
    tree = dna.parse_file(Path(resources) / src, [])
    geom, _ = assemble.assemble(tree, store, seed=1)
    placer = geom.find('**/waterfall*')
    if placer.isEmpty():
        raise RuntimeError('no waterfall placer')

    def path(rel):
        return Filename.fromOsSpecific(str(Path(resources) / rel))
    actor = Actor(path('phase_6/models/golf/golf_waterfall_model.bam'), {'idle': path('phase_6/models/golf/golf_waterfall.bam')})
    root = NodePath('zone')
    actor.reparentTo(root)
    actor.setPos(placer.getPos())     # OZSafeZoneLoader: setPos(waterfallPlacer.getPos())
    joint = actor.exposeJoint(None, 'modelRoot', 'uvj_WakeWhiteTide1')
    bundle = actor.getPartBundle('modelRoot')
    offsets = []
    for frame in (0, 200):
        actor.pose('idle', frame)
        bundle.forceUpdate()
        offsets.append(joint.getMat(actor).getRow3(3))
    rate = (offsets[1][1] - offsets[0][1]) / 200
    if abs(rate - SCROLL_PER_FRAME) > 1e-4 or abs(offsets[1][0] - offsets[0][0]) > 1e-4:
        raise RuntimeError(f'waterfall projector moved {offsets}: update the runtime scroll')
    actor.pose('idle', 0)
    bundle.forceUpdate()
    # setTexProjector(stage, joint, actor): the texture matrix is the joint's transform in
    # actor space, a translation; its whole part does not change the repeating texture.
    u, v = offsets[0][0] % 1.0, offsets[0][1] % 1.0
    mesh = actor.find('**/mesh_tide1')
    mesh.setTexTransform(TextureStage.getDefault(), TransformState.makePos2d((u, v)))
    ex = extract.Extractor(max_tex=256)
    ex.add(root, animated=True)
    out.mkdir(parents=True, exist_ok=True)
    stats = ttm.write(str(out / f'{ZONE}_waterfall.ttm'), ex.textures, ex.batches())
    if stats['uv_clamped']:
        raise ValueError('UV overflow')
    lo, hi = actor.getTightBounds(root)
    stats['actor_bounds'] = [list(lo), list(hi)]
    stats['uv_offset'] = [u, v]
    stats['scroll_per_second'] = SCROLL_PER_FRAME * 24 * PLAY_RATE
    base.destroy()
    return stats


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--resources', default=str(ROOT / 'ref/resources'))
    ap.add_argument('--data', default=str(ROOT / 'sdmc-out/3ds/toontown3ds/data'))
    args = ap.parse_args()
    print(json.dumps(bake(args.resources, Path(args.data) / 'zones')), flush=True)


if __name__ == '__main__':
    main()
