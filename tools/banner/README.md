# HOME Menu 3D banner (CBMD + BCWAV)

The one-scan CIA and `make cia` install a **stereoscopic HOME Menu banner** built from
original Toontown art, not a flat PNG card.

## What gets built

| Output | Role |
|---|---|
| `banner.cgfx` | 3D scene: TTC backdrop plane, `toontown-logo` card, long-dog bust from `Toon.setDNA` |
| `banner.bcwav` | 2.9 s or less of `phase_3/audio/bgm/tt_theme.ogg` (32000 Hz, 16-bit stereo PCM, short fade-out) |
| `banner.bnr` | CBMD + CWAV bundle for `makerom -banner` |
| `banner_preview.png` | 400×240 mono render with the HOME Menu camera (5:3, 30° vertical FOV) |
| `banner_preview_stereo.png` | Left/right eye pair (800×240) |
| `banner_preview_anaglyph.png` | Red/cyan anaglyph for depth check (400×240) |

The scene is three unlit textured planes (`scene_common.py`). The logo texture stays
256×256 — that file is the wide wordmark squeezed into a square — and the card is
`LOGO_ASPECT = 1.83 * 4/3` (about 2.44), the loading-screen card on a 4:3 window.
The dog sits lower right, behind the logo. Hills are further back. Exported to glTF,
converted by `cgfx_convert.py` (pycgfx with RGBA8, no per-axis resize, no specular,
Y-axial logo billboard, rigid COMMON animation), then packed with
[3ds-bannertool](https://github.com/carstene1ns/3ds-bannertool) (`bin/bannertool`, built on first run).

CGFX must stay **≤ 512 KiB** decompressed. The logo and the dog are RGBA8; the backdrop
is RGBA4 at 256×128. One mip level: a second level of the logo alone is 64 KiB and the
three textures would pass the cap. pycgfx is cloned under `third_party/pycgfx` on first run.

## Prerequisites

1. **`ref/resources`** submodule (same tree as `tools/pipeline/`):
   ```bash
   git submodule update --init ref/resources
   ```
2. **Python** with `panda3d==1.10.15`, `pillow`, `numpy` (stock wheel is fine for banner bakes).
3. **ffmpeg**
4. **g++** (to compile bannertool once)
5. **Xvfb** + Mesa llvmpipe (for `render_preview.py`; `build.sh` runs `xvfb-run` with `LIBGL_ALWAYS_SOFTWARE=1`)

Optional: devkitPro `bannertool` on `PATH` — otherwise the script uses `tools/banner/bin/bannertool`.

`bake_toon_portrait.py` renders a stock long dog (`dll`, light blue, neutral) with the upstream `Toon` / `ToonDNA` code, in the Make-A-Toon 3/4 pose (cached as `build/banner/toon_portrait.npy`). Requires the `ref/open-toontown` submodule.

## Regenerate

```bash
tools/banner/build.sh
```

Environment:

- `RESOURCES` — override asset root (default `ref/resources`)
- `OUT_DIR` — install dir (default `tools/release/meta`; also copies to `meta/` for `make cia`)
- `SKIP_BANNER_BUILD=1` — `tools/release/build_one_scan_cia.sh` skips regeneration
- `BANNERTOOL`, `PYCGFX_DIR`, `PYTHON` — tool overrides

## CIA wiring

- `tools/release/build_one_scan_cia.sh` runs `tools/banner/build.sh` then copies `banner.bnr`.
- `make cia` depends on `make banner` and uses `meta/banner.bnr`.
- `meta/icon.png` is the opaque 48×48 SMDH icon. `meta/icon-24.png` is the same art at 24×24 (Lanczos). bannertool stores both sizes as RGB565 and builds the small icon from the 48×48 with a 2×2 box filter.
- `makesmdh` sets `visible,allow3d,recordusage,extendedbanner`.

## Preview

`build.sh` renders the **same scene** as the CGFX export via `render_preview.py`. The camera
matches `banner-camera.gltf`: position (0, 1, 44.786), 30° vertical FOV, aspect 5:3,
near 26.5. The mono preview is 400×240. A 256×128 image would squash that view, so it is
not written. Outputs mono, stereo pair, and anaglyph PNGs under `OUT_DIR`.

There is no flat composited fallback and no debug text overlay.
