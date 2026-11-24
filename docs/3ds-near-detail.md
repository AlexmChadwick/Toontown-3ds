# Near-view texture detail on Old / New 3DS

Gate: `tools/fidelity/compare_views.py` (run by `run_gate.sh`). Each near view (`ttc_near`, `street_near`,
`dd_near`) is scored on four columns against the pinned stock Panda render, same mask (x 165-245 excluded):

- (a) masked Laplacian detail >= 1.3x stock and >= 1.0x r11 (491 / 167 / 281)
- (b) Laplacian variance >= 3 px from strong edges >= 1.0x stock
- (c) sigma 1.5 high-pass amplitude <= 1.2x stock
- (d) mean edge overshoot <= 1.5x stock

## Sharpen sweep (HEAD 04560e4, Azahar Old; Old = New frames)

Settings: u120 = r0.7 120% t2 unclamped; c150 = r1.0 150% t0 clamped; c250 = r1.0 250% t0 clamped;
ec6fbb6 = r1.0 200% t0 unclamped (rejected by QA-84 for ringing).

| view | setting | (a) | (b) | (c) | (d) | all |
|---|---|---|---|---|---|---|
| ttc_near | off | 442 (1.09x) | 0.62x | 1.01x | 1.49x | FAIL |
| ttc_near | u120 | 513 (1.26x) | 0.81x | 1.11x | 1.60x | FAIL |
| ttc_near | c150 | 482 (1.18x) | 0.69x | 1.07x | 1.52x | FAIL |
| ttc_near | c250 | 497 (1.22x) | 0.72x | 1.09x | 1.65x | FAIL |
| ttc_near | ec6fbb6 | 711 (1.74x) | 1.28x | 1.35x | 2.38x | FAIL |
| street_near | off | 236 (1.21x) | 0.98x | 1.03x | 0.88x | FAIL |
| street_near | u120 | 324 (1.66x) | 1.59x | 1.22x | 1.14x | FAIL |
| street_near | c150 | 271 (1.39x) | 1.16x | 1.11x | 0.92x | PASS |
| street_near | c250 | 285 (1.46x) | 1.27x | 1.14x | 0.91x | PASS |
| street_near | ec6fbb6 | 502 (2.57x) | 3.08x | 1.57x | 1.49x | FAIL |
| dd_near | off | 258 (1.12x) | 0.66x | 1.03x | 1.11x | FAIL |
| dd_near | u120 | 317 (1.37x) | 0.97x | 1.16x | 1.35x | FAIL |
| dd_near | c150 | 290 (1.25x) | 0.77x | 1.10x | 1.47x | FAIL |
| dd_near | c250 | 300 (1.30x) | 0.79x | 1.12x | 1.45x | FAIL |
| dd_near | ec6fbb6 | 489 (2.12x) | 1.85x | 1.47x | 3.04x | FAIL |

Full sweep and crops: the qa19 lane's `qa-r3/sharpen-sweep.md`.

## QA-20261119-84 ruling (a)

Thresholds are unchanged and nothing is waived.

1. Zone 2100 only is opted into c150 (`bake_street.NEAR_SHARPEN = {2100: (1.0, 150, 0, 1)}`); street_near
   must pass all four columns.
2. `ttc_near` and `dd_near` stay unsharpened. No sharpen setting passes their bar: the source texels are
   the limit, and any sharpening pushes (d) past its ceiling before (a) / (b) clear. They are a
   **KNOWN GAP**, held to floors from the unsharpened rows (`compare_views.KNOWN_GAP`):

   | view | (a) floor | (b) floor | (d) ceiling | (c) |
   |---|---|---|---|---|
   | ttc_near | 442 | 0.61x | 1.50x | <= 1.2x |
   | dd_near | 257 | 0.65x | 1.12x | <= 1.2x |

   The gate prints `KNOWN GAP <view> (a) Nx (b) Nx: source-limited, QA-84 ruling` while the floors hold and
   FAILs when a build drops below any floor, including a build that sharpens one of these zones
   (the (d) ceiling trips). QA ruling of 2026-11-23: measured values are compared raw, never rounded,
   and printed unrounded next to each bar. The bars are the raw box gate values of 2026-11-23 (ttc_near a 442.5, b 0.6173,
   d 1.4938; dd_near a 257.72, b 0.6557, d 1.1110) rounded the safe way: floors down, ceilings up. The
   earlier 258 / 0.62 / 0.66 floors and 1.49 / 1.11 ceilings were those values rounded to the nearest
   step, so they passed only by rounding the measurement too. Where the floors came from:
   the gates lane's `qa/known-gap-provenance.md`.
3. Tests (`tools/fidelity/test_compare_views.py`): the gate fails on sharpening a zone that is not opted in,
   fails street_near below 1.3x, and pins the opt-in to zone 2100 only.
