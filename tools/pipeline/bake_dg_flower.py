#!/usr/bin/env python3
"""Bake Daisy Gardens' big flower (DistributedDGFlower) to data/zones/5000_flower.ttm.

DistributedDGFlower.generate loads DG_flower-mod at (1.39, 92.91, 2.0), scale 2.5, spins it
1.25 degrees a frame and raises it to the AI's setHeight; the zone bake never had it. Baked in
model space (unit scale); source/world/dg_flower.cpp places, scales and spins it.
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))


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
    model = base.loader.loadModel(Filename.fromOsSpecific(str(Path(args.resources) / 'phase_8/models/props/DG_flower-mod.bam')),
                                  noCache=True)
    ex = extract.Extractor(max_tex=256)
    ex.add(model)
    out = Path(args.data) / 'zones'
    out.mkdir(parents=True, exist_ok=True)
    stats = ttm.write(str(out / '5000_flower.ttm'), ex.textures, ex.batches())
    if stats['uv_clamped']:
        raise ValueError('UV overflow')
    print(json.dumps(stats), flush=True)


if __name__ == '__main__':
    main()
