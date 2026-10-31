#!/usr/bin/env python3
"""Bake camera-relative zone skies (no window). Outputs stay on ignored SD data.

python -I tools/pipeline/bake_sky.py --resources ref/resources --out sdmc-out/3ds/toontown3ds/data/sky
"""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from panda3d.core import Filename, TransparencyAttrib, loadPrcFileData
loadPrcFileData('', 'window-type none\naudio-library-name null\ntextures-power-2 none')
from direct.showbase.ShowBase import ShowBase
import extract
import ttm
import street_atlas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--resources', default='ref/resources')
    ap.add_argument('--out', default='sdmc-out/3ds/toontown3ds/data/sky')
    args = ap.parse_args()
    base = ShowBase()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    stats = {}
    for name, bam, pattern in [('tt', 'TT_sky', '**/Sky'), ('tt_cloud1', 'TT_sky', '**/cloud1'),
                               ('tt_cloud2', 'TT_sky', '**/cloud2'), ('br', 'BR_sky', None)]:
        root = base.loader.loadModel(Filename.fromOsSpecific(str(Path(args.resources).resolve()/
                                                   'phase_3.5/models/props'/f'{bam}.bam')))
        if root.isEmpty():
            raise ValueError(f'missing {bam}')
        node = root.find(pattern) if pattern else root
        if node.isEmpty():
            raise ValueError(f'missing {pattern}')
        node.setTransparency(TransparencyAttrib.M_dual, 1)
        if name in ('tt', 'br'):
            ex = street_atlas.Extractor()
            ex.add(node)
            textures, chunks = street_atlas.build(ex.sources, [ex.batches()], tile=112,
                                                  gutter=8, mip_levels=4)
            batches = chunks[0]
        else:
            ex = extract.Extractor(max_tex=256)
            ex.add(node)
            textures, batches = ex.textures, ex.batches()
        stats[name] = ttm.write(out/f'{name}.ttm', textures, batches)
        # Every sky part must have actual textured triangles. Clouds rotate separately.
        assert stats[name]['triangles'] > 0 and stats[name]['texture_bytes'] > 0
        root.removeNode()
    (out/'budgets.json').write_text(json.dumps(stats, indent=2)+'\n')
    print(json.dumps(stats, indent=2))


if __name__ == '__main__':
    main()
