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
REAL_KNOWN_GAP = dict(compare_views.KNOWN_GAP)


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
        # The numeric gate fixtures run with no KNOWN GAP views; Known tests set their own floors.
        gap = mock.patch.object(compare_views, 'KNOWN_GAP', {})
        gap.start()
        self.addCleanup(gap.stop)

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

    def floors_from(self, view, frame):
        """KNOWN_GAP bars measured raw on `frame` (old = new = frame), exactly as compare() scores it."""
        sl, g = self.stock.mean(2), frame.mean(2)
        edges = near_views.edge_map(sl) | near_views.edge_map(g)
        return {view: {'a': near_views.detail(g),
                       'b': near_views.off_edge(g, edges)/near_views.off_edge(sl, edges),
                       'd': near_views.overshoot(g)/near_views.overshoot(sl)}}

    def test_known_gap_exactly_at_floors_holds(self):
        # Floors exactly at the raw values hold; the view is still scored on the QA-19 bar.
        gap = self.floors_from('ttc_near', self.scene)
        with mock.patch.object(compare_views, 'KNOWN_GAP', gap):
            why, columns = compare('ttc_near', self.scene, self.scene, 120, 120, self.stock)[:2]
        self.assertTrue(columns['known gap'][0])
        self.assertEqual(columns['known gap'][1].count(' held'), 3)
        self.assertFalse(why)

    def test_known_gap_0_001_past_each_floor_breaks(self):
        # Floors (a) (b) 0.001 above the raw value, ceiling (d) 0.001 below it: each breaks alone.
        for key, step in (('a', .001), ('b', .001), ('d', -.001)):
            gap = self.floors_from('ttc_near', self.scene)
            gap['ttc_near'][key] += step
            with mock.patch.object(compare_views, 'KNOWN_GAP', gap):
                why, columns = compare('ttc_near', self.scene, self.scene, 120, 120, self.stock)[:2]
            self.assertFalse(columns['known gap'][0], key)
            self.assertIn(f'({key}) raw', columns['known gap'][1])
            self.assertEqual(columns['known gap'][1].count('BROKEN'), 1, key)
            self.assertEqual([w.split(' ')[0] for w in why], ['known'], key)

    def test_known_gap_floors_do_not_pass_qa19(self):
        # QA-20261119-145: a frame as sharp as stock holds floors set from it but fails (a) and (b).
        gap = self.floors_from('ttc_near', self.stock)
        with mock.patch.object(compare_views, 'KNOWN_GAP', gap):
            why, columns = compare('ttc_near', self.stock, self.stock, 120, 120, self.stock)[:2]
        self.assertTrue(columns['known gap'][0])
        self.assertFalse(columns['texture detail'][0])
        self.assertIn('vs 1.3x stock', columns['texture detail'][1])
        self.assertTrue(any(w.startswith('texture detail') for w in why))

    def test_box_values_fail_qa19_and_hold_floors(self):
        # The 2026-11-23 box gate values: red vs QA-19 (a) 1.3x stock and r11, (b) 1.0x; floors hold raw.
        box = {'ttc_near': {'a': 442.499893, 'stock_a': 407.808092, 'b': 0.617313, 'd': 1.493757},
               'dd_near': {'a': 257.717437, 'stock_a': 230.795694, 'b': 0.655692, 'd': 1.110967}}
        for view, v in box.items():
            self.assertLess(v['a'], 1.3*v['stock_a'], view)
            self.assertLess(v['a'], compare_views.DETAIL_R11[view], view)
            self.assertLess(v['b'], compare_views.OFF_EDGE_MIN, view)
            self.assertGreaterEqual(v['a'], REAL_KNOWN_GAP[view]['a'])
            self.assertGreaterEqual(v['b'], REAL_KNOWN_GAP[view]['b'])
            self.assertLessEqual(v['d'], REAL_KNOWN_GAP[view]['d'])
        # Old bars these values passed only through rounding.
        self.assertLess(257.717437, 258)
        self.assertLess(0.617313, 0.62)
        self.assertGreater(1.493757, 1.49)

    def test_known_gap_prints_raw_values(self):
        gap = self.floors_from('ttc_near', self.scene)
        with mock.patch.object(compare_views, 'KNOWN_GAP', gap):
            columns = compare('ttc_near', self.scene, self.scene, 120, 120, self.stock)[1]
        text = columns['known gap'][1]
        self.assertIn(f"(a) raw {gap['ttc_near']['a']:.6f} vs floor", text)
        self.assertIn(f"(b) raw {gap['ttc_near']['b']:.6f}x vs floor", text)
        self.assertIn(f"(d) raw {gap['ttc_near']['d']:.6f}x vs ceiling", text)

    def test_known_gap_wording(self):
        gap = self.floors_from('ttc_near', self.scene)
        with mock.patch.object(compare_views, 'KNOWN_GAP', gap):
            columns = compare('ttc_near', self.scene, self.scene, 120, 120, self.stock)[1]
        self.assertTrue(columns['known gap'][1].startswith(
            'KNOWN GAP ttc_near, unratified floors, gate red vs QA-19: '))
        src = (Path(__file__).parent / 'compare_views.py').read_text()
        self.assertNotIn('source-limited, QA-84 ruling', src)

    def test_known_gap_fails_below_floor(self):
        gap = self.floors_from('dd_near', self.scene)
        soft = ndimage.gaussian_filter(self.scene, (.8, .8, 0))
        with mock.patch.object(compare_views, 'KNOWN_GAP', gap):
            why, columns = compare('dd_near', soft, soft, 120, 120, self.stock)[:2]
        self.assertFalse(columns['known gap'][0])
        self.assertTrue(why)

    def test_gate_fails_when_sharpen_applied_to_zone_not_opted_in(self):
        # Floors from the unsharpened frame; sharpening that zone raises overshoot past the ceiling.
        gap = self.floors_from('ttc_near', self.scene)
        ringing = halo(self.scene)
        with mock.patch.object(compare_views, 'KNOWN_GAP', gap):
            why, columns = compare('ttc_near', ringing, ringing, 120, 120, self.stock)[:2]
        self.assertFalse(columns['overshoot'][0])
        self.assertFalse(columns['known gap'][0])
        self.assertTrue(why)

    def test_real_known_gap_zones(self):
        # Box sweep values rounded the safe way (floors down, ceilings up), QA ruling 2026-11-23.
        self.assertEqual(REAL_KNOWN_GAP, {'ttc_near': {'a': 442, 'b': 0.61, 'd': 1.50},
                                          'dd_near': {'a': 257, 'b': 0.65, 'd': 1.12}})

    def test_street_near_fails_below_1_3x(self):
        # street_near is not a known gap: as sharp as stock (1.0x) must fail (a).
        why, columns = compare('street_near', self.stock, self.stock, 84, 84, self.stock)[:2]
        self.assertFalse(columns['texture detail'][0])
        self.assertTrue(any('texture detail' in s for s in why))

    def test_street_near_not_known_gap(self):
        self.assertNotIn('street_near', compare_views.KNOWN_GAP)

    def test_baker_opts_in_only_street_2100(self):
        # bake_street imports panda3d (not in the gate's python), so read the literal from source.
        import ast
        tree = ast.parse((Path(__file__).parents[1]/'pipeline'/'bake_street.py').read_text())
        found = [ast.literal_eval(n.value) for n in tree.body
                 if isinstance(n, ast.Assign) and any(getattr(t, 'id', '') == 'NEAR_SHARPEN' for t in n.targets)]
        self.assertEqual(found, [{2100: (1.0, 150.0, 0.0, 1.0)}])

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
