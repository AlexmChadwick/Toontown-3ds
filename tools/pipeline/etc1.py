"""ETC1 / ETC1A4 encoder and decoder for the PICA200 (vectorized numpy, deterministic).

Block coding follows the Khronos ETC1 spec (OES_compressed_ETC1_RGB8_texture):
individual (4:4) and differential (5+3) base colors, flip bit, two 3-bit table
indices, and a 2-bit selector per texel (msb = negate, lsb = large modifier).

Search (per 4x4 block, per flip, per mode, per sub-block):
  1. candidates = quantized sub-block mean + a +-1 cube + +-2/3 steps along gray;
  2. every candidate x all 8 tables is scored with a closed-form, unclamped error
     (selectors chosen per texel); the best one is refined (base = mean(p - mod))
     and a second +-1 cube is scored around the refined center;
  3. the best few candidates (individual) or valid base/delta pairs (differential,
     plus a forced clamped-delta pair so the mode is always available) are rescored
     exactly with clamping over all 8 tables and per-texel selector search;
  4. the lowest exact error over {flip 0, flip 1} x {individual, differential} wins.

PICA200 layout (cross-checked against Azahar video_core/texture/texture_decode.cpp
+ etc1.cpp and devkitPro tex3ds source/encode.cpp):
  * the texture is a grid of 8x8 tiles, row-major, tile row 0 = memory row 0 = image
    top (same orientation as ttm.tile(): rows stored top row first);
  * each tile holds four 4x4 blocks in Z order: top-left, top-right, bottom-left,
    bottom-right;
  * each block is the spec's 64-bit word stored as a *little-endian* u64
    (byte-reversed relative to the spec's big-endian byte stream);
  * ETC1A4: every block is preceded by a little-endian u64 of 4-bit alphas, texel
    (x, y) in nibble 4*(x*4 + y) (column-major, same order as the selector bits).

Quality is on par with rg_etc1 'medium' (tex3ds' encoder). Encoding is threaded over
independent 512-block chunks (env TT_ETC1_THREADS, default 4); output is
byte-identical for any thread count.
"""
import os
from concurrent.futures import ThreadPoolExecutor

import numpy as np

GPU_ETC1, GPU_ETC1A4 = 0xC, 0xD

# Spec modifier tables (a, b): texel modifiers are +a, +b, -a, -b for selector codes
# 0, 1, 2, 3 (code bit1 = msb/negate, bit0 = lsb/large).
TABLES = np.array([[2, 8], [5, 17], [9, 29], [13, 42],
                   [18, 60], [24, 80], [33, 106], [47, 183]], dtype=np.int32)
MODS = np.stack([TABLES[:, 0], TABLES[:, 1], -TABLES[:, 0], -TABLES[:, 1]], axis=1)  # (8, 4)
_A2, _B2 = (2 * TABLES[:, k][:, None] for k in (0, 1))                            # (8, 1)
_A3, _B3 = (3 * TABLES[:, k][:, None] ** 2 for k in (0, 1))

# Texel index inside a block: i = x * 4 + y (column-major, spec order).
_SUBSETS = {
    0: (np.arange(0, 8), np.arange(8, 16)),                     # flip 0: left | right
    1: (np.array([0, 1, 4, 5, 8, 9, 12, 13]),                   # flip 1: top / bottom
        np.array([2, 3, 6, 7, 10, 11, 14, 15])),
}

_CUBE = np.array([(r, g, b) for r in (-1, 0, 1) for g in (-1, 0, 1) for b in (-1, 0, 1)],
                 dtype=np.int32)
_SEED_OFFSETS = np.concatenate([_CUBE, np.array([[k, k, k] for k in (-3, -2, 2, 3)],
                                                dtype=np.int32)])
_TOP = 4          # candidates (or pairs) rescored exactly per sub-block and mode
_PAIR = 12        # differential: candidates per sub-block considered for base/delta pairs
_CHUNK = 512      # blocks per vectorized pass (bounds memory; unit of threading)
_BIG = 1 << 30    # "invalid" error sentinel (real errors are < 2^23)


