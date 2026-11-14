#!/usr/bin/env python3
"""Bake the playground butterflies (DistributedButterfly) to data/butterfly/.

DistributedButterfly.generate loads SZ_butterfly-mod with the flutter / glide / land anims,
keeps one of six wing sets (doId % 6) and colours it with setColor. Each wing set is baked as
its own model, wing_<n>.ttm (body + that wing, the wing on tint slot 1 over white so the
runtime tint is the setColor colour), with vertex-animation tracks wing_<n>_flutter.tta and
wing_<n>_land.tta at 12 fps (tta.py). glide is only blended in by a per-butterfly weight
while flying; the runtime flies on flutter alone.
"""
import argparse
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

WINGS = ('wings_1', 'wings_2', 'wings_3', 'wings_4', 'wings_5', 'wings_6')
CLIPS = ('flutter', 'land')
FPS = 12.0


def bake(resources, out):
    import numpy as np
    from panda3d.core import loadPrcFileData, BamCache, Filename
    BamCache.getGlobalPtr().setActive(False)
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                        'textures-power-2 none\nmodel-cache-dir')
    from direct.showbase.ShowBase import ShowBase
    from direct.actor.Actor import Actor
    import extract
    import tta
    import ttm
    base = ShowBase()
    props = Path(resources) / 'phase_4/models/props'
    out.mkdir(parents=True, exist_ok=True)
    report = {}

    def capture(actor, encode):
        actor.update(force=True)
        ex = extract.Extractor(max_tex=128, encode_textures=encode, tint_rules=[('wings_*', 1)])
        ex.add(actor, animated=True)
        return ex, ex.batches()

    for n, wing in enumerate(WINGS):
        actor = Actor(Filename.fromOsSpecific(str(props / 'SZ_butterfly-mod.bam')),
                      {c: Filename.fromOsSpecific(str(props / f'SZ_butterfly-{c}.bam')) for c in CLIPS})
        node = actor.getGeomNode()
        for other in WINGS:
            if other != wing:
                node.find('**/' + other).removeNode()
        node.find('**/' + wing).setColor(1, 1, 1, 1)
        actor.pose('land', 0)
        ex, ref = capture(actor, True)
        stats = ttm.write(str(out / f'wing_{n}.ttm'), ex.textures, ref)
        report[f'wing_{n}'] = stats
        for clip in CLIPS:
            count = actor.getNumFrames(clip)
            rate = actor.getFrameRate(clip)
            frames, cache = [], {}

            def at(i):
                if i not in cache:
                    actor.pose(clip, i)
                    _, bs = capture(actor, False)
                    if [len(b.verts) for b in bs] != [len(b.verts) for b in ref]:
                        raise RuntimeError(f'{wing} {clip}: vertex layout changed')
                    cache[i] = np.concatenate([b.verts[:, :3] for b in bs])
                return cache[i]
            for i in range(max(1, math.ceil(count / rate * FPS - 1e-8))):
                src = i * rate / FPS
                a = min(int(src), count - 1)
                b = min(a + 1, count - 1)
                frames.append(at(a) * (1 - (src - a)) + at(b) * (src - a))
            report[f'wing_{n}_{clip}'] = tta.write(str(out / f'wing_{n}_{clip}.tta'), np.array(frames), FPS)
        actor.cleanup()
        actor.removeNode()
    base.destroy()
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--resources', default=str(ROOT / 'ref/resources'))
    ap.add_argument('--data', default=str(ROOT / 'sdmc-out/3ds/toontown3ds/data'))
    args = ap.parse_args()
    print(json.dumps(bake(args.resources, Path(args.data) / 'butterfly')), flush=True)


if __name__ == '__main__':
    main()
