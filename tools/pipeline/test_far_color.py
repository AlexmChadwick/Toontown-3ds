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

    def test_kept_decal_stays_in_front_of_its_snapped_wall(self):
        # A wall facing -y at y=0.9 collapses to voxel means on its own plane at spacing 2;
        # a decal 0.1 in front of it (y=0.8, wound to face -y) must stay in front.
        def quad(y, layer):
            v = np.array([[0, y, 0], [4, y, 4], [0, y, 4], [0, y, 0], [4, y, 0], [4, y, 4]], float)
            verts = np.zeros((6, 9))
            verts[:, :3] = v
            verts[:, 5:9] = 1
            return ttm.Batch(None, ttm.BLEND_OPAQUE, False, 0, layer, verts, np.arange(6))
        wall, decal = street_lod.simplify([quad(0.9, 0), quad(0.8, 1)], 2, keep_layers=True)
        self.assertTrue(np.allclose(wall.verts[:, 1], 0.9))
        self.assertTrue(np.all(decal.verts[:, 1] < -0.05))

    def test_far_seam_corners_stay_on_the_near_neighbour(self):
        # Two chunks share the edge (10.3,0.2,0.4)-(0.1,10.3,0.7). Snapped at spacing 4
        # the far chunk would pull it to (12,0,0)-(0,12,0) and open a gap to the near one.
        def tri(pts):
            verts = np.zeros((3, 9))
            verts[:, :3] = pts
            verts[:, 5:9] = 1
            return ttm.Batch(None, ttm.BLEND_OPAQUE, False, 0, 0, verts, np.arange(3))
        a = [tri([[0, 0, 0], [10.3, 0.2, 0.4], [0.1, 10.3, 0.7]])]
        b = [tri([[10.3, 0.2, 0.4], [20, 0, 0], [0.1, 10.3, 0.7]])]
        seams = street_lod.shared_positions([a, b])
        self.assertEqual(len(seams), 2)
        far = street_lod.simplify(a, 4, fixed=seams)[0].verts[:, :3]
        self.assertTrue(np.allclose(far[1:], b[0].verts[[0, 2], :3]))
        # Without the seam it is the only corner in its voxel, so the mean is itself.
        self.assertTrue(np.allclose(street_lod.simplify(a, 4)[0].verts[1, :3], [10.3, 0.2, 0.4]))

    def test_far_voxel_mean_keeps_flat_ground_flat(self):
        # Four ground corners at z 0.3..0.6 share one 16 unit voxel. A voxel centre put
        # them at z 0; the mean keeps them on the ground, and the merge is the same.
        verts = np.zeros((6, 9))
        verts[:, :3] = [[0, 0, 0.3], [40, 0, 0.3], [3, 2, 0.6], [3, 2, 0.6], [40, 0, 0.3], [40, 40, 0.3]]
        verts[:, 5:9] = 1
        b = ttm.Batch(None, ttm.BLEND_OPAQUE, False, 0, 0, verts, np.arange(6))
        low = street_lod.simplify([b], 16)
        self.assertEqual(street_lod.count(low), 1)
        self.assertTrue(np.all(low[0].verts[:, 2] > 0.29))

    def test_card_strip_pieces_keep_their_texture(self):
        # One piece of a backdrop strip: 60 units wide, a fifth of the picture in u.
        image = np.full((32, 32, 4), 255, dtype=np.uint8)
        image[:8, :, 3] = 0  # clear sky above the rock
        tri = np.zeros((3, 9))
        tri[:, :3] = [[0, 0, 0], [60, 0, 0], [60, 0, 80]]
        tri[:, 3:5] = [[0.4, 0.5], [0.6, 0.5], [0.6, 1.0]]
        self.assertTrue(bake_street._silhouette_card(image, tri))
        tri[:, 3] = [0.5, 0.55, 0.55]
        tri[:, 4] = [0.5, 0.5, 0.6]
        self.assertFalse(bake_street._silhouette_card(image, tri))

    def test_repeating_hedge_strip_keeps_its_texture(self):
        # Bossbot HQ's hedge: an upright strip repeating 6 times along u, clear above the
        # leaves. It wraps, so it stays a textured card; a clamped texture or a ground-like
        # face does not.
        image = np.full((32, 32, 4), 255, dtype=np.uint8)
        image[:10, :, 3] = 0
        tri = np.zeros((3, 9))
        tri[:, :3] = [[0, 0, 0], [120, 0, 0], [120, 0, 40]]
        tri[:, 3:5] = [[-3.0, 0.0], [3.0, 0.0], [3.0, 1.0]]
        self.assertTrue(bake_street._silhouette_card(image, tri, wrap_u=True))
        self.assertFalse(bake_street._silhouette_card(image, tri))
        tri[:, :3] = [[0, 0, 0], [120, 0, 0], [120, 40, 0]]
        self.assertFalse(bake_street._silhouette_card(image, tri, wrap_u=True))

    def test_far_collapse_averages_merged_colours(self):
        # A staircase of thin steps: lit treads (1.0) and dark risers (0.25). The
        # surviving face takes the mean of what merged into it, not a tread's white.
        verts = []
        for k in range(8):
            y0, z0 = k * 0.5, k * 0.5
            tread = [[0, y0, z0 + .5], [10, y0, z0 + .5], [10, y0 + .5, z0 + .5],
                     [0, y0, z0 + .5], [10, y0 + .5, z0 + .5], [0, y0 + .5, z0 + .5]]
            riser = [[0, y0, z0], [10, y0, z0], [10, y0, z0 + .5],
                     [0, y0, z0], [10, y0, z0 + .5], [0, y0, z0 + .5]]
            for p in tread:
                verts.append(p + [0, 0, 1, 1, 1, 1])
            for p in riser:
                verts.append(p + [0, 0, .25, .25, .25, 1])
        verts = np.array(verts, dtype=np.float64)
        b = ttm.Batch(None, ttm.BLEND_OPAQUE, False, 0, 0, verts, np.arange(len(verts)))
        low = street_lod.simplify([b], 3)
        self.assertGreater(street_lod.count(low), 0)
        shade = low[0].verts[:, 5]
        self.assertTrue(np.all(shade < 0.9) and np.all(shade > 0.3))

    def test_far_drops_blended_shadow_cards(self):
        shadow = np.zeros((32, 32, 4), dtype=np.uint8)
        shadow[8:24, 8:24, 3] = 255
        self.assertTrue(bake_street._shadow_card(shadow))
        decal = shadow.copy()
        decal[10:12, 10:12, :3] = 200  # a window frame is not a shadow
        self.assertFalse(bake_street._shadow_card(decal))
        tri = np.zeros((3, 9))
        tri[:, :3] = [[0, 0, 0], [10, 0, 0], [10, 0, 10]]
        tri[:, 3:5] = [[0, 0], [1, 0], [1, 1]]
        tri[:, 5:9] = 1
        b = ttm.Batch(0, ttm.BLEND_ALPHA, False, 0, 1, tri, np.arange(3))
        colored, kept, extra = bake_street._colorize_far(None, [[b]], images={0: shadow}, details=True)
        self.assertEqual(colored, [[]])
        self.assertEqual(extra, [[]])

    def test_far_ground_lattice_spares_walls(self):
        # A wall corner (shared with an upright face) keeps the fine lattice; ground-only
        # corners collapse on the coarse one.
        verts = np.zeros((6, 9))
        verts[:, :3] = [[0, 0, 0], [30, 0, 0], [1, 1, 0],      # ground, faces up
                        [30, 0, 0], [30, 0, 9], [30, 1.5, 0]]  # upright face at x=30
        verts[:, 5:9] = 1
        b = ttm.Batch(None, ttm.BLEND_OPAQUE, False, 0, 0, verts, np.arange(6))
        low = street_lod.simplify([b], 1, ground=48)
        self.assertEqual(street_lod.count(low), 1)
        self.assertTrue(np.allclose(low[0].verts[:, 2].max(), 9))

    def test_far_body_keeps_walls_and_leaves_ground_coloured(self):
        class Tex:
            WM_repeat = 1
            def getWrapU(self):
                return 1
            getWrapV = getWrapU
        image = np.full((32, 32, 4), 255, dtype=np.uint8)
        wall = np.zeros((3, 9))
        wall[:, :3] = [[0, 0, 0], [4, 0, 4], [0, 0, 4]]
        wall[:, 3:5] = [[0, 0], [1, 1], [0, 1]]
        floor = wall.copy()
        floor[:, :3] = [[0, 0, 0], [20, 0, 0], [0, 20, 0]]
        brim = wall.copy()  # a small up-facing face (a hat brim) is not ground
        brim[:, :3] = [[0, 0, 0], [4, 0, 0], [0, 4, 0]]
        repeats = wall.copy()
        repeats[:, 3:5] *= 1.5
        b = ttm.Batch(0, ttm.BLEND_OPAQUE, False, 0, 0, wall, np.arange(3))
        self.assertTrue(bake_street._far_body([Tex()], image, b, wall))
        self.assertFalse(bake_street._far_body([Tex()], image, b, floor))
        self.assertTrue(bake_street._far_body([Tex()], image, b, brim))
        self.assertFalse(bake_street._far_body([Tex()], image, b, repeats))