def _expand(q, mode):
    """Quantized base color -> 8-bit. mode 0: 4-bit, mode 1: 5-bit."""
    return q * 17 if mode == 0 else (q << 3) | (q >> 2)


def _quantize(c, mode):
    m = 15 if mode == 0 else 31
    return np.clip(np.rint(c * (m / 255.0)), 0, m).astype(np.int32)


class _Sub:
    """Per-sub-block constants for the closed-form error."""

    def __init__(self, px):
        self.px = px                                   # (N, 8, 3) int32
        self.P = px.sum(axis=1, dtype=np.int32)        # (N, 3)
        self.Q = (px * px).sum(axis=(1, 2), dtype=np.int32)
        self.L = px.sum(axis=2, dtype=np.int32)        # (N, 8)
        self.pf = px.astype(np.float32)                # exact: all sums < 2^24
        self.pp = (self.pf * self.pf).sum(-1)          # (N, 8)


def _approx(sub, cq, mode):
    """Unclamped error of quantized candidates cq (N, K, 3), best over tables.

    Per texel, sum_ch (c + m - p)^2 = |c - p|^2 + 3m^2 - 2m d with d = sum(p) - sum(c),
    so the best selector only depends on |d|. Returns (err (N, K), table (N, K)).
    """
    c = _expand(cq, mode)
    t1 = (8 * (c * c).sum(-1, dtype=np.int32) - 2 * (c * sub.P[:, None, :]).sum(-1, dtype=np.int32)
          + sub.Q[:, None])
    x = np.abs(sub.L[:, None, :] - c.sum(-1, dtype=np.int32)[:, :, None])[:, :, None, :]
    t2 = np.minimum(_A3 - _A2 * x, _B3 - _B2 * x).sum(-1, dtype=np.int32)   # (N, K, 8)
    t = t2.argmin(-1)
    err = t1 + np.take_along_axis(t2, t[..., None], -1)[..., 0]
    return err, t


def _refined_center(sub, cq, t, mode):
    """Given one base (N, 3) + table (N,), mean(p - modifier) with unclamped selectors."""
    c = _expand(cq, mode)
    d = sub.L - c.sum(-1, dtype=np.int32)[:, None]                                    # (N, 8)
    a = TABLES[t, 0][:, None]
    b = TABLES[t, 1][:, None]
    mag = np.where(2 * np.abs(d) > 3 * (a + b), b, a)
    m = np.where(d >= 0, mag, -mag)
    return (sub.px - m[:, :, None]).mean(axis=1)


def _candidates(sub, mode):
    """Candidate bases (N, K, 3) with approximate errors (N, K)."""
    mx = 15 if mode == 0 else 31
    q0 = _quantize(sub.px.mean(axis=1), mode)
    c1 = np.clip(q0[:, None, :] + _SEED_OFFSETS[None], 0, mx)
    e1, t1 = _approx(sub, c1, mode)
    best = e1.argmin(-1)
    n = np.arange(len(best))
    qr = _quantize(_refined_center(sub, c1[n, best], t1[n, best], mode), mode)
    c2 = np.clip(qr[:, None, :] + _CUBE[None], 0, mx)
    e2, _ = _approx(sub, c2, mode)
    return np.concatenate([c1, c2], axis=1), np.concatenate([e1, e2], axis=1)


def _exact(sub, cq, mode):
    """Exact (clamped) error for candidates cq (N, R, 3) over all tables + selectors.

    Returns err (N, R), table (N, R), selector codes (N, R, 8).
    """
    c = _expand(cq, mode)
    n, r = c.shape[:2]
    v = np.clip(c[:, :, None, :] + MODS.reshape(1, 1, 32, 1), 0, 255).astype(np.float32)  # N R 32 3
    # |v - p|^2 = |v|^2 - 2 v.p + |p|^2 (float32 is exact here: every term < 2^24)
    vp = np.matmul(v, sub.pf.transpose(0, 2, 1)[:, None])                      # N R 32 8
    e = (v * v).sum(-1)[..., None] - 2 * vp + sub.pp[:, None, None, :]
    e = e.reshape(n, r, 8, 4, 8)
    sel = e.argmin(axis=3)                                                       # N R 8 8
    emin = e.min(axis=3).sum(-1).astype(np.int64)                                # N R 8
    t = emin.argmin(-1)
    err = np.take_along_axis(emin, t[..., None], -1)[..., 0]
    sel = np.take_along_axis(sel, t[:, :, None, None], 2)[:, :, 0, :]
    return err, t, sel


