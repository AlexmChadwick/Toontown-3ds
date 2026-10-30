"""Split long ground triangles so the PICA200 clipper cannot drop them.

A triangle whose edge is hundreds of units vanishes when it crosses the near
plane on real hardware (sky shows through the ground). Longest-edge bisection
cuts terrain until every ground edge is at most 20 units, inside the 16-24
window. Walls and props are left alone. UVs and colours are interpolated.
"""
import numpy as np

import ttm

MAX_EDGE = 20.0
MIN_UP = 0.5
MAX_PIECES = 4096


def _edges(tri):
    return np.linalg.norm(np.stack((tri[1, :3] - tri[0, :3], tri[2, :3] - tri[1, :3],
                                    tri[0, :3] - tri[2, :3])), axis=1)


def _up(tri):
    n = np.cross(tri[1, :3] - tri[0, :3], tri[2, :3] - tri[0, :3])
    ln = float(np.linalg.norm(n))
    return n[2] / ln if ln > 1e-8 else 0.0


def split_triangle(tri, max_edge=MAX_EDGE):
    """tri is (3, 9) float. Returns a list of (3, 9) triangles."""
    out = [np.asarray(tri, dtype=np.float64)]
    for _ in range(20):
        nxt = []
        changed = False
        for t in out:
            length = _edges(t)
            k = int(np.argmax(length))
            if length[k] <= max_edge or len(nxt) + 2 > MAX_PIECES:
                nxt.append(t)
                continue
            changed = True
            i0, i1 = k, (k + 1) % 3
            mid = (t[i0] + t[i1]) * 0.5
            a = t.copy()
            b = t.copy()
            a[i1] = mid
            b[i0] = mid
            nxt.append(a)
            nxt.append(b)
        out = nxt
        if not changed:
            break
    return out


def split_under(batches, tri_limit=15000, max_edge=MAX_EDGE, ceiling=64.0):
    """Raise the edge cap until the mesh has at most tri_limit triangles.

    The RomFS playground is one resident mesh, so the whole file has to stay
    under 15k triangles. Streamed zones keep MAX_EDGE: their budget is the
    visible set, not the file. stats['max_edge'] is the cap that fit.
    """
    edge = float(max_edge)
    last = None
    while edge <= ceiling + 1e-6:
        out, stats = split_batches(batches, max_edge=edge)
        stats = dict(stats)
        stats['max_edge'] = edge
        last = (out, stats)
        if stats['triangles_out'] <= tri_limit:
            return out, stats
        edge += 2.0
    raise ValueError(
        f'ground mesh is {last[1]["triangles_out"]} triangles at edge {ceiling:g}, over {tri_limit}')


def split_batches(batches, max_edge=MAX_EDGE, min_up=MIN_UP):
    """Return (batches, stats). Only upward-facing triangles with a long edge are split."""
    stats = {'triangles_in': 0, 'triangles_out': 0, 'split': 0}
    out = []
    for b in batches:
        if b.indices is None or len(b.indices) < 3:
            out.append(b)
            continue
        tris = b.verts[np.asarray(b.indices, dtype=np.int64).reshape(-1, 3)]
        pieces = []
        for tri in tris:
            stats['triangles_in'] += 1
            if _up(tri) >= min_up and float(np.max(_edges(tri))) > max_edge:
                parts = split_triangle(tri, max_edge)
                stats['split'] += 1
            else:
                parts = [tri]
            pieces.extend(parts)
        stats['triangles_out'] += len(pieces)
        if not pieces:
            continue
        verts = np.concatenate(pieces, axis=0)
        slot = getattr(b, 'tex_slot', 0)
        for start in range(0, len(verts), ttm.MAX_BATCH_VERTS):
            vs = verts[start:start + ttm.MAX_BATCH_VERTS]
            nb = ttm.Batch(b.texture, b.blend, b.two_sided, b.tint, b.layer, vs,
                           np.arange(len(vs), dtype=np.int64))
            if slot:
                nb.tex_slot = slot
            out.append(nb)
    return out, stats
