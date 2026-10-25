# Performance work (perf branch)

This change is host/build verified. Old 3DS targets remain 30 fps, steady main-thread
busy <20 ms, facility drawing <=5 ms, bottom UI <1 ms, and no interactive frame
>100 ms outside a fade/loading screen. **These native targets are not yet measured
or proven.** The task prohibits Azahar; the main agent must run the checks below.
No protocol, DC, baked format, collision approximation or server simulation changed.

## Measurement corrections

All frame owners now separate update, draw, shared-loader publication, GPU/vblank
wait, bottom UI, frame end and audio. Previously front-end, boss and Cogdo early
returns put their whole frame, including GPU wait, into `audio`. Consequently the
old 33–37 ms figures cannot be treated as pure CPU costs or compared directly to
new `busy`. Uninstrumented tails now remain visible as `other`.

Screenshot dump I/O formerly landed in `dplace` (or another following section).
Series captures on labels ending in `*`/`**` can therefore contaminate the supplied
facility timings. Captures now have their own `shot` section. Add `shots off` to
the autotest config for timing, then remove it for a separate visual comparison.
The real parser test verifies identical input and step timing over 200 frames
with capture enabled/disabled. Any completed frame >=100 ms produces an immediate
`prof: [label] hitch total ... busy ... largest section ...` record, including
intervening spikes that the five-second summaries could otherwise miss.

Every profiler section is logged in bounded six-field lines. Debug displays the
18 hottest active CPU sections in three columns. Two `stats:` records expose
CPU/GPU/frame time, draws, tris, command-buffer fraction/skips, actual uniform
API calls, texture binds, static vertex-buffer binds, heap and linear/VRAM free
space. These are API-call counters, not bytes transferred or GPU execution time.

## Before/after host evidence

Run `bash tools/perf_test/run.sh` from this checkout. Final sample on this arm64
host, optimized clang++ build; timings vary with host/compiler and are **not 3DS
milliseconds**. Source equivalence checks accompany the timed cases.

| Workload | Before | After | Evidence |
|---|---:|---:|---|
| Dequantization world matrix, 1M varying inputs | 8.77 ns/draw | 5.40 ns/draw | 1.62x; 10,000 matrices within 0.0001 |
| Factory TLV, 128 immutable entity preparations | 7,346.6 ns/frame | 304.8 ns/frame | 24.10x; real 572-entity TLV |
| Cogdo nearest six of 64 actors | 293.0 ns/frame | 125.3 ns/frame | 2.34x; 10,000 stable-tie/health cases |
| Distance evaluations in that selection | <=384 | <=64 | same selected actors/order |
| 128 shared-model instances: tint uploads | 128 | 1 | exact tint keys, same ordered batch trace |
| Same trace: texture state / static VBO binds | 128 / 128 | 1 / 1 | boundary/invalidation cases checked |
| 80 unchanged labels x10,000 frames | 800,000 parses | 80 parses | exact keys; lookup 34.86 ns/string |
| Actual TextCache, 64 labels x10,000 frames | 640,000 parses | 64 parses | stub font backend; no native layout timing claim |
| 160 requested models/anims | 160 direct main loads | 0 direct main loads | fake shared loader; peak 4 staged, 41 pump frames |

Matrix benchmarking varies all rows over 256 inputs and consumes all 16 outputs,
avoiding compiler hoisting of a one-component probe. Matrix math changes rendering
only. Additional checks cover 405 projection points, profiler exclusive/busy/wait
accounting, full queues, cancellation/replacement/failure, UTF-8, changing labels,
buffer exhaustion/generation and TLV lookup misses/hash collisions/append/reload.

## Changes and invariants

- **Renderer:** retain exact texture/tint/blend/depth/layer/cull state between
  consecutive models in a world pass. Reuse static geometry binding only for the
  same vertex pointer. The installed citro3d `C3D_GetBufInfo` marks its context
  dirty; the getter must itself stay inside the rebind branch. Shader/morph and
  world/citro2d boundaries invalidate the appropriate cache. Culled models no
  longer bind shaders. `world * dequantize` uses its known diagonal/translation
  form; the final view multiply remains unchanged. Draw order, facial depth bias
  and alpha order remain unchanged; no global material sort is introduced.
