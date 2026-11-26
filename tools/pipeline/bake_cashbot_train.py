#!/usr/bin/env python3
"""Bake Cashbot HQ's freight trains (safezone/Train.py) to data/zones/12000_train_*.ttm.

CashbotHQExterior.load runs a Train on each of four tracks; Train loads the locomotive and,
per run, one to nine cars from CarFiles. None of it is in the zone bake. Each model is baked
in its own space; source/world/cashbot_trains.cpp places them on the tracks. A train is up to
ten models, so each model's textures go on its own atlas pages (street_atlas.build_local, as a
zone chunk): a few draws per model instead of one per texture.
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

# Train.LocomotiveFile, then Train.CarFiles in order (the car type index the runtime draws).
MODELS = (('locomotive', 'CashBotLocomotive'), ('car_0', 'CashBotBoxCar'), ('car_1', 'CashBotTankCar'),
          ('car_2', 'CashBotFlatCar'))


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
    import street_atlas
    import ttm
    base = ShowBase()
    out = Path(args.data) / 'zones'
    out.mkdir(parents=True, exist_ok=True)
    for stem, name in MODELS:
        path = Path(args.resources) / f'phase_10/models/cogHQ/{name}.bam'
        model = base.loader.loadModel(Filename.fromOsSpecific(str(path)), noCache=True)
        ex = street_atlas.Extractor()
        ex.add(model)
        (textures,), (batches,), _ = street_atlas.build_local(ex.sources, [ex.batches()], density=0.5, gutter=8,
                                                              standalone_size=128, mip_levels=4)
        batches = [street_atlas.rebase_large_uvs(b, textures) for b in batches]
        stats = ttm.write(str(out / f'12000_train_{stem}.ttm'), textures, batches)
        if stats['uv_clamped']:
            raise ValueError(f'{name}: UV overflow')
        print(stem, json.dumps(stats), flush=True)


if __name__ == '__main__':
    main()
