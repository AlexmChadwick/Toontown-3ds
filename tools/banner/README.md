# HOME Menu 3D banner (CBMD + BCWAV)

The one-scan CIA and `make cia` install a **stereoscopic HOME Menu banner** built from
original Toontown art, not a flat PNG card.

## What gets built

| Output | Role |
|---|---|
| `banner.cgfx` | 3D scene: sky backdrop plane, `phase_3/maps/toontown-logo` card, dog M-shorts **head mesh** (`tt_a_chr_dgm_shorts_head_250.bam`) |
| `banner.bcwav` | ~2.95 s of `phase_3/audio/bgm/tt_theme.ogg` (22050 Hz stereo, short fades) |
| `banner.bnr` | CBMD + CWAV bundle for `makerom -banner` |
| `banner_preview.png` | 256×128 flat mockup for docs (full GL preview when Panda has a display pipe) |

The scene is exported to glTF, converted with [pycgfx](https://github.com/skyfloogle/pycgfx)
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

Optional: devkitPro `bannertool` on `PATH` — otherwise the script uses `tools/banner/bin/bannertool`.

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

After building, open `tools/release/meta/banner_preview.png`. On a machine with EGL/GL,
`make_banner_scene.py` writes a Panda offscreen render to the same path instead of the
flat composite fallback.
