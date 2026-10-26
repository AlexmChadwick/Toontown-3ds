"""Bake the original RaceGag box, banana and anvil; run Python -I -B."""
import hashlib
import json
import os
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
# The main checkout has ref/<submodule>; linked worktrees reach it through ref/ref.
REF = ROOT / 'ref' if (ROOT / 'ref/open-toontown/toontown').is_dir() else ROOT / 'ref/ref'
sys.path.insert(0, str(ROOT / 'tools/pipeline'))
os.environ['TT_ETC1_THREADS'] = '3'
from panda3d.core import BamCache, NodePath, loadPrcFileData
loadPrcFileData('', 'window-type none\naudio-library-name null\nmodel-cache-dir\nnotify-level error\nmodel-path ' + str(REF / 'resources'))
from direct.showbase.ShowBase import ShowBase
import extract
import ttm
base = ShowBase()
BamCache.getGlobalPtr().setActive(False)
out = ROOT / 'sdmc-out/3ds/toontown3ds/data/racing'
report = {}
for name, original in (('qbox','phase_6/models/karting/qbox'),('banana','phase_5/models/props/banana-peel-mod'),('anvil','phase_4/models/props/anvil-mod'),('creampie','phase_3.5/models/props/tart')):
    model = base.loader.loadModel(str(REF / 'resources' / (original + '.bam')))
    if name == 'creampie':
        # BattleProps.Variants/Splats: creampie is the tart geometry, .7 scale and CreampieColor.
        root = NodePath('creampie')
        model.reparentTo(root)
        model.setScale(.7)
        model.setColor(250/255, 241/255, 24/255, 1)
        model = root
    exporter = extract.Extractor(max_tex=128)
    exporter.add(model)
    stats = ttm.write(str(out / (name + '.ttm')), exporter.textures, exporter.batches())
    assert stats['geometry_bytes'] <= 128*1024 and stats['texture_bytes'] <= 256*1024
    stats['sha256'] = hashlib.sha256((out / (name + '.ttm')).read_bytes()).hexdigest()
    report[name] = stats
    model.removeNode()
(out / 'props.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
print(json.dumps(report, indent=2))
