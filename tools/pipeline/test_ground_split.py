#!/usr/bin/env python3
"""Ground-split checks that do not need Panda or the 3DS toolchain."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import ttm
import ground_split


def tri(p0, p1, p2):
    rows = []
    for p in (p0, p1, p2):
        rows.append([p[0], p[1], p[2], p[0] / 40.0, p[1] / 40.0, 0.2, 0.4, 0.6, 1.0])
    return np.array(rows, dtype=np.float64)


class GroundSplitTest(unittest.TestCase):
    def test_long_ground_is_cut_and_wall_is_kept(self):
        ground = tri((0, 0, 0), (80, 0, 0), (0, 40, 0))
        wall = tri((0, 0, 0), (80, 0, 0), (0, 0, 40))
        batch = ttm.Batch(3, 0, False, 0, 0, np.concatenate([ground, wall]), np.arange(6))
        out, stats = ground_split.split_batches([batch])
        self.assertEqual(stats['split'], 1)
        self.assertGreater(stats['triangles_out'], stats['triangles_in'])
        verts = np.concatenate([b.verts for b in out])
        edges = []
        for t in verts.reshape(-1, 3, 9):
            up = ground_split._up(t)
            longest = float(np.max(ground_split._edges(t)))
            if up >= 0.5:
                self.assertLessEqual(longest, ground_split.MAX_EDGE + 1e-6)
            edges.append(longest)
        self.assertGreater(max(edges), 40)  # the wall edge is still long
        # Midpoint interpolation keeps the ground colour and a UV inside the original range.
        self.assertTrue(np.all(np.abs(verts[:, 5:9] - [0.2, 0.4, 0.6, 1.0]) < 1e-6))
        self.assertGreaterEqual(float(verts[:, 3].min()), 0)
        self.assertLessEqual(float(verts[:, 3].max()), 2)

    def test_playground_sized_ground_ends_under_the_limit(self):
        # TTC's longest baked ground edge is about 609 units.
        ground = tri((0, 0, 0), (609, 0, 0), (0, 400, 0))
        batch = ttm.Batch(1, 0, False, 0, 0, ground, np.arange(3))
        out, stats = ground_split.split_batches([batch])
        self.assertEqual(stats['split'], 1)
        self.assertLess(stats['triangles_out'], ground_split.MAX_PIECES)
        for b in out:
            for t in b.verts.reshape(-1, 3, 9):
                self.assertLessEqual(float(np.max(ground_split._edges(t))), ground_split.MAX_EDGE + 1e-4)

    def test_short_ground_is_unchanged(self):
        ground = tri((0, 0, 1), (10, 0, 1), (0, 10, 1))
        batch = ttm.Batch(None, 0, True, 1, 2, ground, np.arange(3))
        out, stats = ground_split.split_batches([batch])
        self.assertEqual(stats['split'], 0)
        self.assertEqual(len(out), 1)
        self.assertEqual(out[0].texture, None)
        self.assertTrue(out[0].two_sided)
        self.assertEqual(len(out[0].verts), 3)


if __name__ == '__main__':
    unittest.main()
