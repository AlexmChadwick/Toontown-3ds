# All exterior zones, sign lettering and bounded distance LOD

Date: 2026-10-23. Branch: `streets2`. This supersedes the earlier `streets` handoff.
The integrated direct `Model::loadFrom` / `CollisionWorld::loadFrom` paths and shared
`tt::mem::LinearGuard` remain in use. There is no scratch SD file or ZoneLinearGuard.
Only files allowed by AGENT_TASK.md were changed. No Azahar, hardware run or root
`make` is performed in this worktree; the task explicitly prohibits those checks.

## Reproduction and output inventory

```sh
.venv/bin/python -I tools/pipeline/bake_street.py --zone all
# One zone or a selected list, with the same deterministic results:
.venv/bin/python -I tools/pipeline/bake_street.py --zone 2200,5000
.venv/bin/python -I tools/zone_test/run.py
$HOME/devkitpro/devkitARM/bin/arm-none-eabi-g++ \
  -std=gnu++17 -fno-rtti -fno-exceptions -march=armv6k -mtune=mpcore \
  -mfloat-abi=hard -mtp=soft -D__3DS__ -Wall -Wextra \
  -I source -I $HOME/devkitpro/libctru/include -fsyntax-only source/world/zone.cpp
# Match the full-detail and runtime-budgeted views of a zone:
.venv/bin/python -I tools/pipeline/street_preview.py \
  sdmc-out/3ds/toontown3ds/data/zones/2200.tz sdmc-out/previews/streets --lod high --signs
.venv/bin/python -I tools/pipeline/street_preview.py \
  sdmc-out/3ds/toontown3ds/data/zones/2200.tz sdmc-out/previews/streets --lod planned
```

The catalog in `street_catalog.py` covers **25 exteriors**: eight playgrounds and
17 streets. It follows the DNA/storage paths in zones.md and bake_hoods.py. Each
zone writes `<zone>.tz`, `<zone>.pts` and `<zone>.json` into
`sdmc-out/3ds/toontown3ds/data/zones/`. The requested new streets/playgrounds are
included, and 2100/2000 are regenerated with lettering and LOD as well. The `.pts`
files use bake_zone.write_points unchanged: NPC origins, party people, tunnels,
fishing spots, with name/X/Y/Z/heading. All sidecars fit Place's five-field parser
and 39-character point-name limit. Audio is unchanged.

All/list mode starts one isolated Python process per zone, sequentially. This
prevents Panda's dynamic font atlas history from making single-zone and all-zone
outputs differ. Each child uses at most three ETC encoder workers plus its driver;
BLAS/OpenMP are set to one. The seed is 1. No source assets or .venv contents are
written. All dependencies are existing stock Panda3D/numpy wheels. Generated Disney
geometry/font/texture data remains in gitignored SD output; no upstream source or
font file is vendored into the code commit.

## Format v2 and v1 compatibility

`tz.py` is the authoritative little-endian layout specification. Magic stays
`TZ01`; the u16 version distinguishes v1 and v2. The header remains 64 bytes, with
all sections/model blobs aligned to 128 bytes. Floors, PVS, triggers and embedded
TTC/TTM formats are unchanged.

| Section | v1 bytes / entry | v2 bytes / entry | Contents |
|---|---:|---:|---|
| Header | 64 | 64 | Branch zone, counts, offsets, file size |
| Chunk | 80 | 104 | Complete old entry, followed by far-model metadata |
| PVS index | 2 | 2 | Sorted unique chunk indices, including self |
| Tagged floor | 40 | 40 | Three world vertices, owning chunk, server zone |
| Trigger | 156 | 156 | AABB, kind, original name and DNA owner |
| Collision | variable | variable | One unchanged TTC1 blob for precise physics |
| Model | variable | variable | Unchanged TTM1 blobs for selected render geometry |

The extra 24 bytes per v2 chunk are six u32 values: far TTM offset, file bytes,
geometry bytes, texture bytes including mips, draws and triangles. A far blob may
be empty (all six values zero). The original 80-byte part describes the full-detail
model and conservative bounds covering both LODs. Each TTM includes only the atlas
pages/standalone textures used by that chunk/LOD. Runtime holds **one LOD per chunk**;
texture copies across selected models are included in the residency budget.