def _pick(err, *arrays):
    """Select along axis 1 the entry with minimal err."""
    k = err.argmin(axis=1)
    n = np.arange(len(k))
    return (err[n, k],) + tuple(a[n, k] for a in arrays)


def _top(err, r):
    """Indices (N, r) of the r smallest entries per row (unordered)."""
    if r >= err.shape[1]:
        return np.broadcast_to(np.arange(err.shape[1]), err.shape).copy()
    return np.argpartition(err, r - 1, axis=1)[:, :r]


def _encode_mode_individual(s0, s1):
    out = []
    for s in (s0, s1):
        cq, ea = _candidates(s, 0)
        idx = _top(ea, _TOP)
        cq = np.take_along_axis(cq, idx[..., None], 1)
        err, t, sel = _exact(s, cq, 0)
        out.append(_pick(err, cq, t, sel))
    return out


def _encode_mode_differential(s0, s1):
    c0, e0 = _candidates(s0, 1)
    c1, e1 = _candidates(s1, 1)
    n = np.arange(len(e0))
    # Pair only each side's best few candidates (+ the forced pairs below).
    i0, i1 = _top(e0, _PAIR), _top(e1, _PAIR)
    c0, e0 = np.take_along_axis(c0, i0[..., None], 1), np.take_along_axis(e0, i0, 1)
    c1, e1 = np.take_along_axis(c1, i1[..., None], 1), np.take_along_axis(e1, i1, 1)
    # Forced pairs: each side's best base with the other clamped into delta range.
    b0, b1 = c0[n, e0.argmin(1)], c1[n, e1.argmin(1)]
    f1 = np.clip(b0 + np.clip(b1 - b0, -4, 3), 0, 31)
    f0 = np.clip(b1 - np.clip(b1 - b0, -4, 3), 0, 31)
    ef1, _ = _approx(s1, f1[:, None], 1)
    ef0, _ = _approx(s0, f0[:, None], 1)
    c0 = np.concatenate([c0, f0[:, None]], 1)
    e0 = np.concatenate([e0, ef0], 1)
    c1 = np.concatenate([c1, f1[:, None]], 1)
    e1 = np.concatenate([e1, ef1], 1)
    k = c0.shape[1]
    delta = c1[:, None, :, :] - c0[:, :, None, :]                       # (N, K, K, 3)
    valid = ((delta >= -4) & (delta <= 3)).all(-1)
    tot = np.where(valid, e0[:, :, None] + e1[:, None, :], _BIG)
    idx = _top(tot.reshape(len(n), -1), _TOP)                           # (N, R)
    i0, i1 = idx // k, idx % k
    q0 = np.take_along_axis(c0, i0[..., None], 1)
    q1 = np.take_along_axis(c1, i1[..., None], 1)
    ok = ((q1 - q0 >= -4) & (q1 - q0 <= 3)).all(-1)                     # (N, R)
    err0, t0, sel0 = _exact(s0, q0, 1)
    err1, t1, sel1 = _exact(s1, q1, 1)
    tot = np.where(ok, err0 + err1, _BIG)
    j = tot.argmin(1)
    g = lambda a: a[n, j]  # noqa: E731
    return [(g(err0), g(q0), g(t0), g(sel0)), (g(err1), g(q1), g(t1), g(sel1))]


