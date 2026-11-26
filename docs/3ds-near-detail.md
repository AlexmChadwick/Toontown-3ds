# Near-view texture detail on Old / New 3DS

Gate: `tools/fidelity/compare_views.py` (run by `run_gate.sh`). Each near view (`ttc_near`, `street_near`,
`dd_near`) is scored on four columns against the pinned stock Panda render, same mask (x 165-245 excluded):

- (a) masked Laplacian detail >= 1.3x stock and >= 1.0x r11 (491 / 167 / 281)
- (b) Laplacian variance >= 3 px from strong edges >= 1.0x stock
- (c) sigma 1.5 high-pass amplitude <= 1.2x stock
- (d) mean edge overshoot <= 1.5x stock

QA-20261119-168: on `ttc_near` and `dd_near` only, the gag shop awning cell (x 280-320, y 0-40) is
masked out of every frame for the r11 floor (r11 under the same mask: 375 / 212) and for (b). The
anti-strobe rule keeps that edge-on sliver at its mean last mip, where stock and r11 alias it into a
hatch. The 1.3x stock part of (a) stays unmasked. The gate prints the mask, the cell, and both the
masked and unmasked values.

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

## Historical QA-20261119-84 results

The ruling enabled c150 for zone 2100. It left the two playgrounds unsharpened after the
unclamped 200% filter produced ringing. The box gate recorded these values on
`2307354d79e7e2038d80f940dda26a4c5a33bf76`:

   | view | (a) raw | (a) bar | (b) raw | (d) raw | result |
   |---|---|---|---|---|---|
   | ttc_near | 442.499893 (1.085069x stock 407.808092) | 530.150520 (1.3x stock), 491 (r11) | 0.617313x | 1.493757x | FAIL |
   | dd_near | 257.717437 (1.116648x stock 230.795694) | 300.034402 (1.3x stock), 281 (r11) | 0.655692x | 1.110967x | FAIL |

QA-20261119-145 documented the status as red. The history of the interim regression
bounds is in [the provenance note](../qa/known-gap-provenance.md).

## QA-20261119-168 bake

- Zone 1000 uses radius 1, 400% clamped on near atlas tiles. The boardwalk is magnified
  in the near view. The improvement is in the atlas woodwork and props.
- Zone 2000 uses radius 1, 400% clamped on near atlas tiles, preserving the resampled
  colour within two texels of a cutout edge. It also uses radius 1, 150% clamped on the
  detailed mips of standalone, repeating ground textures with source anisotropy. The
  filter wraps across each tile boundary and keeps the terminal mean mip.
- Both playgrounds exclude the awning textures from the tile filter. Zone 2100 keeps
  its existing c150 profile. The sampler and art formats are shared across profiles.

The gate compares raw values to the four QA thresholds and prints them to six decimals.
QA-168 specifies a 1.5x overshoot ceiling for both playgrounds. It supersedes the
interim regression bounds, including the 1.12x Donald's Dock ceiling taken from
the unsharpened bake.