class FoldTwoSidedTest(unittest.TestCase):
    def tri(self, z=0.0):
        t = np.zeros((3, 9))
        t[:, :3] = [[0, 0, z], [1, 0, z], [0, 0, z + 1]]
        t[:, 5:9] = 1
        return t

    def test_small_two_sided_folds_with_reversed_copy(self):
        import street_atlas
        one, two = self.tri(), self.tri(5)
        groups = {(0, True): [two.copy()], (0, False): [one.copy()]}
        street_atlas.fold_two_sided(groups, 1)
        self.assertEqual(list(groups), [(0, False)])
        tris = np.concatenate(groups[(0, False)]).reshape(-1, 3, 9)
        self.assertEqual(len(tris), 3)
        self.assertTrue(np.array_equal(tris[1], two))
        self.assertTrue(np.array_equal(tris[2], two[::-1]))  # same face, reversed winding

    def test_large_two_sided_keeps_both_draws(self):
        import street_atlas
        many = np.concatenate([self.tri(i) for i in range(street_atlas.FOLD_TWO_SIDED_MAX + 1)])
        groups = {(0, True): [many], (0, False): [self.tri()]}
        street_atlas.fold_two_sided(groups, 1)
        self.assertEqual(sorted(groups), [(0, False), (0, True)])

    def test_far_mesh_keeps_one_sided_faces_one_sided(self):
        batches = [ttm.Batch(None, ttm.BLEND_OPAQUE, two, 0, 0, self.tri(), np.arange(3)) for two in (True, False)]
        far, _, _ = bake_street._colorize_far([], [batches], details=True)
        self.assertEqual([(bool(b.two_sided), len(b.indices) // 3) for b in far[0]], [(False, 3)])

class StripPageTest(unittest.TestCase):
    class Tex:
        WM_repeat, WM_clamp = 1, 0

        def __init__(self, n):
            self.this, self.n = 10_000 + n, n

        def getWrapU(self):
            return 1

        def getWrapV(self):
            return 0

        def getName(self):
            return f't{self.n}'

    def test_u_repeating_textures_share_one_wrapping_page(self):
        import street_atlas
        tri = np.zeros((3, 9))
        tri[:, :3] = [[0, 0, 0], [10, 0, 0], [0, 0, 2]]
        tri[:, 3:5] = [[0, 0], [5, 0], [0, 1]]
        tri[:, 5:9] = 1
        batches = [ttm.Batch(i, ttm.BLEND_ALPHA_TEST, True, 0, 1, tri.copy(), np.arange(3)) for i in (0, 1)]
        textures, result = street_atlas.build([self.Tex(0), self.Tex(1)], [batches], encode=False, tile=16,
                                              gutter=2, mip_levels=2, standalone_size=32, page=64,
                                              repeat_standalone=True)
        self.assertEqual(len(result[0]), 1)  # one draw
        page = textures[result[0][0].texture]
        self.assertEqual((page.wrap_s, page.wrap_t), (ttm.GPU_REPEAT, ttm.GPU_CLAMP_TO_EDGE))
        v = result[0][0].verts[:, 4]
        self.assertTrue(0 < v.min() and v.max() < 1)
        self.assertEqual(result[0][0].verts[:, 3].max(), 5)  # u keeps its repeats

    def test_pretile_is_decided_by_each_triangles_own_uvs(self):
        # awning_edge: the TTC trolley valance stays inside v 0..1 and is tiled 2x2; the gag
        # shop end cap reaches v 1.33 on the clamped axis and keeps the single copy.
        import street_atlas
        image = np.zeros((64, 64, 4), dtype=np.uint8)
        inside = (np.array([0.0, -0.02]), np.array([10.5, 1.0]))
        outside = (np.array([0.0, -0.02]), np.array([10.5, 1.33]))
        self.assertEqual(street_atlas._pretile_factor(image, ttm.GPU_REPEAT, ttm.GPU_CLAMP_TO_EDGE, inside), 2)
        self.assertEqual(street_atlas._pretile_factor(image, ttm.GPU_REPEAT, ttm.GPU_CLAMP_TO_EDGE, outside), 1)


if __name__ == '__main__':
    unittest.main()
