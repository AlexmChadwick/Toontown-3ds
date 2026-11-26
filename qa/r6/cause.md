# ttc_near / dd_near near-detail gap: cause (round 6, task 1)

Written 2026-11-24 by the claude-3ds-gates lane on 44a72ef11563679b5a804b74c346b32aae825656.
Frames: the round 4 box gate 1 (source 2307354d79e7e2038d80f940dda26a4c5a33bf76, app sha256
c02a4ff2...2bb8, Old = New byte-identical), pinned stock (STOCK_PIN, TT_RESOURCES d00df01), r11 =
`/workspace/shots/3ds-integ-r11/r11_views_new` (ed9740e). Bake: 2000.tz 3dec9118..., 1000.tz 9cbbae17...
All numbers are the gate's own metrics (`near_views.detail`, `off_edge`, `overshoot`, grey = RGB mean,
mask x 165-245), unrounded. Tools: `qa/r6/summary.py` (split of a metric into regions: each pixel's
share of the variance), `qa/r6/bake_proxy.py` (Panda render of the baked .tz chunks at the gate camera,
stock_detail's lens).

## Gate values (tip)

| view | (a) tip | (a) stock | bar max(1.3x stock, r11) | (b) tip / stock |
|---|---|---|---|---|
| ttc_near | 442.499893 | 407.808092 | 530.150520 | 70.711735 / 114.547666 = 0.617313x |
| dd_near | 257.717437 | 230.795694 | 300.034402 | 34.542576 / 52.681087 = 0.655692x |

## Split by region

| frame | metric | total | awning cell x 280-320 y 0-40 | ground y 160-240 | rest |
|---|---|---|---|---|---|
| ttc stock | (a) | 407.808092 | 13.946048 | 143.885223 | 249.976822 |
| ttc tip | (a) | 442.499893 | 0.905759 | 129.460226 | 312.133908 |
| ttc r11 | (a) | 491.397334 | 123.717966 | 122.411029 | 245.268340 |
| ttc stock | (b) | 114.547666 | 21.487849 | 66.378366 | 26.681451 |
| ttc tip | (b) | 70.711735 | 1.276196 | 45.420335 | 24.015204 |
| dd stock | (a) | 230.795694 | 12.307676 | 64.135081 | 154.352937 |
| dd tip | (a) | 257.717437 | 0.534259 | 65.870021 | 191.313156 |
| dd r11 | (a) | 281.155139 | 73.265617 | 62.124150 | 145.765373 |
| dd stock | (b) | 52.681087 | 18.464989 | 13.113710 | 21.102388 |
| dd tip | (b) | 34.542576 | 0.802057 | 15.080749 | 18.659771 |

(r11 (b) is left out: its edge map differs from the gate's, so the split is not comparable.)

## Cause 1: the r11 bar and a large part of (b) are aliasing on the gag shop awning

- The one 40x40 cell at x 280-320, y 0-40 is the gag shop awning seen edge-on. Stock (Panda, mips to
  1x1) draws it as a red / salmon hatch, r11 as a full-contrast red / yellow checker, the tip as a flat
  band (`crops/awning_cell_stock_r11_tip_6x.png`). The tip's flat band is the AGENTS.md section 4 fix:
  the last stored PICA level of a repeating standalone texture is its mean colour, so the stripes do not
  strobe edge-on.
- That cell is 123.717966 of r11's 491.397334 (ttc) and 73.265617 of 281.155139 (dd). Without it the
  tip beats r11: ttc 441.594134 vs 367.679368 (1.20x), dd 257.183178 vs 207.889522 (1.24x). So the
  r11 bar (491 / 281) is only reachable by bringing the strobe back.
- It is 21.487849 of stock's ttc (b) 114.547666 and 18.464989 of stock's dd (b) 52.681087. Of the dd
  (b) gap (18.138511) it is 17.662932, i.e. 97%.
- Proxy check: the same bake rendered with Panda's own mip chain (level 0 only from the bake) gives
  that cell 29.27 (b), the hatch is back. With the stored chain it gives 2.06. So the cell is decided by
  the mean last mip, not by tile density, ETC1 or the runtime.

## Cause 2: stock samples the hood ground textures with anisotropic filtering

- The ttc ground rows hold 66.378366 of stock's (b) vs 45.420335 on the tip (ttc (b) gap 43.835931:
  ground 20.958031, awning 20.211653, rest 2.666247).
- The cobbles are `toonrockroad_newToontown2` (128 px). The bake holds it at 128 (source 128), ETC1
  level 0 Laplacian variance 402 vs source 355: no loss there. Putting the source JPEG on our geometry
  in the proxy does not change the cobble patch (x 120-165, y 200-240): 255.7 vs stock 472.3.
- The upstream .bam textures carry `anisotropic_degree 4` on exactly the ground set:
  toontown_central.bam `toonrockroad_newToontown2`, `grassTT`, `sidewalk`, `trollly_pave_tile`,
  `fence_stoneWall`; donalds_dock.bam `boardwalk_floor`, `grassDD`. Panda renders them anisotropic.
  Proxy with anisotropic degree 4: cobble patch 475.7 (stock 472.3); with plain trilinear (our path)
  247.9 to 255.7 (`crops/cobble_stock_srctexproxy_bakeproxy_tip_6x.png`).
- PICA200 has no anisotropic filtering. The nearest equivalent is a sharper mip choice on those
  textures only. Proxy, LOD bias on the 7 ground textures: ttc (a) 752.623 -> 768.126 (-0.5) ->
  781.985 (-1.0). dd is unchanged at -0.5 (362.251): its near boardwalk is magnified, not minified,
  and its ground (b) is already above stock (15.080749 vs 13.113710).

## Cause 3: hard alpha-test edges carry the (d) overshoot

- ttc (d) 1.493757x: overshoot edges in the tip are concentrated on the door frame's left edge
  (x 120-160, y 40-200: 40 + 37 edges, sum 12.0, stock 0.9). The frame is a cutout. Stock blends its
  soft alpha fringe; the bake draws it alpha-tested (>0.5, AGENTS.md section 4, `M_dual`), so the
  art's dark outline sits right on a hard edge and reads as overshoot
  (`crops/doorframe_stock_proxy_tip_6x.png`). This is what makes clamped sharpening fail (d) on ttc
  (sweep: c150 1.52x) before (a) and (b) clear.

## What is ruled out

- Azahar draw path: hard polygon edges in the proxy and the Azahar frame are identical pixel for pixel
  (rows 40 / 60 / 100, x 78-96), and off-edge detail is proxy 65.216 vs Azahar 69.453 (ttc), 35.735
  vs 34.325 (dd). The old "Panda proxy scores 2x Azahar" note (qa/known-gap-provenance.md) comes from
  the proxy's flat sky backdrop and hard alpha-test edges, not from texture interiors.
- LOD / draw distance / culling: `plan.txt` keeps every chunk of the cell near on Old and New
  (ttc cells 5 4 1 0 9, dd 3 4 7 0 1, all `0/0`), 58 / 57 draws.
- Fog: near views are inside 110 units (fog start), none applied.
- Lens: the three `_near` steps and stock both use 55 degrees.
- Bake texture size / format: wall, door and crate tiles are on 512 ETC1 / ETC1A4 local pages at 4
  mips; ground standalone textures are at source size. A global LOD bias of +0.5 / +1 / +1.5 on the
  whole bake changes ttc (a) only 724 / 685 / 658 (most near surfaces are magnified).

## What this means for the bar

Without the awning cell and with stock-equal ground, ttc (a) would be about 442.5 - 0.9 + 14 (+ the
ground gain, proxy about +29 at bias -1) = about 485, under 530 (1.3x stock). dd (a) 1.3x stock is
300 against 257.7. 1.3x stock on these two views needs about +20% more Laplacian variance than the
stock render of the same source textures. With source-resolution tiles that can only come from
sharpening (bounded by (c) and (d); (d) is spent on the alpha-test edges) or from aliasing.
Task 2 fixes what is a real fidelity loss (anisotropic ground; the soft cutout fringe if it fits the
draw budget) and reports the bar honestly afterwards.