- **Facility:** LevelSpec caches exact full (room,entity) lookups including misses;
  hash collisions use the original binary reader. Append/unload invalidates it.
  LevelGames prepares immutable matrices/scales/model/grid fields once per entity
  and spec revision. Movement, crushed state, mole elevation, grid fallbacks and
  gameplay timers stay live. Room-window checks and renderer frustum/distance
  culling already existed; neither is made more aggressive. This removes repeated
  preparation/state work; native draw-call and facility <=5 ms results are pending.
- **Cogdo:** one stable distance pass selects the same nearest six render actors,
  replacing repeated scans. All 64 simulations remain live. Boss owners receive
  shared renderer/text improvements and explicit profiling, rather than invented
  gameplay/collision shortcuts.
- **Text:** Ui, FrontEnd and BattlePanel retain exact parsed UTF-8 glyph layouts.
  Position, scale, colour and draw flags remain live. Cache and original glyph
  buffer invalidate together on exhaustion; long strings render uncached. Other
  panels' existing dirty/revision caches are retained. Every target still clears
  every frame. FrontEnd already rebuilds preview DNA only on selection/DNA changes.
- **Streaming:** fixed descriptor batches enqueue model/anim requests to the
  existing shared AssetLoader. No extra runtime thread, off-main state mutation,
  or LinearGuard across file I/O is added. Each batch has <=4 in-flight requests;
  the existing global pool remains 24 slots. Poll/admit/publish happens after the
  GPU fence. Replacement cancels stale requests and releases old destinations at
  that fence before replacement I/O, avoiding old+new resident overlap.
- **Owners:** facility props/games, Cog bodies/heads/battle clips, racing, golf,
  estate, Doodles, Cog interior floors, and all 15 separate model-owning trolley
  games plus catching use bounded queues. The body/clip cache's payload capacity
  is unchanged. Minigame ready waits for initial queued assets after the existing
  rules timer; packet formats and simulation/game timers are unchanged. Pending
  racing track/floor identifiers prevent repeat-load storms.
- **Entry:** facility no longer spins in Zone::finishLoading; the same chunks
  progress through its existing workers over frames. Initial geometry/props show
  a loading indicator and hold the player at the entrance; later PVS movement is
  not gated. Other active owners show pending-load hints. Network updates continue.
  GPU-fenced teardown fixes a buffer lifetime hazard; no unobserved z-fighting,
  LOD or visual glitch is claimed fixed.

Remaining synchronous work includes boot-only models, already-faded InteriorLife
entry props, metadata/spec/collision/SFX reads and joint metadata. These paths
need native hitch measurements; there is no blanket claim that all possible
interactive >100 ms frames have been eliminated. Animation sampling/collision
broadphase/network logging were not approximated or rewritten without evidence.

## Memory and build

| Budget / measurable quantity | Base aae224c | Optimized |
|---|---:|---:|
| ELF text | 1,664,592 B | 1,704,432 B |
| ELF data | 56,380 B | 100,180 B |
| ELF bss | 908,832 B | 968,840 B |
| Code + static total (cap 4 MiB) | 2,629,804 B | 2,773,452 B |
| main prologue stack frame incl saved registers | 18,144 B | 77,552 B |
| main stack reservation | 256 KiB | 256 KiB |
| Regular heap reservation | 24 MiB | 24 MiB |
| Linear reservation | 24 MiB | 24 MiB |
| VRAM capacity | 6 MiB | 6 MiB |
| Actual runtime heap/linear/VRAM peaks | unmeasured here | unmeasured here |

Fixed costs: each TextKeys table 26,116 B plus 128 C2D_Text records; existing glyph
buffers remain 1,024/4,096/1,024 glyphs. Facility draw records are 84 B x128; the
find cache is 3,072 B on ARM. All 24 batch descriptor arrays together hold 456
slots (~58,560 B on ARM including counters), distributed among existing owners;
most are in the larger main stack frame, others static. These are descriptors,
not extra cached geometry/textures. AssetLoader staging still competes within
existing linear budgets. Peak resident usage must be logged during scene changes;
unchanged heap reservations do not establish unchanged usage or headroom.

The native `make -j4` build succeeds with zero source warnings. The linker retains
its toolchain `crtn.o` executable-stack note (third-party object). Forty-one
existing regression suites pass, plus the perf harness. The suit ground-truth
suite returns 77 (UNAVAILABLE): `tools/server/.venv` lacks its Panda3D fork exposing
`panda3d.toontown`; the stock wheel cannot substitute. This is not a passing test,
and the requirement that every suite pass cannot be verified in this environment.
Original boss/Cogdo runners now link name_tables.cpp, and collision/suit runners
resolve this worktree's linked resources without another checkout. No assets were
changed; interior-prop two-process rebakes remain byte-identical.

