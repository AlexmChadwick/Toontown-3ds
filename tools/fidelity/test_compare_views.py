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


COMMIT = '0' * 40


def scene():
    """A frame with real texture (smoothed noise) and clean step edges (vertical bands)."""
    rng = np.random.default_rng(12)
    tex = ndimage.gaussian_filter(rng.normal(0, 1, (240, 400)), 1.0)
    bands = np.where((np.arange(400) // 20) % 2, 70.0, 180.0)[None, :].repeat(240, 0)
    grey = ndimage.gaussian_filter(bands, 0.7) + tex / tex.std() * 16
    return np.repeat(grey[:, :, None], 3, axis=2)


def halo(image, amount=2.0):
    """Unsharp mask: the ringing QA-20261119-84 found."""
    return image + amount * (image - ndimage.gaussian_filter(image, (1, 1, 0)))


def pin_for(directory):
    """STOCK_PIN for a write_stock dir as it stands (the tree's render hash, its PNG hashes)."""
    return {'tt_resources_commit': COMMIT, 'render_sha256': near_views.render_sha256(),
            'png': {n: hashlib.sha256((Path(directory) / f'stock_{n}.png').read_bytes()).hexdigest()
                    for n, v in near_views.views().items() if v['r11_lens']}}


def write_stock(directory, image, meta_fovy=None, eye_shift=0.0, meta=True):
    """A stock_detail.py-style dir: stock_<view>.png per near view and, unless meta is False,
    a stock_meta.json (lens main.cpp's unless meta_fovy, eyes shifted by eye_shift)."""
    out = {'fovy': near_views.fovy() if meta_fovy is None else meta_fovy, 'size': [400, 240],
           'tt_resources': '/res', 'tt_resources_commit': COMMIT, 'render_sha256': near_views.render_sha256(),
           'views': {}}
    for name, view in near_views.views().items():
        if not view['r11_lens']:
            continue
        png = Path(directory) / f'stock_{name}.png'
        Image.fromarray(np.clip(image, 0, 255).astype(np.uint8)).save(png)
        out['views'][name] = {'zone': view['zone'], 'eye': [v + eye_shift for v in view['eye']],
                              'target': view['target'], 'sha256': hashlib.sha256(png.read_bytes()).hexdigest()}
    if meta:
        (Path(directory) / 'stock_meta.json').write_text(json.dumps(out))


class Gates(unittest.TestCase):
    def setUp(self):
        self.image = np.random.default_rng(12).integers(0, 256, (240, 400, 3)).astype(float)
        self.scene = scene()
        self.stock = ndimage.gaussian_filter(self.scene, (.5, .5, 0))
        patch = mock.patch.object(compare_views, 'resources_commit', return_value=COMMIT)
        patch.start()
        self.addCleanup(patch.stop)

    def test_identical_detail(self):
        # Sharper than a softer stock, no halo: all four near bars pass.
        why, columns = compare('ttc_near', self.scene, self.scene, 120, 120, self.stock)[:2]
        self.assertFalse(why)
        for name in ('texture detail', 'off-edge detail', 'high-pass', 'overshoot'):
            self.assertTrue(columns[name][0], name)

    def test_halo_fails_highpass_and_overshoot(self):
        # QA-20261119-84: a 200% unsharp mask clears (a) and (b) but rings.
        ringing = halo(self.stock)
        why, columns = compare('ttc_near', ringing, ringing, 120, 120, self.stock)[:2]
        self.assertTrue(columns['texture detail'][0])
        self.assertTrue(columns['off-edge detail'][0])
        self.assertFalse(columns['high-pass'][0])
        self.assertFalse(columns['overshoot'][0])

    def test_off_edge_detail(self):
        # Sharp edges over flat texture: the off-edge column fails though (a) may pass.
        bands = ndimage.gaussian_filter(np.where((np.arange(400) // 20) % 2, 70.0, 180.0)[None, :].repeat(240, 0), .3)
        flat = np.repeat(bands[:, :, None], 3, axis=2)
        columns = compare('ttc_near', flat, flat, 120, 120, self.stock)[1]
        self.assertFalse(columns['off-edge detail'][0])

    def test_near_columns_all_printed_without_stock(self):
        columns = compare('dd_near', self.scene, self.scene, 120, 120)[1]
        for name in ('texture detail', 'off-edge detail', 'high-pass', 'overshoot'):
            self.assertFalse(columns[name][0], name)

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
        why, columns = compare('street_near', self.scene, self.scene, 74, 84, self.stock, ['6', '54'])[:2]
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
            self.assertEqual(check_stock(d, pin=pin_for(d)), [])

    def test_stock_rehashed_blurred_fails_pin(self):
        # QA-20261119-84 finding 2: blur the stock PNGs and re-hash them into the meta (lens and
        # poses unchanged). The meta is self-consistent, but the PNGs are not the pinned render.
        with tempfile.TemporaryDirectory() as d:
            write_stock(d, self.image)
            pin = pin_for(d)
            write_stock(d, ndimage.gaussian_filter(self.image, (2, 2, 0)))
            why = check_stock(d, pin=pin)
            self.assertFalse(any('does not hash' in s for s in why))
            self.assertTrue(any('not the pinned stock render' in s for s in why))

    def test_stock_wrong_resources_commit(self):
        with tempfile.TemporaryDirectory() as d:
            write_stock(d, self.image)
            pin = dict(pin_for(d), tt_resources_commit='1' * 40)
            self.assertTrue(any('TT_RESOURCES commit' in s for s in check_stock(d, pin=pin)))

    def test_stock_resources_checkout_moved(self):
        # The meta names the pinned commit, but the checkout it names is somewhere else now.
        with tempfile.TemporaryDirectory() as d:
            write_stock(d, self.image)
            with mock.patch.object(compare_views, 'resources_commit', return_value='2' * 40):
                self.assertTrue(any('not the pinned commit' in s for s in check_stock(d, pin=pin_for(d))))

    def test_stock_render_script_changed(self):
        with tempfile.TemporaryDirectory() as d:
            write_stock(d, self.image)
            pin = dict(pin_for(d), render_sha256='3' * 64)
            self.assertTrue(any('render scripts' in s for s in check_stock(d, pin=pin)))

    def test_stock_pin_matches_tree(self):
        # STOCK_PIN's render hash is the tree's: editing the stock render scripts needs a re-pin.
        self.assertEqual(compare_views.STOCK_PIN['render_sha256'], near_views.render_sha256())

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
