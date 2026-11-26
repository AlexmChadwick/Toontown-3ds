#!/usr/bin/env python3
"""Bake the Map-tab minimap of every exterior zone: .tz -> top-down ETC1 .tmap.

    /Users/alexchadwick/toontown-3ds/.venv/bin/python -I tools/pipeline/bake_minimap.py \
        [--zones-dir DIR] [--out-dir DIR] [--preview-dir DIR] [ZONE ...]

The picture is rendered from the decoded .tz chunk TTM bytes (far LOD where present, so
it matches what the game streams) with an orthographic camera looking straight down
(+X right, +Y up) on Panda's software offscreen buffer, supersampled 4x, box-filtered
and encoded as one ETC1 level.

TMAP v1 (little endian, <zone>.tmap next to <zone>.tz):
    0   char[4] "TMAP"; u16 version=1; u16 flags=0
    8   u16 width; u16 height; u8 gpuFormat (0xC ETC1); u8 mipLevels (=1); u16 pad
    16  f32 minX, minY, maxX, maxY    world rectangle the image covers exactly
    32  u32 dataOffset (=128); u32 dataSize (=w*h/2); u32 reserved[2]
    48  zero padding to 128
    128 PICA-tiled ETC1 data (same bytes a TTM texture entry points at)
Runtime mapping: u = (x - minX) / (maxX - minX); v = (maxY - y) / (maxY - minY), both
0..1 with v = 0 at the image top. Pixels are square (the rectangle has the image's
aspect). Sizes: 256x256, 256x128 / 128x256 for elongated zones, 256x64 / 64x256 beyond 3:1.
Deterministic: same .tz in, same bytes out.
"""
import os
for _key in ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'MKL_NUM_THREADS', 'VECLIB_MAXIMUM_THREADS'):
    os.environ[_key] = '1'
os.environ.setdefault('TT_ETC1_THREADS', '4')
import argparse
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import numpy as np  # noqa: E402

import etc1  # noqa: E402
import tz  # noqa: E402

MAGIC = b'TMAP'
VERSION = 1
HEADER = struct.Struct('<4sHHHHBBH4f4I')
DATA_OFFSET = 128
GPU_ETC1 = etc1.GPU_ETC1
SUPER = 4
MARGIN = 8.0        # world units around the playable floor
BG = (0x1E, 0x2A, 0x44)   # matches kBg of the bottom-screen UI
DEFAULT_ZONES = ROOT / 'sdmc-out/3ds/toontown3ds/data/zones'


def pick_size(ext_x, ext_y):
    r = ext_x / ext_y
    if r >= 3.0:
        return 256, 64
    if r >= 1.5:
        return 256, 128
    if r <= 1 / 3.0:
        return 64, 256
    if r <= 1 / 1.5:
        return 128, 256
    return 256, 256


def floor_bounds(zone):
    f = np.array([fl[:9] for fl in zone['floors']], dtype=np.float64).reshape(-1, 3)
    lo, hi = f.min(axis=0), f.max(axis=0)
    return lo[:2] - MARGIN, hi[:2] + MARGIN


def write_tmap(path, w, h, rect, data):
    assert len(data) == w * h // 2
    out = bytearray(DATA_OFFSET + len(data))
    HEADER.pack_into(out, 0, MAGIC, VERSION, 0, w, h, GPU_ETC1, 1, 0, *rect, DATA_OFFSET, len(data), 0, 0)
    out[DATA_OFFSET:] = data
    Path(path).write_bytes(out)


