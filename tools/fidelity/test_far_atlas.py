"""Far shells retain real geometry, alpha, UV detail and decal layers."""
from pathlib import Path
import sys
import tempfile
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'pipeline'))
from panda3d.core import Texture, PNMImage
import far_atlas
import ttm


def texture():
    tex = Texture('wall')
    image = PNMImage(64, 64, 4)
    image.fill(1, 1, 1)
    image.alphaFill(1)
    tex.load(image)
    tex.setWrapU(Texture.WM_repeat)
    tex.setWrapV(Texture.WM_clamp)
    return tex


def batch(tex=0, layer=0):
    vs = np.array([[0, 0, 0, -.5, 0, 1, .8, .6, 1],
                   [30, 0, 0, 2.5, 0, 1, .8, .6, 1],
                   [0, 0, 30, -.5, 1, 1, .8, .6, 1]], dtype=float)
    return ttm.Batch(tex, ttm.BLEND_ALPHA_TEST, True, 0, layer, vs, np.arange(3))


class Shells(unittest.TestCase):
    def test_no_missing_layers_or_geometry(self):
        src = [batch(layer=layer) for layer in range(5)]
        tex, chunks = far_atlas.build([texture()], [src])
        self.assertEqual({b.layer for b in chunks[0]}, set(range(5)))
        for before, after in zip(src, sorted(chunks[0], key=lambda b: b.layer)):
            np.testing.assert_array_equal(before.verts[:, :3], after.verts[:, :3])
            np.testing.assert_array_equal(before.verts[:, 5:], after.verts[:, 5:])
            self.assertEqual(before.blend, after.blend)
        self.assertTrue(all(t.w in (128, 256, 512) and t.mips == 3 for t in tex))

    def test_merge_and_determinism(self):
        src = [texture() for _ in range(40)]
        bs = [batch(i) for i in range(40)]
        textures, chunks = far_atlas.build(src, [bs])
        self.assertEqual(len(chunks[0]), 1)
        self.assertEqual(len(chunks[0][0].indices), 120)
        with tempfile.TemporaryDirectory() as directory:
            a, b = Path(directory)/'a.ttm', Path(directory)/'b.ttm'
            ttm.write(a, textures, chunks[0])
            textures2, chunks2 = far_atlas.build(src, [bs])
            ttm.write(b, textures2, chunks2[0])
            self.assertEqual(a.read_bytes(), b.read_bytes())

    def test_real_uv_detail_and_alpha(self):
        image = np.zeros((64, 64, 4), dtype=np.uint8)
        image[:, :, 0] = np.arange(64)[None, :]*4
        image[:, :, 3] = np.arange(64)[:, None]*4
        p = far_atlas.patch(image, batch().verts, 32, (True, False))
        self.assertGreater(p[:, :, 0].std(), 30)
        self.assertGreater(p[:, :, 3].std(), 30)
        uv = np.array([[[.25, .5], [1.25, .5]]])
        got = far_atlas.sample(image, uv, (True, False))
        np.testing.assert_array_equal(got[0, 0], got[0, 1])


if __name__ == '__main__':
    unittest.main()