## Main-agent Azahar procedure

Build artifacts are retained locally at `sdmc-out/perf/profile-baseline.3dsx` and
`sdmc-out/perf/optimized.3dsx`. The baseline is base aae224c plus instrumentation
commit 5b3ad75 and the identical capture-disable parser; **none of the performance
changes**. Recreate it with `bash tools/perf_test/build_comparison.sh`. A raw-base
build is also under `sdmc-out/perf/baseline/`. SHA256s and host results are in
`performance_results.json`. Rebuild assets/install data exactly as for the base;
this branch introduces no new asset paths or formats.

Use Old 3DS mode, identical SD data/server state/seed, stereo off, same camera and
stress count. Timing config is `sdmc:/3ds/toontown3ds/autotest` containing:

```
shots off
from go factory
until factory: exit
```

Front-end testing uses `autotest_fe` instead; its presence selects that script, so
remove it before world tests. Existing `only`, `from`, `until` filters still work.
Keep setup/boarding labels in the run: skipping directly to a room does not create
the necessary live server state. Baseline/optimized `busy` are now comparable;
original supplied early-owner numbers were not. Wait for initial loading to finish
before comparing steady frames. Run these exact existing labels:

| Labels (inclusive sequences, with normal setup) | Compare |
|---|---|
| `go factory`, `factory: board`, `factory: crates`, `factory: entities`, `factory: battle`, `factory: exit` | `rooms`, `props`, `levgames`, `levupd`, `dplace`, `loads` |
| `go da office`, `da office: board`, `da office: lasers`, `da office: security cam`, `da office: next floor`, `da office: exit` | same facility sections |
| `go country club`, `club: board`, `club: maze`, `club: moles`, `club: golf green`, `club: exit` | same facility sections |
| `cogdo: board`, `cogdo: floor`, `cogdo: game`, `cogdo: suite`, `cogdo: exit` | `cogdoupd`, `cogdodraw`, `loads`, `ui`, `wait` |
| `boss: board`, `boss: rounds`, `boss: pies`, `boss: end` | `bossupd`, `bossdraw`, `loads`, `ui`, `wait` |
| `cfo: board`, `cfo: rounds`, `cfo: cranes`, `cfo: end` | same boss sections |
| `cj: board`, `cj: rounds`, `cj: court`, `cj: end` | same boss sections |
| `ceo: board`, `ceo: banquet`, `ceo: rounds`, `ceo: golf`, `ceo: end` | same boss sections |
| `fe: title` through `fe: typed name` (includes `fe: species cat`, `fe: colour 6`, `fe: next shirt`, `fe: pick a name`) | `feupd`, `fedraw`, `ui`, `loads`, `wait` |
| `board trolley`, `trolley: ride`, `catch game`, `catch: finish`; `speedway: pads`, `speedway: race`; `golf: board` through `golf: exit`; `go home` | `minigame`, `dmini`, `racing`, `dracing`, `golf`, `dgolf`, `estate`, `destate`, `loads` |
| `tab: map`, `tab: debug`, `stress 8 toons`, `stress 64 toons` | `ui`, `dtoons`, `dact`, counters/headroom |

Record fps, average/max busy and each hot section, heap/linear/VRAM, draw/tri counts,
uniform/texture/VBO binds and command usage in docs/test-log.md for both builds.
Acceptance: 30 fps; steady busy <20 ms; facility combined `rooms`+`props`+`levgames`
<=5 ms; UI <1 ms; no unexplained interactive hitch >=100 ms. A small `dplace`
alone is insufficient because facility work now has separate sections. Check all
hitch records, not just five-second averages. Exclude explicitly identified capture,
HOME/sleep and fade/loading windows, and investigate any large `other`/`audio`.

For visuals, run separately with captures enabled, decode top/bottom dumps with
`tools/dev/decode_shot.py`, and compare equal settled phases/seed/camera. Verify
facial layers/alpha, static-to-morph/shader transitions, text overflow and dynamic
panel changes, all room props/moles/lasers/crates, Cogdo nearest-actor ties, actor
clip seeking at original timestamps, and scene-exit/reentry without GPU flicker.
Check that entrance loading prevents falling through missing floors, indicators
clear after success/failure, minigame ready/start still completes, and model/clip
misses keep their standard fallback without a request storm. The front-end script
contains toon deletion/create steps: use the disposable test account as usual.
