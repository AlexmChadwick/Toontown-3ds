# Near-view texture detail on Old / New 3DS

Gate: `tools/fidelity/compare_views.py` (run by `run_gate.sh`). Each near view (`ttc_near`, `street_near`,
`dd_near`) is scored on four columns against the pinned stock Panda render, same mask (x 165-245 excluded):

- (a) masked Laplacian detail >= 1.3x stock and >= 1.0x r11 (491 / 167 / 281)
- (b) Laplacian variance >= 3 px from strong edges >= 1.0x stock
- (c) sigma 1.5 high-pass amplitude <= 1.2x stock
- (d) mean edge overshoot <= 1.5x stock

## Sharpen sweep (HEAD 04560e4, Azahar Old; Old = New frames)

Settings: u120 = r0.7 120% t2 unclamped; c150 = r1.0 150% t0 clamped; c250 = r1.0 250% t0 clamped;
ec6fbb6 = r1.0 200% t0 unclamped (rejected by QA-84 for ringing). This table is the qa19 lane's sweep as it
was recorded (2 decimals); it is history, not a gate result. Gate results are compared and reported raw
(the table under the ruling below).

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
2. `ttc_near` and `dd_near` stay unsharpened (the 200% mask rang). Unsharpened, they miss the QA-19 near
   bar: (a) at least 1.3x stock at the same lens and at least 1.0x r11, and (b) off-edge at least 1.0x
   stock. QA-20261119-145: the gate is **RED** for both views. QA-84 set no floors, and the cause of the
   gap is not isolated (the gates lane's `qa/known-gap-provenance.md`: Panda renders of the same bake
   score about 2x Azahar, which points at the draw path, not the source texels). Box gate raw values,
   2026-11-23 / 24 on 2307354d79e7e2038d80f940dda26a4c5a33bf76:

   | view | (a) raw | (a) bar | (b) raw | (d) raw | result |
   |---|---|---|---|---|---|
   | ttc_near | 442.499893 (1.085069x stock 407.808092) | 530.150520 (1.3x stock), 491 (r11) | 0.617313x | 1.493757x | FAIL |
   | dd_near | 257.717437 (1.116648x stock 230.795694) | 300.034402 (1.3x stock), 281 (r11) | 0.655692x | 1.110967x | FAIL |

   Both views are scored on the full QA-19 / QA-84 bars like street_near. The `compare_views.KNOWN_GAP`
   floors are **unratified** and do not pass a view; they are only a regression check printed beside
   the result:

   | view | (a) floor | (b) floor | (d) ceiling |
   |---|---|---|---|
   | ttc_near | 442 | 0.61x | 1.50x |
   | dd_near | 257 | 0.65x | 1.12x |

   The gate prints `KNOWN GAP <view>, unratified floors, gate red vs QA-19: (a) raw ... vs floor ... held`
   (or `BROKEN`, with a FAIL prefix, when a build drops below a floor or sharpens one of these zones so
   the (d) ceiling trips). Measured values are compared raw, never rounded, and printed unrounded next to
   each bar (QA ruling of 2026-11-23). The floors are the raw box values above rounded the safe way:
   floors down, ceilings up. This stays red until the cause is isolated and either the QA-19 bar is met
   or Alex rules on lowering it.
3. Tests (`tools/fidelity/test_compare_views.py`): the gate fails on sharpening a zone that is not opted in,
   fails street_near below 1.3x, and pins the opt-in to zone 2100 only.