class Renderer:
    def __init__(self):
        from panda3d.core import loadPrcFileData
        loadPrcFileData('', 'window-type offscreen\naudio-library-name null\nnotify-level warning\n'
                            'load-display p3tinydisplay\nmodel-cache-dir\nwin-size 1024 1024\n'
                            'framebuffer-multisample 0')
        from direct.showbase.ShowBase import ShowBase
        self.base = ShowBase()
        if self.base.win is None:
            raise RuntimeError('Panda offscreen buffer unavailable')
        self.base.disableMouse()
        self.base.setBackgroundColor(BG[0] / 255, BG[1] / 255, BG[2] / 255, 1)
        self.cache = {}

    def render(self, data, zone, centre, side):
        """Render one zone; returns (1024, 1024, 3) uint8 covering `side` world units square."""
        from panda3d.core import OrthographicLens, PNMImage
        import street_preview
        base = self.base
        self.cache = {}     # fresh per zone: keeps output independent of bake order
        group = base.render.attachNewNode('zone')
        for c in zone['chunks']:
            off, size = (c['low_offset'], c['low_size']) if c['low_size'] else (c['offset'], c['size'])
            if not size:
                continue
            street_preview.load_model(tz.model_bytes(data, off, size), self.cache).reparentTo(group)
        top = max(group.getTightBounds()[1].z, 0.0) + 100.0
        lens = OrthographicLens()
        lens.setFilmSize(side, side)
        lens.setNearFar(1.0, top + 1000.0)
        base.cam.node().setLens(lens)
        base.camera.setPos(centre[0], centre[1], top)
        base.camera.setHpr(0, -90, 0)
        for _ in range(3):
            base.graphicsEngine.renderFrame()
        img = PNMImage()
        assert base.win.getScreenshot().store(img)
        group.removeNode()
        n = img.getXSize()
        arr = np.empty((n, n, 3), dtype=np.uint8)
        for y in range(n):
            row = [img.getXel(x, y) for x in range(n)]
            arr[y] = np.round(np.array(row) * 255)
        return arr


def downsample(arr):
    a = arr.astype(np.uint16)
    h, w = a.shape[:2]
    a = a.reshape(h // SUPER, SUPER, w // SUPER, SUPER, -1).sum(axis=(1, 3))
    return ((a + SUPER * SUPER // 2) // (SUPER * SUPER)).astype(np.uint8)


def bake(renderer, path, out_dir, preview_dir):
    data = path.read_bytes()
    zone = tz.validate(data)
    lo, hi = floor_bounds(zone)
    ext = hi - lo
    w, h = pick_size(*ext)
    s = float(max(ext[0] / w, ext[1] / h))     # world units per map pixel
    cx, cy = (lo + hi) / 2
    rect = (cx - w * s / 2, cy - h * s / 2, cx + w * s / 2, cy + h * s / 2)
    side = s * 1024 / SUPER                    # a 1024px render covers 256 map px per axis
    big = renderer.render(data, zone, (cx, cy), side)
    # 1024 render px = 256 map px per axis; crop the centre w x h map px (x SUPER).
    x0, y0 = (1024 - w * SUPER) // 2, (1024 - h * SUPER) // 2
    img = downsample(big[y0:y0 + h * SUPER, x0:x0 + w * SUPER])
    rgba = np.concatenate([img, np.full((h, w, 1), 255, dtype=np.uint8)], axis=2)
    blob = etc1.encode_levels([rgba], False)
    zid = zone['branch']
    write_tmap(out_dir / f'{zid}.tmap', w, h, rect, blob)
    if preview_dir:
        from panda3d.core import Filename, PNMImage
        pv = PNMImage(w, h, 3)
        for y in range(h):
            for x in range(w):
                pv.setXel(x, y, *(float(v) / 255 for v in img[y, x]))
        pv.write(Filename.fromOsSpecific(str(preview_dir / f'{zid}.png')))
    print(f'{zid}: {w}x{h} scale {s:.3f} u/px  rect ({rect[0]:.1f},{rect[1]:.1f})-({rect[2]:.1f},{rect[3]:.1f})  '
          f'{DATA_OFFSET + len(blob)} bytes', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('zones', nargs='*', type=int)
    ap.add_argument('--zones-dir', type=Path, default=DEFAULT_ZONES)
    ap.add_argument('--out-dir', type=Path, default=ROOT / 'sdmc-out/3ds/toontown3ds/data/zones')
    ap.add_argument('--preview-dir', type=Path, default=ROOT / 'sdmc-out/previews/minimap')
    args = ap.parse_args()
    files = sorted(p for p in args.zones_dir.glob('*.tz') if p.stem.isdigit())
    if args.zones:
        files = [p for p in files if int(p.stem) in args.zones]
    args.out_dir.mkdir(parents=True, exist_ok=True)
    args.preview_dir.mkdir(parents=True, exist_ok=True)
    renderer = Renderer()
    for p in files:
        bake(renderer, p, args.out_dir, args.preview_dir)
    renderer.base.destroy()


if __name__ == '__main__':
    main()