The writer accepts `version=1`; the host reader and C++ loader accept both versions.
The C++ loader reads old entries at their original 80-byte stride into zeroed
104-byte structs. Legacy files retain their existing full-detail PVS behaviour;
the new 15k guarantee applies to v2. Host tests repack every exterior as v1 and load
it through the actual C++ code, in addition to testing every v2 output.

## Format v3: shared texture pool (2026-10-27)

Every chunk TTM used to carry its own copy of each atlas page it touched, so a page shared
by 30 chunks was stored 30 times (near and far). Across all `.tz` files that was 271 MB of
texture copies for about 12 MB of distinct payloads. TZ v3 introduced a layout that keeps
the v2 layout and stores each distinct payload once in a pool after the last blob; the
header's two former reserved words are the pool offset and size. Embedded TTMs keep their
full texture tables but end after their index data, and each entry's blob-relative
`dataOffset` points forward into the pool, so `Model::loadFrom` is unchanged. Both readers
(`tz.validate`, `zone.cpp`) require every pooled payload to lie inside the pool.
`tz.model_bytes()` rebuilds a standalone TTM for host tools.

The original v3 implementation uploaded a copy per chunk; the runtime residency cache now
shares those uploads too. Existing files convert without a rebake (lossless; v3 -> v2 gives
back the original bytes):

```sh
.venv/bin/python -I tools/pipeline/tz.py sdmc-out/3ds/toontown3ds/data/{zones,estate,level}/*.tz
.venv/bin/python -I tools/pipeline/tz.py --version 2 <file>.tz   # back to v2
```

Sizes on SD: zones 389 -> 131 MB (2100.tz 22.0 -> 6.2 MB), level 68 -> 58 MB.

## Format v4: exact shared-texture planning (2026-10-29)

V4 keeps the v3 payload pool and adds two 256-bit texture-identity sets to each chunk,
one per LOD. A small `TPR1` directory at the start of the pool records each identity's
0x80-aligned linear-allocation cost. The runtime ORs the selected models' fixed-size sets
and sums each unique cost once. Near/far selection therefore uses the same shared-page
semantics as residency without probing scattered TTM texture tables during zone entry.
The cache remains the final hard 8 MiB admission gate. V1-v3 files remain readable;
new bakes and `tz.py` repacks default to v4.

## Geometry reduction and runtime selection

Near geometry is preserved, apart from the pre-existing quantization/atlasing and
UV clipping. Far LOD does two things:

- Removes overlaid decal layers, including window details and sign letters.
- Snaps world positions to a common voxel lattice, then removes collapsed and
  duplicate triangles. UVs/vertex colours remain per corner, preserving material
  seams. Adjacent chunks use the same lattice. Full-precision collision and tagged
  floor triangles are never simplified.

The baker selects the smallest spacing from 0, .25, .5, .75, 1, 1.5, 2, 3, 4, 6,
8 units for which **every** all-far PVS fits 12k triangles and 150 draws. The actual
spacing and maximum displacement bound (`sqrt(3)*spacing/2`) are recorded per zone.
Playground grid PVS includes the 120-unit distance, 30-unit camera orbit allowance
and an additional eight units for possible far-mesh displacement. Streets retain
the exact authored DNA PVS lists.

For v2, Zone::update starts with all far models in the PVS. It considers chunks in
stable nearest-first order and upgrades those within 65 units only if the complete
selected set remains within all four caps:

- 15,000 world triangles
- 150 world draws
- 6 MiB geometry
- 8 MiB textures, including every resident copy and mip

This is a bound on the **entire selected PVS before frustum/distance culling**. No
PVS cell is discarded to fake a passing triangle count. Collision and network
interests always describe the complete authored visible set. Far baseline validity
is checked for every cell by both writer and runtime; each upgrade rechecks all
caps. Consequently the bound does not depend on sampling particular camera angles.

The selected plan is reconsidered on cell changes or after four units of movement,
limiting SD churn. Stable updates and selection use fixed arrays and no allocation.
An old LOD normally stays visible until its replacement is ready. If temporary double
residency would exceed a hard geometry or conservative texture cap, it is GPU-fenced and
retired before the replacement is queued, avoiding a loader wait that can never resolve.
TZ v3/v4 shared pages are reference counted, freed as soon as the last model releases them,
and admitted against the exact unique full-mip-chain byte total; walking through PVS cells
therefore cannot accumulate the entire zone texture pool. Model/Anim's existing shared
allocator lock is never held around a Model::loadFrom call. A failed load clears partial
residency, reports Zone::error(), and allows retry. Initial no-floor arrivals still use
the integrated nearest-cell fallback; later misses preserve the current set.

