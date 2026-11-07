# Render overhaul — 2026-10-30

> **Update (render/overhaul-1030 `a4e64a9`):** this PR's build was later run in Azahar (Old and New 3DS,
> 8 view spots, online stereo and offline soaks); the after shots, compare sheets, soak logs and the
> near-chunk LOD fix are in `/workspace/shots/3ds-render-1030/REPORT.md` and `docs/test-log.md`.
> The "After shots were not captured" section below describes this PR's own run only.

Branch `cursor/render-quality-2097`, based on `render/overhaul-1030` at `5db9143`.
Not merged. No force-push.

## What was wrong

Close-up blur on streamed zones comes from the street atlas: 32px tiles, one mip,
so the GPU's linear filter has nothing sharp to sample. Filtering in
`Model::loadTexture` was already linear, and trilinear when a mip chain exists.
The RomFS playground mesh was already 128px with mips. The views that look soft
are the SD `.tz` zones.

Draw distance stopped at 120 units in `Place::draw`, with the far plane at 300
and no outdoor fog, so the world ended in a hard edge. High LOD only reached
about 65 units.

Sky through the TTC ground is the PICA200 clipper dropping triangles that cross
the near plane. `romfs/baked/ttc_sz.ttm` had a flat ground edge of 608.8 units
(411 upward triangles longer than 20).

The before capture (Azahar, Old 3DS, build `5db9143`) was already at 29.9 fps
with about 3 ms of CPU in a 33 ms frame, 32–97 draws and 5k–11.5k triangles,
about 16 MB linear free and 4.2 MB VRAM free. Quality was limited by those
settings.

## What changed

- Near streamed geometry uses 64px atlas tiles, an 8px gutter and three mips
  filtered per tile so gutters do not bleed. Upward faces with an edge of at
  least 8 keep a standalone 256px texture. Cog HQ bakes still use the old 32px
  single-mip default.
- The far LOD is a voxel-simplified mesh whose corners sample the source
  texture into vertex colour. Those chunks store no texture pages, so a
  playground-sized PVS fits the 8 MiB texture copy budget. Fog hides the
  swap from the sharp mesh. The far plane sits past the fog, so the sky is
  the fog colour.
- `source/render/quality.h` picks the profile from `APT_CheckNew3DS`. Old 3DS:
  far 520, draw 440, high LOD to 160 (keep 185), fog 110–400. New 3DS: far
  900, draw 740, high LOD to 260 (keep 290), fog 180–680. `osSetSpeedupEnable`
  is not called. A high-LOD upgrade still stops at 15k triangles, 150 draws,
  6 MiB geometry and 8 MiB textures. Stereo uses the same far plane and fog
  LUT for both eyes.
- Donald's Dock white fog and the underwater fog are unchanged. Outdoor sky
  fog is applied only when fog is off, so water keeps its own colour. Water
  draw order and camera collision were not edited.
- The baker bisects upward triangles (normal Z ≥ 0.5) until every edge is at
  most 20. Collision is not split. `romfs/baked/ttc_sz.ttm` was rewritten in
  place: same textures, same position scale and offset, longest ground edge
  19.99. That fallback mesh is now 16,649 triangles because the whole
  playground is one model. The streamed zones are the budgeted path.

## Budgets (host bakes, not frame times)

Far rows are the all-far PVS. Planned rows are the resident maximum after
nearest-first high-LOD upgrades, sampling both the Old (160) and New (260)
radii. All three stay inside 150 draws, 15k triangles, 6 MiB geometry and
8 MiB textures. VRAM is unchanged: world textures stay in linear memory.

| Zone | Far draws | Far tris | Far geom | Far tex | Planned draws | Planned tris | Planned geom | Planned tex |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 2000 TTC | 68 | 11,525 | 622 KB | 0 | 150 | 14,947 | 788 KB | 4.19 MB |
| 2100 Silly Street | 84 | 10,049 | 530 KB | 0 | 150 | 14,854 | 783 KB | 4.27 MB |
| 1000 DD | 35 | 11,175 | 589 KB | 0 | 150 | 13,018 | 686 KB | 4.79 MB |

Before, the same views drew 32–97 batches and 5,096–11,503 triangles and left
about 16 MB linear and 4.2 MB VRAM free at 29.9 fps. The new plan uses the
draw cap (150) and up to about 5 MB more texture than a single old near chunk
set, with the linear headroom the before capture showed (about 16 MB free).
That is still inside the 8 MiB zone-texture budget. Per-view fps after this
change was not measured.

Ground split: TTC 11,636 → 15,590 (411), Silly Street 54,632 → 57,778 (1,209),
DD 9,178 → 11,643 (219). Zone 2000 near edges after quantization are ≤ 19.99.

## Before and after, per view

Before (Azahar, Old 3DS, `5db9143`):

| View | fps | cpu ms | draws | tris | linear free | VRAM free |
|---|---:|---:|---:|---:|---:|---:|
| ttc_near | 29.9 | 3.3 | 82 | 8,473 | 16,201 KB | 4,194 KB |
| ttc_mid | 29.9 | 3.2 | 81 | 11,503 | 16,354 KB | 4,194 KB |
| ttc_far | 29.9 | 3.0 | 68 | 7,895 | 16,405 KB | 4,194 KB |
| ttc_tree | 29.9 | 3.5 | 97 | 10,765 | 16,387 KB | 4,194 KB |
| street_near | 29.9 | 2.9 | 44 | 5,809 | 17,724 KB | 4,194 KB |
| street_far | 29.9 | 2.7 | 32 | 5,096 | 17,724 KB | 4,194 KB |
| dd_near | 29.9 | 3.1 | 71 | 9,293 | 17,398 KB | 4,194 KB |
| dd_far | 29.9 | 3.0 | 69 | 10,269 | 17,398 KB | 4,194 KB |

After shots were not captured. Azahar 2127.0-alpha3 was downloaded and
extracted. `AppRun.wrapped` does not load: it requires Qt 6.8 (`Qt_6.8`) and
FFmpeg 7 (`libavcodec.so.61` and the matching avutil/avformat/swscale
libraries). This VM is Ubuntu 24.04, whose packages are Qt 6.4 and FFmpeg 6
(`libavcodec.so.60`). There is no after PNG, no compare image, and no
10-minute soak.

The new zones are in gitignored `sdmc-out/3ds/toontown3ds/data/zones/`
(`2000.tz`, `2100.tz`, `1000.tz`). A machine with Azahar can rebake with
`python3 tools/pipeline/bake_street.py` and rerun
`AUTOTEST_ONLY=view: tools/dev/azahar_test.sh`.

## Left undone

- Azahar and hardware frame times, including stereo with the slider and a
  New 3DS profile check. The profile is selected at boot and the far plane is
  shared by both eyes; that path was not executed.
- The other playgrounds and streets were not rebaked here. The same baker
  path covers them. `sdmc-out/` is not in the commit.
- Boot toon textures were not rebaked. The source clothes images are already
  small; `bake_toon.py` defaults are now 256 and 128 so a later bake can use
  a larger source. The RomFS dog parts stay at 64 / 32 / 128.
- The RomFS playground fallback is one 16,649-triangle mesh. It can exceed
  the 15k guideline when the whole playground is in frame. The streamed zone
  is what the views use.