def _pack(flip, diff, q0, q1, t0, t1, sel0, sel1):
    """-> (N,) uint64 spec words."""
    flip = flip.astype(np.uint64)
    diff = diff.astype(np.uint64)
    q0 = q0.astype(np.int64)
    q1 = q1.astype(np.int64)
    d = (q1 - q0) & 7
    hi_ind = (q0[:, 0] << 28 | q1[:, 0] << 24 | q0[:, 1] << 20 | q1[:, 1] << 16
              | q0[:, 2] << 12 | q1[:, 2] << 8)
    hi_dif = (q0[:, 0] << 27 | d[:, 0] << 24 | q0[:, 1] << 19 | d[:, 1] << 16
              | q0[:, 2] << 11 | d[:, 2] << 8)
    hi = np.where(diff == 1, hi_dif, hi_ind).astype(np.uint64)
    hi |= t0.astype(np.uint64) << np.uint64(5) | t1.astype(np.uint64) << np.uint64(2)
    hi |= diff << np.uint64(1) | flip
    lo = np.zeros(len(hi), dtype=np.uint64)
    for f in (0, 1):
        m = flip == f
        for sel, idx in zip((sel0, sel1), _SUBSETS[f]):
            for j, i in enumerate(idx):
                code = sel[m, j].astype(np.uint64)
                lo[m] |= ((code >> np.uint64(1)) << np.uint64(16 + i)) | ((code & np.uint64(1)) << np.uint64(i))
    return hi << np.uint64(32) | lo


def _encode_chunk(blocks):
    px = blocks.astype(np.int32)
    best = None
    for flip in (0, 1):
        i0, i1 = _SUBSETS[flip]
        s0, s1 = _Sub(px[:, i0]), _Sub(px[:, i1])
        for diff, fn in ((0, _encode_mode_individual), (1, _encode_mode_differential)):
            (e0, q0, t0, sel0), (e1, q1, t1, sel1) = fn(s0, s1)
            cand = (e0 + e1, np.full(len(px), flip), np.full(len(px), diff),
                    q0, q1, t0, t1, sel0, sel1)
            if best is None:
                best = list(cand)
            else:
                better = cand[0] < best[0]
                for k in range(len(best)):
                    best[k] = np.where(better if best[k].ndim == 1 else better[:, None],
                                       cand[k], best[k])
    return _pack(*best[1:])


def encode_blocks(blocks, threads=None):
    """blocks: (N, 16, 3) uint8, texel i = x*4 + y. Returns (N,) uint64 spec words.

    Chunks are independent, so the result does not depend on the thread count
    (numpy releases the GIL inside the big array ops).
    """
    chunks = [blocks[s:s + _CHUNK] for s in range(0, len(blocks), _CHUNK)]
    threads = threads or min(len(chunks), os.cpu_count() or 1,
                             int(os.environ.get('TT_ETC1_THREADS', '4')))
    if threads <= 1:
        parts = [_encode_chunk(c) for c in chunks]
    else:
        with ThreadPoolExecutor(threads) as ex:
            parts = list(ex.map(_encode_chunk, chunks))
    return np.concatenate(parts) if parts else np.empty(0, dtype=np.uint64)


def decode_blocks(words):
    """(N,) uint64 spec words -> (N, 16, 3) uint8, texel i = x*4 + y."""
    w = words.astype(np.uint64)
    hi = (w >> np.uint64(32)).astype(np.int64)
    lo = (w & np.uint64(0xFFFFFFFF)).astype(np.int64)
    flip = hi & 1
    diff = (hi >> 1) & 1
    t1 = (hi >> 2) & 7
    t0 = (hi >> 5) & 7
    sh = np.array([24, 16, 8])
    # individual
    i0 = ((hi[:, None] >> (sh + 4)) & 15) * 17
    i1 = ((hi[:, None] >> sh) & 15) * 17
    # differential
    b = (hi[:, None] >> (sh + 3)) & 31
    dd = (hi[:, None] >> sh) & 7
    dd = np.where(dd >= 4, dd - 8, dd)
    b1 = b + dd
    d0 = (b << 3) | (b >> 2)
    d1 = (b1 << 3) | (b1 >> 2)
    base0 = np.where(diff[:, None] == 1, d0, i0)
    base1 = np.where(diff[:, None] == 1, d1, i1)
    i = np.arange(16)
    x, y = i // 4, i % 4
    second = np.where(flip[:, None] == 1, y[None] >= 2, x[None] >= 2)          # (N, 16)
    base = np.where(second[..., None], base1[:, None, :], base0[:, None, :])
    table = np.where(second, t1[:, None], t0[:, None])
    msb = (lo[:, None] >> (16 + i)) & 1
    lsb = (lo[:, None] >> i) & 1
    mod = MODS[table, msb * 2 + lsb]
    return np.clip(base + mod[..., None], 0, 255).astype(np.uint8)