These limits cover world meshes. Toons, NPCs and other actors still add work; an
application targeting 15k for the complete frame should reserve an actor allowance
by lowering the world triangle cap and far target together, followed by rebaking.
Old 3DS timing and SD transition latency are unmeasured. Streaming remains synchronous.

## Measured before/after budgets

Each before count is the worst all-near PVS **with the newly implemented sign text**.
After columns are the maxima observed over up to eight floor centroids per cell,
plus bounds-centre samples for cells without floor. The runtime checks above enforce
15k/150/6 MiB/8 MiB at every other point as well. Columns can peak at different cells.
Numbers count packed geometry/texture payload, not allocator bookkeeping, regular
heap metadata/collision, render targets or actor resources.

The `.json` reports retain legacy `whole`, `worst` and `visible_sets` as all-near
statistics for comparison. Use `before`, `low`, and `planned_worst` for the new
before/far/runtime measurements. An all-near `worst` over 15k is expected; it is not
the simultaneously resident/drawn v2 plan.

| Zone | Before draws | Before tris | After draws | After tris | Geometry bytes | Texture bytes | Far grid units |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1000 | 78 | 15,220 | 76 | 15,000 | 810,000 | 1,990,304 | 0.5 |
| 1100 | 88 | 21,382 | 66 | 14,997 | 809,838 | 1,889,344 | 0.5 |
| 1200 | 86 | 21,509 | 64 | 14,868 | 802,872 | 1,747,456 | 0.5 |
| 1300 | 89 | 29,549 | 60 | 14,934 | 806,436 | 1,780,096 | 0.75 |
| 2000 | 131 | 20,764 | 117 | 14,862 | 802,548 | 2,967,200 | 1.5 |
| 2100 | 132 | 27,675 | 101 | 14,964 | 808,056 | 2,806,848 | 0.75 |
| 2200 | 106 | 27,025 | 84 | 14,999 | 809,946 | 2,719,424 | 0.75 |
| 2300 | 108 | 33,414 | 89 | 14,984 | 809,136 | 2,937,984 | 1 |
| 3000 | 95 | 13,614 | 88 | 13,385 | 722,790 | 2,648,320 | 0.25 |
| 3100 | 104 | 27,502 | 70 | 14,966 | 808,164 | 2,009,472 | 0.75 |
| 3200 | 111 | 26,622 | 80 | 14,988 | 809,352 | 2,097,024 | 0.75 |
| 3300 | 96 | 19,339 | 85 | 14,942 | 806,868 | 2,282,656 | 0.25 |
| 4000 | 71 | 14,008 | 71 | 14,003 | 756,162 | 1,769,120 | 0.25 |
| 4100 | 88 | 23,267 | 66 | 14,879 | 803,466 | 1,780,160 | 0.5 |
| 4200 | 124 | 28,278 | 80 | 14,993 | 809,622 | 1,878,592 | 0.75 |
| 4300 | 93 | 28,142 | 74 | 14,991 | 809,514 | 1,813,056 | 0.75 |
| 5000 | 73 | 18,639 | 63 | 14,813 | 799,902 | 1,545,408 | 1 |
| 5100 | 95 | 22,677 | 74 | 14,992 | 809,568 | 1,878,592 | 0.75 |
| 5200 | 116 | 30,739 | 88 | 14,990 | 809,460 | 2,468,416 | 1.5 |
| 5300 | 89 | 22,560 | 70 | 14,986 | 809,244 | 2,227,968 | 0.75 |
| 6000 | 59 | 9,173 | 57 | 9,152 | 494,208 | 1,578,240 | 0 |
| 8000 | 57 | 10,615 | 57 | 10,615 | 573,210 | 1,750,176 | 0 |
| 9000 | 84 | 12,833 | 84 | 12,833 | 692,982 | 2,260,608 | 0 |
| 9100 | 97 | 19,383 | 73 | 14,989 | 809,406 | 1,703,744 | 0.25 |
| 9200 | 98 | 24,760 | 74 | 14,968 | 808,272 | 2,249,792 | 0.75 |

Measured maxima: 117 draws, 15,000 triangles, 810,000 geometry bytes and
2,967,200 texture bytes (2.83 MiB). All-far baselines peak at 11,947 triangles
and 98 draws. The largest far-lattice displacement bound is 1.30 units.

