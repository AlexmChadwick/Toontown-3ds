"""Near-view texture detail, shared by compare_views.py and stock_detail.py (numpy only).

views() -> {name: {'zone', 'eye', 'target', 'toon', 'r11_lens'}} read from source/main.cpp kViews, and
fovy() -> kViewFovYDeg, the lens of the views whose r11_lens is true (the near views), so the gate
and the stock render cannot drift from the game's cameras.

detail(grey) is the Laplacian variance (scipy.ndimage.laplace, reflect edges) over the frame minus
MASK_X: the 3DS frames have our toon and its nametag at x 165-245 and the stock render has
neither, so both are scored without that column (QA-20261119-74).
"""
from pathlib import Path
import re

import numpy as np

MASK_X = (165, 245)


def detail(grey):
    p = np.pad(grey, 1, mode='symmetric')
    lap = p[:-2, 1:-1] + p[2:, 1:-1] + p[1:-1, :-2] + p[1:-1, 2:] - 4 * grey
    keep = np.ones(grey.shape, bool)
    keep[:, MASK_X[0]:MASK_X[1]] = False
    return float(lap[keep].var())

MAIN = Path(__file__).resolve().parents[2] / 'source/main.cpp'
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
