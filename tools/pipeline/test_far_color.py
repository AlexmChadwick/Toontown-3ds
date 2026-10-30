#!/usr/bin/env python3
"""Far-LOD hill cards and the grid ladder. No Panda and no 3DS toolchain."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import ttm
import bake_street
import ground_split
import street_lod


def hill_image():
    image = np.zeros((32, 32, 4), dtype=np.uint8)
    image[:, :, 1] = 180
    image[:, :, 3] = 255
    image[:8, :, 3] = 0
    image[:8, :, 0] = 255
    return image


def card():
    verts = np.array([
        [0, 0, 0, 0, 0, 1, 1, 1, 1],
        [130, 0, 10, 1, 0, 1, 1, 1, 1],
        [0, 0, 40, 0, 1, 1, 1, 1, 1],
        [130, 0, 40, 1, 1, 1, 1, 1, 1],
    ], dtype=np.float64)
    return ttm.Batch(0, ttm.BLEND_ALPHA_TEST, False, 0, 0, verts, np.array([0, 1, 2, 1, 3, 2]))


def prop():
    verts = np.array([
        [0, 0, 0, 0.1, 0.2, 1, 1, 1, 1],
        [2, 0, 0, 0.2, 0.2, 1, 1, 1, 1],
        [0, 2, 0, 0.1, 0.3, 1, 1, 1, 1],
    ], dtype=np.float64)
    return ttm.Batch(0, ttm.BLEND_OPAQUE, False, 0, 0, verts, np.arange(3))


class FarColorTest(unittest.TestCase):
    def test_hill_card_keeps_its_texture_and_a_prop_does_not(self):
        low, textures, report = bake_street.choose_far(
            None, [[card(), prop()]], [{'vis': [0]}], images={0: hill_image()})
        self.assertEqual(textures, [])
        self.assertGreater(report['hill_card_triangles'], 0)
        self.assertLessEqual(report['low_worst_draws'], street_lod.DRAW_CAP)
        kept = [b for b in low[0] if b.texture is not None]
        flat = [b for b in low[0] if b.texture is None]
        self.assertEqual(len(kept), 1)
        self.assertEqual(kept[0].blend, ttm.BLEND_ALPHA_TEST)
        self.assertGreater(float(np.ptp(kept[0].verts[:, 3:5])), 0.5)
        self.assertTrue(flat)
        self.assertTrue(np.all(flat[0].verts[:, 3:5] == 0))

    def test_sign_letters_stay_textured_when_the_far_mesh_is_coloured(self):
        letters = np.array([
            [0, 0, 4, 0.0, 0.0, 1, 1, 1, 1],
            [1, 0, 4, 0.2, 0.0, 1, 1, 1, 1],
            [0, 0, 5, 0.0, 0.4, 1, 1, 1, 1],
        ], dtype=np.float64)
        sign = ttm.Batch(1, ttm.BLEND_ALPHA, False, 0, 1, letters, np.arange(3), sign=True)
        low, textures, report = bake_street.choose_far(
            None, [[card(), prop(), sign]], [{'vis': [0]}], images={0: hill_image()})
        self.assertEqual(textures, [])
        self.assertEqual(report['sign_triangles'], 1)
        kept = [b for b in low[0] if getattr(b, 'sign', False)]
        self.assertEqual(len(kept), 1)
        self.assertEqual(kept[0].texture, 1)
        self.assertGreater(float(np.ptp(kept[0].verts[:, 3:5])), 0.1)
        self.assertLessEqual(report['low_worst_draws'], street_lod.DRAW_CAP)

    def test_grid_ladder_starts_at_the_requested_floor(self):
        self.assertEqual(bake_street.grid_ladder(128)[0], 128)
        self.assertIn(192, bake_street.grid_ladder(128))
        self.assertEqual(bake_street.grid_ladder(192)[0], 192)
        self.assertNotIn(128, bake_street.grid_ladder(192))
        self.assertEqual(bake_street.grid_ladder(100)[0], 100)

    def test_hollow_far_mesh_loses_to_one_that_keeps_triangles(self):
        hollow = {'low_worst_triangles': 700, 'voxel_spacing': 48}
        body = {'low_worst_triangles': 6000, 'voxel_spacing': 8}
        self.assertTrue(bake_street.prefer_far(hollow, None))
        self.assertTrue(bake_street.prefer_far(body, hollow))
        self.assertFalse(bake_street.prefer_far(hollow, body))
        self.assertGreaterEqual(body['low_worst_triangles'], bake_street.FAR_BODY)

    def test_lower_draw_bound_counts_nonempty_cells(self):
        raw = [[card()], [], [prop()]]
        chunks = [{'vis': [0, 1, 2]}, {'vis': [1]}]
        self.assertEqual(bake_street.lower_draw_bound(raw, chunks), 2)

    def test_monolithic_ground_loosens_the_edge_to_fit(self):
        rows = []
        for p in ((0, 0, 0), (200, 0, 0), (0, 80, 0)):
            rows.append([p[0], p[1], p[2], 0, 0, 1, 1, 1, 1])
        batch = ttm.Batch(0, 0, False, 0, 0, np.array(rows, dtype=np.float64), np.arange(3))
        out, stats = ground_split.split_under([batch], tri_limit=12)
        self.assertLessEqual(stats['triangles_out'], 12)
        self.assertGreater(stats['max_edge'], ground_split.MAX_EDGE)
        longest = 0.0
        for b in out:
            for tri in b.verts[b.indices.reshape(-1, 3)]:
                if ground_split._up(tri) >= 0.5:
                    longest = max(longest, float(np.max(ground_split._edges(tri))))
        self.assertLessEqual(longest, stats['max_edge'] + 1e-6)


if __name__ == '__main__':
    unittest.main()
