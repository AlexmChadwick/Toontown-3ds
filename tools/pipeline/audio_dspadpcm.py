"""DSP-ADPCM (GameCube / 3DS "DSPADPCM") encoder and reference decoder.

Frame = 8 bytes = 1 header byte (predictor index << 4 | shift) + 14 nibbles.
Each channel has 8 predictor pairs (c1, c2) in Q11. Decoding one sample:

    pred = c1 * hist0 + c2 * hist1
    out  = clamp16((((nib << shift) << 11) + 1024 + pred) >> 11)

which simplifies exactly to  out = clamp16((nib << shift) + ((pred + 1024) >> 11)).

Encoder (written from the format definition, not from Nintendo's tool):
  1. Coefficients: per 14-sample frame, compute the order-2 autocorrelation
     terms; cluster frames into 8 predictors with a Lloyd/LBG iteration whose
     distortion is the frame's order-2 prediction error energy (the exact
     least-squares centroid is a 2x2 solve). Predictors are clamped inside the
     stability triangle and quantised to Q11.
  2. Frames: greedy closed-loop search. For every frame all 8 predictors x 13
     shifts are tried with the real decoder in the loop (history feedback and
     clamping included) and the candidate with the least squared error wins.

Deterministic: no randomness, float64 only in the coefficient search.
"""
import numpy as np

SPF = 14  # samples per frame
BPF = 8   # bytes per frame
NUM_COEFS = 8
NUM_SHIFTS = 13

# Fixed, deterministic starting predictors (c1, c2): silence/flat, first order,
# second order "smooth", and a spread of resonant ones.
_INIT = np.array([
    (0.0, 0.0), (0.9, 0.0), (1.8, -0.82), (1.95, -0.96),
    (1.5, -0.6), (1.2, -0.3), (0.5, 0.2), (-0.5, 0.0),
], dtype=np.float64)


def _frames(x):
    """x: float64 (N,). Returns X0, X1, X2 of shape (F, 14): sample, hist0, hist1."""
    n = len(x)
    f = (n + SPF - 1) // SPF
    xp = np.zeros(f * SPF + 2)
    xp[2:2 + n] = x
    return (xp[2:].reshape(f, SPF), xp[1:-1].reshape(f, SPF), xp[:-2].reshape(f, SPF))


def _clamp_stable(c):
    c2 = np.clip(c[:, 1], -0.998, 0.998)
    lim = (1.0 - c2) * 0.998  # poles of z^2 - c1 z - c2 inside the unit circle
    c1 = np.clip(c[:, 0], -lim, lim)
    return np.stack([c1, c2], axis=1)


def compute_coefs(samples, iterations=40):
    """samples: int16 array (N,). Returns int list of 16 Q11 coefficients."""
    x = samples.astype(np.float64)
    X0, X1, X2 = _frames(x)
    e0 = (X0 * X0).sum(1)
    r1 = (X0 * X1).sum(1)
    r2 = (X0 * X2).sum(1)
    R11 = (X1 * X1).sum(1)
    R12 = (X1 * X2).sum(1)
    R22 = (X2 * X2).sum(1)
    active = e0 > SPF * 16.0  # ignore near-silence (rms < 4)
    if active.sum() < NUM_COEFS:
        active = e0 > 0
    if not active.any():
        return [0] * 16
    # Unweighted (energy) distortion: total residual energy is what bounds the
    # global SNR. Energy-normalised weighting was measured ~0.1-0.6 dB worse.
    w = np.ones(int(active.sum()))
    e0, r1, r2 = e0[active], r1[active], r2[active]
    R11, R12, R22 = R11[active], R12[active], R22[active]

    def distortion(c):  # (F, K)
        a, b = c[:, 0][None, :], c[:, 1][None, :]
        d = (e0[:, None] - 2 * (a * r1[:, None] + b * r2[:, None])
             + a * a * R11[:, None] + 2 * a * b * R12[:, None] + b * b * R22[:, None])
        return d * w[:, None]

    c = _INIT.copy()
    for _ in range(iterations):
        d = distortion(c)
        assign = d.argmin(1)
        best = d[np.arange(len(d)), assign]
        new = c.copy()
        for k in range(NUM_COEFS):
            m = assign == k
            if not m.any():
                # reseed empty cluster on the worst-predicted frame
                j = int(best.argmax())
                A = np.array([[R11[j], R12[j]], [R12[j], R22[j]]])
                bb = np.array([r1[j], r2[j]])
                best[j] = 0.0
            else:
                ww = w[m]
                A = np.array([[(ww * R11[m]).sum(), (ww * R12[m]).sum()],
                              [(ww * R12[m]).sum(), (ww * R22[m]).sum()]])
                bb = np.array([(ww * r1[m]).sum(), (ww * r2[m]).sum()])
            A = A + np.eye(2) * (1e-9 * np.trace(A) + 1e-12)
            new[k] = np.linalg.solve(A, bb)
        new = _clamp_stable(new)
        if np.allclose(new, c, atol=1e-7):
            c = new
            break
        c = new
    q = np.clip(np.rint(c * 2048.0), -32768, 32767).astype(np.int64)
    return [int(v) for v in q.reshape(-1)]


