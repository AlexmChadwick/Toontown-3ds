# Platform badge kit

Cartoony Toontown-style platform badges for nametags. Original, hand-written SVG art
(no downloaded logos, no generated images). Source of truth:
`/workspace/toontown/platform-badges-kit/`; Toontown-3ds vendors it at `tools/platform-badges/`.

| id | enum    | file | badge |
|----|---------|------|-------|
| 0  | UNKNOWN | (none) | never drawn |
| 1  | WEB     | `badges/1_web.svg` | cartoon globe |
| 2  | MAC     | `badges/2_mac.svg` | red apple with leaf and bite |
| 3  | IOS     | `badges/3_ios.svg` | phone/tablet with a small apple mark on its screen |
| 4  | ANDROID | `badges/4_android.svg` | green robot head with antennae |
| 5  | WINDOWS | `badges/5_windows.svg` | four-pane window (red/green/blue/yellow) |
| 6  | VR      | `badges/6_vr.svg` | infinity loop on a little headset |
| 7  | N3DS    | `badges/7_n3ds.svg` | open clamshell handheld with two screens |
| 8  | LINUX   | `badges/8_linux.svg` | PC monitor with a `>_` prompt (no penguin) |

Style: 64x64 viewBox, soft drop shadow, thick `#241509` ink outline (9 units = ~1.2 px at
16 px), top-light gradient bevel, white gloss highlight, Toontown palette.

## Regenerate

    python3 -P make_atlas.py            # needs cairosvg + Pillow; no network; deterministic

Outputs (committed):
- `atlas_64.png`, `atlas_32.png`, `atlas_16.png`: one row of 9 square cells, **cell index ==
  enum id**, cell 0 empty. Straight (non-premultiplied) alpha; fully transparent texels are
  RGB 0. Reduced from an 8x render in premultiplied space (no dark fringes).
- `atlas.json`: enum table and per-size `px` rects and `uv` rects (`[u0, v0, u1, v1]`,
  v0 = top row in image order; flip v for bottom-up texture APIs such as Panda/GL).
- `preview.png`: every badge at 64/32/16 on the nametag colour, mock "Flippy" nametags with
  each badge, a same-platform tag without a badge, and the 16 px cells zoomed 4x.

## Using it in a port

- Pick the atlas closest to (and not below) the on-screen name text height; scale the quad
  to the text height (square), place it left of the name with a gap of ~0.25 x height, and
  multiply by the nametag's alpha/fade colour.
- Show it only when `other != UNKNOWN && other != own && toon is not local`.
- 3DS: convert `atlas_16.png` (or a 24 px render via `--out`) into the existing t3x/RGBA4 UI
  atlas; a 144x16 RGBA4 sheet is 4.6 KB.
- Server protocol and integration: `Toontown-3ds/tools/server/platform/README.md`.
