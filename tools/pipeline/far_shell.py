"""Bounded quadric shells. Lock UV/colour seams and keep decals/alpha cards intact.

Offline dependency: stock fast-simplification==0.2.0 wheel. No runtime dependency.
"""
import numpy as np
import ttm


def attributes(points, vertices, faces, mapping):
    """Project new vertices to original faces for UV/colour transfer.

    Process in bounded blocks. Seam vertices are border-locked before collapse;
    each batch has just one source material and draw state.
    """
    tris = vertices[faces]
    a, ab, ac = tris[:, 0, :3], tris[:, 1, :3]-tris[:, 0, :3], tris[:, 2, :3]-tris[:, 0, :3]
    aa, bb, cc = (ab*ab).sum(1), (ab*ac).sum(1), (ac*ac).sum(1)
    den = aa*cc-bb*bb
    valid = np.abs(den) > 1e-16
    den = np.where(valid, den, 1)
    result, errors = [], []
    for start in range(0, len(points), 64):
        p = points[start:start+64]
        ap = p[:, None, :]-a
        da, dc = (ap*ab).sum(2), (ap*ac).sum(2)
        v, w = (cc*da-bb*dc)/den, (aa*dc-bb*da)/den
        bary = np.stack((1-v-w, v, w), axis=2)
        # For the tiny off-surface errors allowed here, clamped barycentrics
        # give a conservative projection. Invalid/degenerate faces never win.
        bary = np.maximum(bary, 0)
        bary /= np.maximum(bary.sum(2, keepdims=True), 1e-12)
        q = (bary[..., None]*tris[None, :, :, :3]).sum(2)
        dist = ((q-p[:, None, :])**2).sum(2)
        related = (mapping[faces][None, :, :] == np.arange(start, start+len(p))[:, None, None]).any(2)
        dist[~related] = np.inf
        dist[:, ~valid] = np.inf
        best = dist.argmin(1)
        rows = np.arange(len(p))
        errors.extend(np.sqrt(dist[rows, best]))
        result.append((bary[rows, best, :, None]*tris[best]).sum(1))
    out = np.concatenate(result)
    out[:, :3] = points
    return out, np.asarray(errors)


def component(vertices, faces, ratio):
    import fast_simplification
    if len(faces) < 16:
        return vertices, faces
    points, simplified, collapses = fast_simplification.simplify(vertices[:, :3], faces,
        target_count=max(4, int(len(faces)*ratio)), agg=4, preserve_border=True, return_collapses=True)
    if not len(simplified) or len(simplified) >= len(faces):
        return vertices, faces
    qpoints, qfaces = points, simplified
    points, simplified, mapping = fast_simplification.replay_simplification(vertices[:, :3], faces, collapses)
    if qpoints.shape == points.shape and np.array_equal(qfaces, simplified):
        points = qpoints
    out, error = attributes(points, vertices, faces, mapping)
    lo, hi = vertices[:, :3].min(0), vertices[:, :3].max(0)
    used = points[np.unique(simplified)]
    if (error.max(initial=0) > .75 or np.max(np.abs(used.min(0)-lo)) > .5 or
            np.max(np.abs(used.max(0)-hi)) > .5):
        return vertices, faces
    return out, simplified


def simplify(batch, ratio):
    # Open cards keep their boundaries; decals never enter the simplifier.
    if batch.layer or batch.sign or len(batch.indices) < 48:
        return batch
    expanded = batch.verts[batch.indices]
    _, ids, inv = np.unique(np.round(expanded, 6), axis=0, return_index=True, return_inverse=True)
    vertices = expanded[ids]
    faces = inv.reshape(-1, 3).astype(np.int32)
    parent = np.arange(len(vertices))
    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for a, b, c in faces:
        ra = root(a)
        parent[root(b)] = parent[root(c)] = ra
    groups = {}
    for face in faces:
        groups.setdefault(root(face[0]), []).append(face)
    # A global quadric target spends reductions where they are cheapest. Validate
    # and accept each component independently, so one protected prop cannot veto
    # the reductions on unrelated architecture sharing its material.
    import fast_simplification
    gp, gf, collapses = fast_simplification.simplify(vertices[:, :3], faces,
        target_count=max(4, int(len(faces)*ratio)), agg=4, preserve_border=True, return_collapses=True)
    rp, rf, mapping = fast_simplification.replay_simplification(vertices[:, :3], faces, collapses)
    if gp.shape != rp.shape or not np.array_equal(gf, rf):
        gp, gf = rp, rf
    projected, errors = attributes(gp, vertices, faces, mapping)
    parts, indices, base = [], [], 0
    for fs in groups.values():
        fs = np.array(fs, dtype=np.int32)
        used, local = np.unique(fs, return_inverse=True)
        original = vertices[used]
        local = local.reshape(-1, 3).astype(np.int32)
        vs, out_faces = component(original, local, ratio)
        candidate_ids = np.unique(mapping[used])
        candidate_ids = candidate_ids[candidate_ids >= 0]
        candidate_faces = gf[np.isin(gf, candidate_ids).all(1)]
        if len(candidate_faces) and len(candidate_faces) < len(out_faces):
            active = np.unique(candidate_faces)
            pts = gp[active]
            lo, hi = original[:, :3].min(0), original[:, :3].max(0)
            if (errors[active].max(initial=0) <= .75 and
                    np.max(np.abs(pts.min(0)-lo)) <= .5 and np.max(np.abs(pts.max(0)-hi)) <= .5):
                active, remap = np.unique(candidate_faces, return_inverse=True)
                vs, out_faces = projected[active], remap.reshape(-1, 3)
        parts.append(vs)
        indices.append(out_faces.reshape(-1)+base)
        base += len(vs)
    if sum(len(i) for i in indices) >= len(batch.indices):
        return batch
    return ttm.Batch(batch.texture, batch.blend, batch.two_sided, batch.tint, batch.layer,
                     np.concatenate(parts), np.concatenate(indices), sign=batch.sign)