def _to_blocks(img):
    """(h, w, c) -> (h/4 * w/4, 16, c) in row-major block order, texel i = x*4 + y."""
    h, w, c = img.shape
    b = img.reshape(h // 4, 4, w // 4, 4, c).transpose(0, 2, 3, 1, 4)   # by bx x y c
    return b.reshape(-1, 16, c)


def _from_blocks(blocks, h, w):
    c = blocks.shape[-1]
    b = blocks.reshape(h // 4, w // 4, 4, 4, c).transpose(0, 3, 1, 2, 4)  # by y bx x c
    return b.reshape(h, w, c)


def _pica_order(h, w):
    """Row-major block indices in PICA order (8x8 tiles row-major, Z order inside)."""
    idx = np.arange((h // 4) * (w // 4)).reshape(h // 8, 2, w // 8, 2)
    return idx.transpose(0, 2, 1, 3).reshape(-1)


def _alpha_words(a_blocks):
    """(N, 16) uint8 alpha -> (N,) uint64, nibble 4*i for texel i = x*4 + y."""
    a4 = ((a_blocks.astype(np.uint32) * 15 + 127) // 255).astype(np.uint64)
    w = np.zeros(len(a4), dtype=np.uint64)
    for i in range(16):
        w |= a4[:, i] << np.uint64(4 * i)
    return w


def encode_levels(levels, alpha):
    """levels: list of (h, w, 4) uint8 RGBA (row 0 = top). Returns one bytes blob
    with every level PICA-tiled, ETC1A4 if alpha else ETC1."""
    blocks, alphas, orders = [], [], []
    for lv in levels:
        h, w = lv.shape[:2]
        assert h % 8 == 0 and w % 8 == 0, (h, w)
        bl = _to_blocks(np.ascontiguousarray(lv))
        orders.append(_pica_order(h, w))
        blocks.append(bl[:, :, :3])
        alphas.append(bl[:, :, 3])
    words = encode_blocks(np.concatenate(blocks))
    out, pos = [], 0
    for bl, a, order in zip(blocks, alphas, orders):
        cw = words[pos:pos + len(bl)][order]
        pos += len(bl)
        if alpha:
            aw = _alpha_words(a)[order]
            out.append(np.stack([aw, cw], axis=1).astype('<u8').tobytes())
        else:
            out.append(cw.astype('<u8').tobytes())
    return b''.join(out)


def decode_level(data, w, h, alpha):
    """PICA ETC1/ETC1A4 bytes of one level -> (h, w, 4) uint8 RGBA, row 0 = top."""
    n = (w // 4) * (h // 4)
    v = np.frombuffer(data[:n * (16 if alpha else 8)], dtype='<u8')
    if alpha:
        v = v.reshape(n, 2)
        aw, cw = v[:, 0], v[:, 1]
    else:
        cw = v
    order = _pica_order(h, w)
    rgb = np.empty((n, 16, 3), dtype=np.uint8)
    rgb[order] = decode_blocks(cw)
    a = np.full((n, 16), 255, dtype=np.uint8)
    if alpha:
        nib = (aw[:, None] >> (np.arange(16, dtype=np.uint64) * np.uint64(4))) & np.uint64(15)
        a[order] = (nib * 17).astype(np.uint8)
    return _from_blocks(np.concatenate([rgb, a[..., None]], axis=2), h, w)


def level_bytes(w, h, alpha):
    return w * h if alpha else w * h // 2
