"""stock_detail.py <views_dir> [<views_dir> ...] [--out <dir>]

Texture detail of the near fidelity views against the stock game. Renders each near view
(compare_views.DETAIL_R11) from the original assets, assembled from the zone DNA as upstream does
(dna.py / assemble.py), with the game camera (52 degree 4:3 FOV, so 40.2 degrees vertically, at
400x240), and prints the Laplacian variance compare_views.py scores, for the stock render and for
each views_dir's shot_view_<view>_top.png. Exit 1 when a capture has less detail than stock.

compare_views.py asks for 1.3x its round-11 baseline. This shows what the source textures allow
at the same camera: a capture cannot hold more real detail than the original assets have.

    TT_RESOURCES=<ref/resources> xvfb-run -a .venv/bin/python -I tools/fidelity/stock_detail.py \\
        <gate>/vold/views <gate>/vnew/views --out <dir>
"""
import math
import os
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

# main.cpp kViews: name, zone, eye, target.
VIEWS = [('ttc_near', 2000, (-79.2, -78.1, 5.5), (-82.8, -91.6, 5.0)),
         ('street_near', 2100, (-123.0, -115.0, 5.0), (-137.0, -115.0, 6.0)),
         ('dd_near', 1000, (-108.0, -28.0, 11.0), (-122.0, -28.0, 10.0))]


def laplace_var(path):
    """scipy.ndimage.laplace(grey).var(), as compare_views.py scores detail (reflect edges)."""
    a = np.asarray(Image.open(path).convert('RGB')).astype(float).mean(2)
    p = np.pad(a, 1, mode='symmetric')
    lap = p[:-2, 1:-1] + p[2:, 1:-1] + p[1:-1, :-2] + p[1:-1, 2:] - 4 * a
    return float(lap.var())


def main():
    args = sys.argv[1:]
    out = Path('/tmp/stock_detail')
    if '--out' in args:
        i = args.index('--out')
        out = Path(args[i + 1])
        del args[i:i + 2]
    out.mkdir(parents=True, exist_ok=True)
    base = ShowBase()
    base.setBackgroundColor(0.55, 0.75, 1, 1)
    base.camLens.setFov(2 * math.degrees(math.atan(math.tan(math.radians(20.1)) * 400 / 240)), 40.2)
    base.camLens.setNearFar(1, 1500)
    bad = 0
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
        stock = laplace_var(ref)
        line = f'{name}: stock {stock:.0f}'
        for d in args:
            v = laplace_var(Path(d) / f'shot_view_{name}_top.png')
            bad += v < stock
            line += f'; {d} {v:.0f} ({v / stock:.2f}x stock)'
        print(line, flush=True)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
