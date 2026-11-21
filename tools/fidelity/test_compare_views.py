"""Synthetic fixtures exercise every numeric gate; these are not game images."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage
import compare_views
from compare_views import check_stock, compare
import near_views


def write_stock(directory, image, meta_fovy=None, eye_shift=0.0, meta=True):
    """A stock_detail.py-style dir: stock_<view>.png per near view and, unless meta is False,
    a stock_meta.json (lens main.cpp's unless meta_fovy, eyes shifted by eye_shift)."""
    out = {'fovy': near_views.fovy() if meta_fovy is None else meta_fovy, 'size': [400, 240], 'views': {}}
    for name, view in near_views.views().items():
        if not view['r11_lens']:
            continue
        png = Path(directory) / f'stock_{name}.png'
        Image.fromarray(image.astype(np.uint8)).save(png)
        out['views'][name] = {'zone': view['zone'], 'eye': [v + eye_shift for v in view['eye']],
                              'target': view['target'], 'sha256': hashlib.sha256(png.read_bytes()).hexdigest()}
    if meta:
        (Path(directory) / 'stock_meta.json').write_text(json.dumps(out))


class Gates(unittest.TestCase):
    def setUp(self):
        self.image = np.random.default_rng(12).integers(0, 256, (240, 400, 3)).astype(float)

    def test_identical_detail(self):
        self.assertFalse(compare('ttc_near', self.image, self.image, 120, 120, self.image*.5)[0])

    def test_missing_stock(self):
        self.assertTrue(any('stock' in s for s in compare('ttc_near', self.image, self.image, 120, 120)[0]))

    def test_detail_vs_stock(self):
        # As sharp as stock passes r11 but not 1.3x stock.
        why = compare('ttc_near', self.image, self.image, 120, 120, self.image)[0]
        self.assertTrue(any('detail' in s for s in why))

    def test_detail_vs_r11(self):
        soft = np.full_like(self.image, 140)
        soft[::2] += 8
        why = compare('dd_near', soft, soft, 120, 120, np.full_like(soft, 140))[0]
        self.assertTrue(any('r11' in s for s in why))

    def test_draw_waiver_identical(self):
        why, columns = compare('street_near', self.image, self.image, 74, 84, self.image*.5, ['6', '54'])[:2]
        self.assertFalse(why)
        self.assertIn('draw ratio waived: frames identical (0 px differ), Old far LOD chunks 6/54',
                      columns['draws'][1])

    def test_draw_waiver_needs_identical(self):
        other = self.image.copy()
        other[0, 0, 0] += 1
        self.assertTrue(compare('street_near', self.image, other, 74, 84, self.image*.5, ['6', '54'])[0])

    def test_draw_waiver_only_street_near(self):
        why = compare('ttc_mid', self.image, self.image, 74, 84, None, ['6', '54'])[0]
        self.assertTrue(any('no waiver for ttc_mid' in s for s in why))

    def test_draw_waiver_needs_plan(self):
        why = compare('street_near', self.image, self.image, 74, 84, self.image*.5)[0]
        self.assertTrue(any('no --plan' in s for s in why))

    def test_mask_ignores_toon_column(self):
        # Edges only inside x 165-245 (the toon and nametag) add no detail.
        flat = np.full((240, 400), 140.0)
        toon = flat.copy()
        toon[60:200, 170:240:3] = 0
        self.assertEqual(near_views.detail(flat), near_views.detail(toon))
        self.assertGreater(ndimage.laplace(toon).var(), 0)

    def test_stock_meta_ok(self):
        with tempfile.TemporaryDirectory() as d:
            write_stock(d, self.image)
            self.assertEqual(check_stock(d), [])

    def test_stock_without_meta(self):
        with tempfile.TemporaryDirectory() as d:
            blurred = ndimage.gaussian_filter(self.image, (2, 2, 0))
            write_stock(d, blurred, meta=False)
            self.assertTrue(any('no stock_meta.json' in s for s in check_stock(d)))

    def test_stock_wrong_lens(self):
        with tempfile.TemporaryDirectory() as d:
            write_stock(d, self.image, meta_fovy=40.2)
            self.assertTrue(any('lens 40.2' in s for s in check_stock(d)))

    def test_stock_wrong_pose(self):
        with tempfile.TemporaryDirectory() as d:
            write_stock(d, self.image, eye_shift=1.0)
            self.assertTrue(any('eye' in s for s in check_stock(d)))

    def test_stock_swapped_png(self):
        # A blurred PNG dropped in after stock_detail.py wrote the meta no longer hashes to it.
        with tempfile.TemporaryDirectory() as d:
            write_stock(d, self.image)
            png = Path(d) / 'stock_ttc_near.png'
            Image.open(png).filter(ImageFilter.GaussianBlur(2)).save(png)
            self.assertTrue(any('does not hash' in s for s in check_stock(d)))

    def test_main_fails_on_blurred_stock_without_meta(self):
        # End to end: identical sharp captures, a blurred stock with no meta: exit 1.
        with tempfile.TemporaryDirectory() as d:
            views, stock = Path(d) / 'v', Path(d) / 's'
            views.mkdir()
            stock.mkdir()
            for name in compare_views.VIEWS:
                Image.fromarray(self.image.astype(np.uint8)).save(views / f'shot_view_{name}_top.png')
            write_stock(stock, ndimage.gaussian_filter(self.image, (3, 3, 0)), meta=False)
            log = Path(d) / 'log.txt'
            log.write_text(''.join(f'view: {n} draws 100\n' for n in compare_views.VIEWS))
            argv = ['compare_views.py', str(views), str(views), '--old-log', str(log), '--new-log', str(log),
                    '--stock', str(stock)]
            with mock.patch.object(sys, 'argv', argv), mock.patch('builtins.print'):
                self.assertEqual(compare_views.main(), 1)

    def test_flat_sky(self):
        flat = np.full_like(self.image, 140)
        self.assertTrue(any('sky flat' in s for s in compare('dd_far', flat, flat, 120, 120)[0]))

    def test_missing_prop(self):
        blob = self.image.copy()
        blob[48:96, 50:100] = 140
        self.assertTrue(any('tile ssim' in s for s in compare('ttc_far', blob, self.image, 120, 120)[0]))

    def test_missing_draws(self):
        self.assertTrue(compare('ttc_mid', self.image, self.image)[0])
        other = self.image.copy()
        other[0, 0, 0] += 1
        self.assertTrue(compare('ttc_mid', self.image, other, 89, 111)[0])

    def test_flat_detail(self):
        flat = np.full_like(self.image, 140)
        self.assertTrue(any('texture detail' in s for s in compare('dd_near', flat, flat, 120, 120)[0]))

    def test_pixel_difference(self):
        other = 255-self.image
        self.assertTrue(any('px differ' in s for s in compare('ttc_mid', other, self.image, 120, 120)[0]))


if __name__ == '__main__':
    unittest.main()