## Sign lettering

`sign_text.py` independently implements layout from the behaviour of the provided
libtoontown DNASignBaseline/DNASignText reference and the resource DNA. No unlicensed
implementation text was copied. assemble.py now dispatches every baseline to that
helper, including mixed text/graphic baselines and circular graphic baselines.

- Fonts come from loaded storage `store_font` entries, with extensionless static
  font models resolved to `.bam`; item font codes can override the baseline font.
  Panda generates the glyph meshes offline. Static and TTF fonts are supported.
- Line advancement uses scaled item width, cumulative kern, alternating stumble,
  alternating stomp and alternating roll/wiggle, then centres the completed run.
- Width/height form elliptical baselines; indent is angular there. Inner baselines
  reverse item order. Chord widths advance the curved cursor and centre the arc.
  Degenerate radii are handled without NaNs.
- `c` uppercases text, `b` enlarges word initials by 1.5 in X/Z, and `d` adds the
  font shadow with scaled baseline colour. Item positions/scales/rotations and
  explicit colours override baseline defaults. Spaces still advance the cursor.
- Generated text inherits decal layering, with a small separation from its board.
  Full font pages remain standalone textures at up to 256px, avoiding the 32px
  prop-atlas reduction that would erase small letters.
- **Alpha-mask normalization:** Panda stores alpha-only font textures as grayscale
  PNM images. The wrapper copies that grayscale into alpha and supplies white RGB
  before A8 encoding/atlasing. A regression test checks both transparent and opaque
  glyph pixels. Decoded close-ups verify real glyph silhouettes instead of quads.

The runtime sees ordinary TTM meshes/textures; no text/font library is added to the
3DS build. Per-zone sign counts, preview camera positions and diagnostics are in
JSON. The completed set includes 13,709 letters across 1,258 baselines. Distant
letters intentionally disappear with the other decal details in far LOD.

Atlases otherwise retain the existing 256px ETC pages, 32px prop tiles, four-pixel
gutters and no cross-tile mips. Repeated UVs are clipped at texture boundaries.
Burrrgh terrain additionally needs clipping into <=16-repeat UV pieces before
int16 packing; subtracting whole repeats preserves its tiling, unlike clamping.
The UV regression test checks geometric area and packed coordinate range.

## Visual checks and preview paths

Previews decode the actual embedded TTM bytes: quantized positions, UVs, vertex
colours and PICA textures. Shared decoded texture pages are cached and geometry is
filled with numpy arrays to keep full-zone comparisons fast. No source scene is
substituted. `--lod high`, `--lod low` and `--lod planned` allow matched views;
`--signs` adds close-ups from generated baseline bounds. Ground planned views use
the same PVS/LOD planner as the runtime. Overhead views show the complete zone for
layout inspection (high unless explicitly `--lod low`).

Matched views and sign close-ups were generated for streets 2200, 5200 and 9200,
and playgrounds 1000, 3000 and 5000. Representative paths:

- `sdmc-out/previews/streets/2200_high_sign_0.png`: curved, coloured glyphs read
  “Spaghetti and Goofballs / Italian Cuisine”, with the actual baked alpha masks.
- `sdmc-out/previews/streets/5200_high_cell_5201_0.png` and
  `5200_planned_cell_5201_0.png`: near shop signs, building outlines and props.
- `sdmc-out/previews/streets/5200_low_cell_5201_0.png`: matched all-far comparison;
  window/letter decals disappear, and small curved props become visibly coarser.
- `sdmc-out/previews/streets/9200_planned_sign_0.png`: curved florist lettering.
- `sdmc-out/previews/playgrounds/1000_planned_top.png`: full dock layout.
- `sdmc-out/previews/playgrounds/3000_planned_top.png`: snowy terrain and preserved
  repeated sidewalk texture, including the oversized-UV fix.
- `sdmc-out/previews/playgrounds/5000_planned_top.png`: garden layout.

Ground cameras choose large floor triangles and unobstructed camera rays; central
playground views avoid low underwater floor samples. Overhead and several ground/
close-up views were inspected, including the explicit full-detail/all-far street
comparison. Images remain gitignored. The preview tool also emits matching high
and planned close-ups and additional ground cameras alongside these paths.

At 960x720, prop textures are visibly coarse because the 3DS assets are deliberately
small. Near sign glyphs remain readable; the far mesh removes letters/windows and
makes curved props coarser. The recorded displacement bounds make that visual cost
explicit. LOD changes are discrete, with no morph/fade. The selected PVS can still
show chunks beyond the nominal far distance when a large chunk straddles it; all
of their triangles are included in the budget.

