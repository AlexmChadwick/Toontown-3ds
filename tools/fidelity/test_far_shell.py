"""Quadric regression: borders/UV seams, decals and every component survive."""
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'pipeline'))
import far_shell
import ttm


def grid(offset=0):
    v = np.array([[offset+x, y, 0, x/10, y/10, 1, 1, 1, 1]
                  for y in range(11) for x in range(11)], dtype=float)
    faces = []
    for y in range(10):
        for x in range(10):
            i = y*11+x
            faces.extend([i, i+1, i+11, i+1, i+12, i+11])
    return ttm.Batch(None, ttm.BLEND_OPAQUE, False, 0, 0, v, np.array(faces))


class Shells(unittest.TestCase):
    def test_border_and_uv(self):
        original = grid()
        reduced = far_shell.simplify(original, .5)
        self.assertLess(len(reduced.indices), len(original.indices))
        np.testing.assert_allclose(reduced.verts[:, 3:5], reduced.verts[:, :2]/10, atol=1e-6)
        border = original.verts[(original.verts[:, 0] == 0) | (original.verts[:, 0] == 10) |
                                (original.verts[:, 1] == 0) | (original.verts[:, 1] == 10), :3]
        for p in border:
            self.assertTrue(np.any(np.all(np.isclose(reduced.verts[:, :3], p), axis=1)))

    def test_decals_and_alpha_cards_unchanged(self):
        b = grid()
        b.layer = 1
        self.assertIs(far_shell.simplify(b, .1), b)
        b.layer = 0
        b.blend = ttm.BLEND_ALPHA_TEST
        b.indices = b.indices[:6] # an original two-triangle leaf card
        self.assertIs(far_shell.simplify(b, .1), b)

    def test_coincident_uv_seams(self):
        a, b = grid(), grid()
        b.verts[:, 3] += 2
        combined = ttm.Batch(None, 0, False, 0, 0, np.concatenate((a.verts, b.verts)),
                             np.concatenate((a.indices, b.indices+len(a.verts))))
        out = far_shell.simplify(combined, .5)
        offset = out.verts[:, 3]-out.verts[:, 0]/10
        self.assertTrue(np.all(np.isclose(offset, 0) | np.isclose(offset, 2)))
        self.assertTrue(np.any(np.isclose(offset, 0)) and np.any(np.isclose(offset, 2)))

    def test_disconnected_components(self):
        a, b = grid(), grid(100)
        combined = ttm.Batch(None, 0, False, 0, 0, np.concatenate((a.verts, b.verts)),
                             np.concatenate((a.indices, b.indices+len(a.verts))))
        out = far_shell.simplify(combined, .5)
        self.assertTrue(np.any(out.verts[:, 0] < 20))
        self.assertTrue(np.any(out.verts[:, 0] > 90))


if __name__ == '__main__':
    unittest.main()
