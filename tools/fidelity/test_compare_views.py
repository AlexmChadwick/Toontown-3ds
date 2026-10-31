"""Synthetic fixtures exercise every numeric gate; these are not game images."""
import unittest
import numpy as np
from compare_views import compare


class Gates(unittest.TestCase):
    def setUp(self):
        self.image = np.random.default_rng(12).integers(0, 256, (240, 400, 3)).astype(float)

    def test_identical_detail(self):
        self.assertFalse(compare('ttc_near', self.image, self.image, 120, 120)[0])

    def test_flat_sky(self):
        flat = np.full_like(self.image, 140)
        self.assertTrue(any('sky flat' in s for s in compare('dd_far', flat, flat, 120, 120)[0]))

    def test_missing_prop(self):
        blob = self.image.copy()
        blob[48:96, 50:100] = 140
        self.assertTrue(any('tile ssim' in s for s in compare('ttc_far', blob, self.image, 120, 120)[0]))

    def test_missing_draws(self):
        self.assertTrue(compare('ttc_mid', self.image, self.image)[0])
        self.assertTrue(compare('ttc_mid', self.image, self.image, 89, 111)[0])

    def test_flat_detail(self):
        flat = np.full_like(self.image, 140)
        self.assertTrue(any('texture detail' in s for s in compare('dd_near', flat, flat, 120, 120)[0]))

    def test_pixel_difference(self):
        other = 255-self.image
        self.assertTrue(any('px differ' in s for s in compare('ttc_mid', other, self.image, 120, 120)[0]))


if __name__ == '__main__':
    unittest.main()
