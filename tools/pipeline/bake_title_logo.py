#!/usr/bin/env python3
"""Bake the Toontown Online logo for the title screen into romfs/baked/title_logo.ttm.

Upstream draws phase_3/maps/toontown-logo (256x256 jpg + _a.rgb alpha) on the `logo` card
of phase_3/models/gui/progress-background (1.708 x 0.6 units), so the square texture is
shown at about 2.85:1. The title screen shows it at 205x72 pixels; the texture is resized
to 256x128 (no visible loss at that size, half the memory) and kept RGBA8 because the
outline's soft alpha bands badly in RGBA4. One texture, one placeholder quad, 128 KB.

    .venv/bin/python -I tools/pipeline/bake_title_logo.py
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402
from panda3d.core import PNMImage  # noqa: E402

import ttm  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
MAPS = ROOT / 'ref' / 'resources' / 'phase_3' / 'maps'
OUT = ROOT / 'romfs' / 'baked' / 'title_logo.ttm'
W, H = 256, 128


def main():
    img = PNMImage(str(MAPS / 'toontown-logo.jpg'))
    alpha = PNMImage(str(MAPS / 'toontown-logo_a.rgb'))
    img.addAlpha()
    for y in range(img.getYSize()):
        for x in range(img.getXSize()):
            img.setAlpha(x, y, alpha.getGray(x, y))
    small = PNMImage(W, H, 4)
    small.gaussianFilterFrom(1.0, img)
    px = np.zeros((H, W, 4), dtype=np.uint8)
    for y in range(H):
        for x in range(W):
            c = small.getXelA(x, y)
            px[y, x] = [round(c[0] * 255), round(c[1] * 255), round(c[2] * 255), round(c[3] * 255)]
    texture = ttm.Texture('title_logo', px, ttm.GPU_RGBA8, ttm.GPU_CLAMP_TO_EDGE, ttm.GPU_CLAMP_TO_EDGE,
                          mipmaps=False)
    quad = np.array([[-1, 0, -1, 0, 0, 1, 1, 1, 1], [1, 0, -1, 1, 0, 1, 1, 1, 1],
                     [1, 0, 1, 1, 1, 1, 1, 1, 1], [-1, 0, 1, 0, 1, 1, 1, 1, 1]], dtype=float)
    batch = ttm.Batch(0, ttm.BLEND_ALPHA, True, 0, 0, quad, np.array([0, 1, 2, 0, 2, 3]))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    stats = ttm.write(OUT, [texture], [batch])
    print(f'{OUT}: {stats["file_bytes"]} bytes')


if __name__ == '__main__':
    main()