def encode(channels_pcm, coefs):
    """Encode one or more channels in lock step.

    channels_pcm: list of int16 arrays, all the same length N.
    coefs: list (per channel) of 16 Q11 ints.
    Returns (frame_bytes per channel, decoded int16 per channel,
             headers uint8 (C, F), hist0 (C, F), hist1 (C, F)) where hist0/hist1
    are the decoder state *before* each frame.
    """
    C = len(channels_pcm)
    n = len(channels_pcm[0])
    F = (n + SPF - 1) // SPF
    x = np.zeros((C, F * SPF), dtype=np.int64)
    for c in range(C):
        x[c, :n] = channels_pcm[c]
    co = np.array(coefs, dtype=np.int64).reshape(C, NUM_COEFS, 2)
    KS = NUM_COEFS * NUM_SHIFTS
    c1 = np.repeat(co[:, :, 0], NUM_SHIFTS, axis=1)          # (C, KS)
    c2 = np.repeat(co[:, :, 1], NUM_SHIFTS, axis=1)
    shift = np.tile(np.arange(NUM_SHIFTS, dtype=np.int64), NUM_COEFS)[None, :]
    scale = np.broadcast_to(np.int64(1) << shift, (C, KS))
    two_scale = 2 * scale
    hdr_cand = (np.repeat(np.arange(NUM_COEFS), NUM_SHIFTS) << 4 | np.tile(np.arange(NUM_SHIFTS), NUM_COEFS))

    nibbles = np.zeros((C, F, SPF), dtype=np.int64)
    decoded = np.zeros((C, F * SPF), dtype=np.int64)
    headers = np.zeros((C, F), dtype=np.uint8)
    hist0 = np.zeros((C, F), dtype=np.int64)
    hist1 = np.zeros((C, F), dtype=np.int64)
    h0 = np.zeros(C, dtype=np.int64)
    h1 = np.zeros(C, dtype=np.int64)
    rows = np.arange(C)
    nib_buf = np.empty((SPF, C, KS), dtype=np.int64)
    dec_buf = np.empty((SPF, C, KS), dtype=np.int64)
    for f in range(F):
        hist0[:, f] = h0
        hist1[:, f] = h1
        s = x[:, f * SPF:(f + 1) * SPF]
        H0 = np.broadcast_to(h0[:, None], (C, KS))
        H1 = np.broadcast_to(h1[:, None], (C, KS))
        err = np.zeros((C, KS), dtype=np.int64)
        for i in range(SPF):
            P = (c1 * H0 + c2 * H1 + 1024) >> 11
            # round-half-up of (s - P) / scale
            nb = np.clip((2 * (s[:, i:i + 1] - P) + scale) // two_scale, -8, 7)
            d = np.clip((nb << shift) + P, -32768, 32767)
            e = s[:, i:i + 1] - d
            err += e * e
            nib_buf[i] = nb
            dec_buf[i] = d
            H1 = H0
            H0 = d
        best = err.argmin(1)
        headers[:, f] = hdr_cand[best]
        nibbles[:, f, :] = nib_buf[:, rows, best].T
        decoded[:, f * SPF:(f + 1) * SPF] = dec_buf[:, rows, best].T
        h0 = dec_buf[SPF - 1, rows, best]
        h1 = dec_buf[SPF - 2, rows, best]

    out = []
    for c in range(C):
        nib = (nibbles[c] & 0xF).astype(np.uint8)                 # (F, 14)
        packed = (nib[:, 0::2] << 4) | nib[:, 1::2]                # (F, 7)
        frames = np.concatenate([headers[c][:, None], packed], axis=1)
        out.append(frames.astype(np.uint8).tobytes())
    dec = [decoded[c, :n].astype(np.int16) for c in range(C)]
    return out, dec, headers, hist0, hist1


def decode(data, coefs, num_samples, ps=None, hist0=0, hist1=0):
    """Reference decoder (independent of the encoder's vectorised path).
    data: frame bytes; coefs: 16 Q11 ints. Returns int16 array."""
    cf = [(coefs[2 * k], coefs[2 * k + 1]) for k in range(NUM_COEFS)]
    out = np.zeros(num_samples, dtype=np.int16)
    h0, h1 = hist0, hist1
    n = 0
    mv = memoryview(data)
    for fo in range(0, len(data), BPF):
        if n >= num_samples:
            break
        hb = mv[fo]
        a, b = cf[hb >> 4]
        sh = hb & 0xF
        for j in range(SPF):
            if n >= num_samples:
                break
            byte = mv[fo + 1 + (j >> 1)]
            nib = (byte >> 4) if (j & 1) == 0 else (byte & 0xF)
            if nib >= 8:
                nib -= 16
            v = ((((nib << sh) << 11) + 1024 + a * h0 + b * h1) >> 11)
            if v > 32767:
                v = 32767
            elif v < -32768:
                v = -32768
            out[n] = v
            n += 1
            h1 = h0
            h0 = v
    return out
