"""stock_detail.py <views_dir> [<views_dir> ...] [--out <dir>] [--fovy <degrees>]

Texture detail of the near fidelity views against the stock game. Renders each near view (main.cpp
kViews rows with r11Lens, read by near_views.py) from the original assets, assembled from the zone
DNA as upstream does (dna.py / assemble.py), at 400x240 with main.cpp's eye, target and near lens
(kViewFovYDeg, 55 degrees vertically), and prints the masked detail compare_views.py scores
(near_views.detail) for the stock render and for each views_dir's shot_view_<view>_top.png.
Exit 1 when a capture has less detail than stock.

Writes <out>/stock_<view>.png and <out>/stock_meta.json (lens, size, each view's zone / eye /
target / PNG sha256, TT_RESOURCES and its git commit, render_sha256 of near_views.RENDER_FILES).
compare_views.py refuses a stock dir whose meta is missing, whose lens is not main.cpp's near lens,
whose poses differ from kViews, whose PNGs do not hash to the meta, or whose commit, render hash or
PNG hashes differ from its STOCK_PIN (the render is byte-deterministic). --fovy is for experiments only: anything but kViewFovYDeg fails there.

    TT_RESOURCES=<ref/resources> xvfb-run -a .venv/bin/python -I tools/fidelity/stock_detail.py \\
        <gate>/vold/views <gate>/vnew/views --out <dir>
"""
import hashlib
import json
import math
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/pipeline'))
RES = Path(os.environ.get('TT_RESOURCES', str(ROOT / 'ref/resources')))
from panda3d.core import loadPrcFileData
loadPrcFileData('', 'window-type offscreen\naudio-library-name null\nwin-size 400 240\n'
                f'model-path {RES}\nnotify-level error\n')
from direct.showbase.ShowBase import ShowBase
from panda3d.core import SamplerState
import numpy as np
from PIL import Image
import dna
import assemble
import street_catalog

sys.path.insert(0, str(Path(__file__).resolve().parent))
import near_views

# main.cpp kViews rows shot with the near lens: name, zone, eye, target.
VIEWS = [(n, v['zone'], tuple(v['eye']), tuple(v['target'])) for n, v in near_views.views().items() if v['r11_lens']]


def laplace_var(path):
    """near_views.detail of the image, as compare_views.py scores it."""
    return near_views.detail(np.asarray(Image.open(path).convert('RGB')).astype(float).mean(2))


def main():
    args = sys.argv[1:]
    out = Path('/tmp/stock_detail')
    if '--out' in args:
        i = args.index('--out')
        out = Path(args[i + 1])
        del args[i:i + 2]
    fovy = near_views.fovy()
    if '--fovy' in args:
        i = args.index('--fovy')
        fovy = float(args[i + 1])
        del args[i:i + 2]
    out.mkdir(parents=True, exist_ok=True)
    base = ShowBase()
    base.setBackgroundColor(0.55, 0.75, 1, 1)
    half = math.radians(fovy / 2)
    base.camLens.setFov(2 * math.degrees(math.atan(math.tan(half) * 400 / 240)), fovy)
    base.camLens.setNearFar(1, 1500)
    bad = 0
    commit = subprocess.run(['git', '-C', str(RES), 'rev-parse', 'HEAD'], capture_output=True,
                            text=True).stdout.strip() or None
    meta = {'fovy': fovy, 'size': [400, 240], 'tt_resources': str(RES), 'tt_resources_commit': commit,
            'render_sha256': near_views.render_sha256(ROOT), 'views': {}}
    for name, zone, eye, target in VIEWS:
        _, storages, src = street_catalog.paths(zone)
        store = dna.Storage(base.loader, str(RES))
        for s in storages:
            store.load(str(RES / s))
        scene, _ = assemble.assemble(dna.parse_file(RES / src, []), store, seed=1)
        scene.reparentTo(base.render)
        for t in scene.findAllTextures():
            t.setMinfilter(SamplerState.FT_linear_mipmap_linear)
        base.camera.setPos(*eye)
        base.camera.lookAt(*target)
        base.graphicsEngine.renderFrame()
        base.graphicsEngine.renderFrame()
        ref = out / f'stock_{name}.png'
        base.win.saveScreenshot(str(ref))
        scene.removeNode()
        meta['views'][name] = {'zone': zone, 'eye': list(eye), 'target': list(target), 'png': ref.name,
                               'sha256': hashlib.sha256(ref.read_bytes()).hexdigest()}
        stock = laplace_var(ref)
        line = f'{name}: stock {stock:.0f}'
        for d in args:
            v = laplace_var(Path(d) / f'shot_view_{name}_top.png')
            bad += v < stock
            line += f'; {d} {v:.0f} ({v / stock:.2f}x stock)'
        print(line, flush=True)
    (out / 'stock_meta.json').write_text(json.dumps(meta, indent=1) + '\n')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