## Verification record

2026-10-23, macOS host, branch streets2:

- All 25 v2 zone files pass strict chunk/PVS/floor/model/texture/collision checks.
  Every all-far visible set passes the four runtime budgets. All 25 `.pts` anchor
  files exist and conform to Place's parser.
- The five unit tests pass: byte layouts, repeat/clamp atlas area and UV bounds,
  oversized terrain-repeat splitting, line/curved sign layout with flags, and
  alpha-only font-mask conversion.
- The real zone.cpp/collision.cpp compile on the host with C++17, no RTTI/exceptions,
  `-Wall -Wextra -Werror`, AddressSanitizer and UndefinedBehaviorSanitizer. Both v2
  files and v1 compatibility repacks pass for all 25 zones: **738 floor samples per
  format across 734 floor cells**, PVS transitions, callbacks, initial nearest-cell
  fallback, no-floor retention, selected geometry/texture/triangle/draw budgets,
  duplicate-update behaviour and **1,507 trigger records per format**.
- **250 malformed v2 fixtures** are rejected: short header, wrong version, bad PVS,
  floor owner, memory metric, near-model offset/magic, trigger kind, far-model
  offset and triangle metric. Invalid paths leave the zone unloaded.
- The required devkitARM ARMv6K C++17 syntax check completes with **zero warnings**.
- Two complete final bakes compare byte-identical across **all 75 files**: 25 TZs,
  25 anchor sidecars and 25 JSON reports, using isolated per-zone processes.
- Decoded Panda offscreen previews pass the visual checks described above. No
  source-scene substitution is used for the preview meshes or textures.

All source/format changes were verified with host tools; no emulator/hardware FPS,
GPU time, VRAM or process heap/linear measurements are claimed. Task instructions
forbid Azahar/root make and forbid edits to docs/test-log.md/AGENTS.md here. The main
agent should add an emulator/hardware test-log entry when integrating.

## Runtime integration and remaining work

No changes to main.cpp, Place, Renderer, Model, net or the Makefile are needed for
these exteriors: the integrated Place already prefers branch-number `.tz` files,
loads sidecar anchors, uses Zone collision and forwards Zone cell changes. Merge
this worktree's Zone loader **before** installing the v2 SD files; old binaries only
accept v1. Never reintroduce the obsolete scratch-file loader or allocator guard
from the earlier handoff. Retain the existing LinearGuard inside Model/Anim.

The only main-checkout follow-ups are documentation/device verification: update
AGENTS.md for all exterior zones, v2 LOD/sign decisions and the new measured budget
table; add device results to docs/test-log.md. Place's TTC anchor resolver currently
uses its RomFS `.pts` special case even when 2000.tz comes from SD. The generated
2000.pts is compatible; optionally prefer the adjacent SD sidecar in Place::resolve
before the existing RomFS fallback if TTC anchors are changed in future.

Existing unresolved optional prop codes remain listed verbatim in each JSON. They
are absent models/aliases in the current storage resolver, not silently invented
geometry; examples include DD_barrel in 9200, TT mailbox/hydrant/trashcan aliases,
BR roof/billboard props and a DG neighbourhood sign. There are no unresolved font
codes and no skipped text baselines in these bakes. Doors/tunnel volumes, full
collision and anchor extraction remain intact. This work does not add interiors,
animated boats/water, new gameplay, extra music, stereo polish or public-server
support. No push or merge is performed.

## Bakefix assets and budgets — 2026-10-24

Reproduce the seven zones and all moving props (from this worktree root):

```sh
.venv/bin/python -I -B tools/pipeline/bake_zone_fixes.py --resources ref/ref/resources --baseline /Users/alexchadwick/toontown-3ds/sdmc-out/3ds/toontown3ds/data/zones
.venv/bin/python -I -B tools/zone_test/run.py --zones=1000,2000,3000,4000,5000,9000,12000
.venv/bin/python -I -B tools/zone_test/bakefix_checks.py --resources "$PWD/ref/ref/resources" --out "$PWD/sdmc-out/3ds/toontown3ds/data/zones" --baseline /Users/alexchadwick/toontown-3ds/sdmc-out/3ds/toontown3ds/data/zones
```

