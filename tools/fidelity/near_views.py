"""Near-view texture detail, shared by compare_views.py and stock_detail.py (numpy; scipy only
for the ringing guards).

views() -> {name: {'zone', 'eye', 'target', 'toon', 'r11_lens'}} read from source/main.cpp kViews, and
fovy() -> kViewFovYDeg, the lens of the views whose r11_lens is true (the near views), so the gate
and the stock render cannot drift from the game's cameras.

detail(grey) is the Laplacian variance (scipy.ndimage.laplace, reflect edges) over the frame minus
MASK_X: the 3DS frames have our toon and its nametag at x 165-245 and the stock render has
neither, so both are scored without that column (QA-20261119-74).

Laplacian variance also rises with sharpening halos, so QA-20261119-84 added three guards, ported
from QA's halo.py / overshoot.py (same mask):
- off_edge(grey, edges): Laplacian variance over pixels at least 3 px from every strong edge of the
  frames compared (edge_map: top 10% Sobel magnitude after a 1 px Gaussian);
- highpass(grey): std of grey minus its 1.5 px Gaussian (high-pass amplitude);
- overshoot(grey): mean fraction of the step height a clean horizontal step edge rises above its
  high plateau.
"""
from pathlib import Path
import re

import numpy as np

MASK_X = (165, 245)


def detail(grey, cell=None):
    """Laplacian variance outside MASK_X and, when given, the (x0, y0, x1, y1) `cell`."""
    return float(_laplace(grey)[_keep(grey.shape, cell)].var())


def _laplace(grey):
    p = np.pad(grey, 1, mode='symmetric')
    return p[:-2, 1:-1] + p[2:, 1:-1] + p[1:-1, :-2] + p[1:-1, 2:] - 4 * grey


def _keep(shape, cell=None):
    keep = np.ones(shape, bool)
    keep[:, MASK_X[0]:MASK_X[1]] = False
    if cell:
        x0, y0, x1, y1 = cell
        keep[y0:y1, x0:x1] = False
    return keep


def edge_map(grey, q=90):
    from scipy import ndimage
    s = ndimage.gaussian_filter(grey, 1.0)
    mag = np.hypot(ndimage.sobel(s, 0), ndimage.sobel(s, 1))
    return mag > np.percentile(mag[_keep(grey.shape)], q)


def off_edge(grey, edges, exclude=3, cell=None):
    """Laplacian variance at least `exclude` px from `edges` (the union of edge_map over the
    frames compared), outside the mask (and `cell`, as detail)."""
    from scipy import ndimage
    far = _keep(grey.shape, cell) & ~ndimage.binary_dilation(edges, iterations=exclude)
    return float(_laplace(grey)[far].var())


def highpass(grey, sigma=1.5):
    from scipy import ndimage
    return float((grey - ndimage.gaussian_filter(grey, sigma))[_keep(grey.shape)].std())


def overshoot(grey, thr=25, k=4):
    """Mean overshoot above the high plateau of clean horizontal step edges, as a fraction of the
    step (QA overshoot.py: steps >= 30 grey levels, plateaus flatter than 0.45 of the step)."""
    from scipy import ndimage
    d = np.diff(ndimage.gaussian_filter1d(grey, 1.0, axis=1), axis=1)
    width = grey.shape[1]
    out = []
    for y in range(grey.shape[0]):
        row = grey[y]
        for x in range(k + 1, width - k - 2):
            if MASK_X[0] - k <= x <= MASK_X[1] + k:
                continue
            v = d[y, x]
            if abs(v) <= thr or abs(v) < abs(d[y, x - 1]) or abs(v) < abs(d[y, x + 1]):
                continue
            lo, hi = row[x - k:x - 2], row[x + 3:x + k + 1]
            step = hi.mean() - lo.mean()
            if abs(step) < 30 or np.ptp(lo) > .45 * abs(step) or np.ptp(hi) > .45 * abs(step):
                continue
            prof = (row[x - k:x + k + 2] - lo.mean()) * np.sign(step) / abs(step)
            out.append(max(prof.max() - 1, 0.0))
    return float(np.mean(out)) if out else 0.0

MAIN = Path(__file__).resolve().parents[2] / 'source/main.cpp'
# What the stock render is made from: stock_detail.py records the sha256 of these files and
# compare_views.py rechecks it against the tree.
RENDER_FILES = ('tools/fidelity/stock_detail.py', 'tools/pipeline/dna.py', 'tools/pipeline/assemble.py',
                'tools/pipeline/street_catalog.py')


def render_sha256(root=MAIN.parents[1]):
    import hashlib
    h = hashlib.sha256()
    for name in RENDER_FILES:
        h.update(name.encode() + b'\0' + (Path(root) / name).read_bytes())
    return h.hexdigest()
_NUM = r'(-?[\d.]+)f?'
_VEC = r'\{\s*' + r',\s*'.join([_NUM] * 3) + r'\s*\}'
_ROW = re.compile(r'\{\s*"(\w+)",\s*(\d+),\s*' + _VEC + r',\s*' + _VEC + r',\s*\{([^}]*)\},\s*(true|false)\s*\}')


def views(main=MAIN):
    text = Path(main).read_text(errors='ignore')
    table = text[text.index('kViews[] = {'):]
    table = table[:table.index('};')]
    out = {}
    for m in _ROW.finditer(table):
        g = m.groups()
        out[g[0]] = {'zone': int(g[1]), 'eye': [float(v) for v in g[2:5]],
                     'target': [float(v) for v in g[5:8]],
                     'toon': [float(v.strip().rstrip('f')) for v in g[8].split(',')],
                     'r11_lens': g[9] == 'true'}
    if not out:
        raise ValueError(f'no kViews rows in {main}')
    return out


def fovy(main=MAIN):
    m = re.search(r'kViewFovYDeg\s*=\s*' + _NUM, Path(main).read_text(errors='ignore'))
    if not m:
        raise ValueError(f'no kViewFovYDeg in {main}')
    return float(m.group(1))
