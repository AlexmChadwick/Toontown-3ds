# HOME Menu 3D banner (CBMD + BCWAV)

The one-scan CIA and `make cia` install a **stereoscopic HOME Menu banner** built from
original Toontown art, not a flat PNG card.

## What gets built

| Output | Role |
|---|---|
| `banner.cgfx` | 3D scene: TTC backdrop plane, `toontown-logo` card, long-dog bust from `Toon.setDNA` |
| `banner.bcwav` | ~2.95 s of `phase_3/audio/bgm/tt_theme.ogg` (22050 Hz stereo, short fades) |
| `banner.bnr` | CBMD + CWAV bundle for `makerom -banner` |
| `banner_preview.png` | 256×128 mono render of the CGFX scene (Panda3D + Xvfb + llvmpipe) |
| `banner_preview_stereo.png` | Left/right eye pair (512×128) |
| `banner_preview_anaglyph.png` | Red/cyan anaglyph for depth check |

The scene is three unlit textured planes at different depths (`scene_common.py`), exported to glTF,
converted with [pycgfx](https://github.com/skyfloogle/pycgfx)
(under `third_party/pycgfx`, cloned on first run), then packed with
[3ds-bannertool](https://github.com/carstene1ns/3ds-bannertool) (`bin/bannertool`, built on first run).

CGFX must stay **≤ 512 KiB**; textures are downscaled in `gltf_export.py`.

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
- **`meta/icon.png` is unchanged** (48×48 SMDH icon only).

## Preview

`build.sh` renders the **same scene** as the CGFX export via `render_preview.py` (2:1 frustum,
banner camera, software GL). Outputs mono, stereo pair, and anaglyph PNGs under `OUT_DIR`.

There is no flat composited fallback and no debug text overlay.