The driver serializes zones, disables model/bytecode caches, uses three ETC workers
and single-thread numerical libraries, and keeps scratch under its output directory.
No source format, runtime file, shared reference or virtual environment is written.
The playgrounds retain grid 128/distance 120; DD retains `--separate-boat`.
Cashbot changes to grid 192/distance 120 for conservative PVS (see `coghq.md`).
These are conservative **static-zone planned maxima**, each metric maximized
independently over floor samples. Byte counts are resident bytes, not file size.

| Zone | Triangles before -> after | Draws before -> after | Geometry bytes before -> after | Texture bytes before -> after | New TZ bytes |
|---|---:|---:|---:|---:|---:|
| 1000 | 14,735 -> 12,340 | 77 -> 80 | 795,690 -> 666,360 | 2,012,128 -> 2,077,664 | 4,726,144 |
| 2000 | 14,862 -> 14,889 | 117 -> 120 | 802,548 -> 804,006 | 2,967,200 -> 3,076,384 | 7,558,400 |
| 3000 | 13,385 -> 11,745 | 88 -> 87 | 722,790 -> 634,230 | 2,648,320 -> 2,582,784 | 5,875,584 |
| 4000 | 14,003 -> 12,253 | 71 -> 72 | 756,162 -> 661,662 | 1,769,120 -> 1,769,120 | 4,195,968 |
| 5000 | 14,813 -> 14,937 | 63 -> 68 | 799,902 -> 806,598 | 1,545,408 -> 1,720,128 | 4,772,224 |
| 9000 | 12,833 -> 11,080 | 84 -> 86 | 692,982 -> 598,320 | 2,260,608 -> 2,391,680 | 5,139,968 |
| 12000 | 6,382 -> 14,554 | 57 -> 105 | 344,628 -> 785,916 | 652,288 -> 1,404,992 | 8,433,152 |

All static zones pass 15k triangles, 150 draws, 6 MiB geometry and 8 MiB textures.
Removing the car can change atlas packing and permit different near upgrades;
planned maxima therefore need not decrease monotonically. The static platform
and boarding sphere remain; mover collision does not remain as a static ghost.

### Runtime assets and frame reservations

Assets are under `sdmc-out/3ds/toontown3ds/data/zones/` in this worktree. Copy the
seven `<zone>.tz/.pts/.json` triplets together. Additional assets:

- `{1000,2000,3000,4000,5000,9000}_trolley.ttm`, plus optional matching `.ttc`.
- `1000_pier_{east,west}.ttm` and matching `.ttc`.
- `<hood>_moving.json` for the six trolley hoods (offline transform/budget handoff;
  runtime does not need a JSON parser). `bakefix_budgets.json` and
  `bakefix_checks.json` are verification reports, not game assets.

Read `trolley_station` from every hood's `.pts`, and `pier_east`/`pier_west` from
1000. See `trolley.md` for the essential car H=90 transform and `boat.md` for the
DNA-adjusted pier pivots, full pitch collision and synchronized timing. Existing
boat assets/path tables are retained and are not regenerated here.

**Main-agent runtime work:** reserve moving props before planning zone LOD or
loading their buffers. All six cars cost 1,421 triangles / 14 draws / 171,406 geometry
bytes / 130,976 texture bytes **per loaded hood**, not six simultaneous copies.
DD's two piers add 376 triangles / 6 draws / 10,128 geometry bytes / 49,024 texture
bytes (two TTC blobs total 2,240 heap bytes, plus loader/query overhead).
The static-zone planner must accept reduced limits or equivalent nearest-upgrade
suppression: at most **13,579 triangles / 136 draws** while a trolley is drawn,
or **13,203 / 130** for DD trolley+piers; reduce geometry/texture limits by the
above resident bytes as well. Account for the existing boat, Toons and other
actors separately. The default planner's 15k limit alone is not a whole-frame
budget: TTC/DG static maxima plus the new car would exceed it. The far baselines
remain below 12k; these reservations can be satisfied by declining near upgrades.
Do not simply draw movers on top of a fully spent static-zone budget.

Only pipeline/test/documentation changes are committed; ignored outputs must be
copied from this worktree (or regenerated). All seven real zones passed the host
suite; eight moving meshes were compared to authored geometry and anchor frames.
An independent repeat bake produced **43 byte-identical files** (seven zone triplets,
eight TTM/TTC pairs and six moving-prop metadata files). No make, Azahar, push or
merge was performed.
