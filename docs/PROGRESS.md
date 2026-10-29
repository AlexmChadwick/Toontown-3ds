# Progress log

Dated entries: what was done, how it was verified, and what is next. Test numbers live in
`docs/test-log.md`; architecture and decisions in `AGENTS.md`.

## 2026-10-29 — Exact shared-texture planning (TZ v4)

### Done
- Added TZ v4 while retaining runtime and host-reader compatibility with v1-v3. V4 keeps
  the lossless shared payload pool and adds fixed 256-bit near/far texture-identity sets per
  chunk plus a compact table of 0x80-aligned allocation costs.
- Changed near/far selection to budget the exact union of selected shared GPU pages instead
  of summing duplicated per-chunk texture totals. Queue admission likewise unions resident,
  in-flight and incoming identity sets, so a shared page no longer forces an unnecessary far
  LOD, old-LOD retirement, or stalled upgrade. The cache remains the final 8 MiB hard gate.
- Kept planning allocation-free and bounded: each union is 32 bytes, at most 256 identities,
  and malformed out-of-range bits or costs fail zone validation. New bakes/repacking default
  to v4; old files retain their previous conservative behavior.
- Made `tools/perf_test/run.sh` honor `CXX` (default `c++`) and fail with an actionable error,
  instead of hard-coding Clang on every compile command.

### Verified
- Synthetic containers built from the committed title-logo TTM and TTC collision round-trip
  through `tz.write`/`tz.validate` in versions 1, 2, 3 and 4. `tz.model_bytes` reconstructs
  the original TTM byte-for-byte. Sizes were 245120, 376704, 245632 and 245888 bytes,
  respectively, so v4 added 256 bytes over v3 in this one-model fixture.
- `python3 -m py_compile tools/pipeline/tz.py tools/zone_test/run.py`, `bash -n` on the perf
  runner, its explicit missing-compiler diagnostic, and `git diff --check` pass.
- This checkout has no C++ compiler, devkitARM, Panda3D environment, full zone bake, or
  emulator. The real C++ zone suite, 3DS build and Azahar memory/PVS walk could not run here.

### Asset size decisions
- No texture was changed, downscaled or recompressed. V4 adds 64 bytes per chunk plus a
  small allocation-cost directory; texture payloads remain byte-identical to v3.

### Known issues
- Rebuild the full SD zone/level set before deployment because the runtime deliberately
  uses exact planning metadata only for new v4 files. Existing v1-v3 files still load.
- V4 needs an equipped Old 3DS Azahar and hardware walk under repeated PVS/near-far churn.
  Confirm planner choices, cache telemetry, linear high-water marks and corrupt-directory
  rejection with the real C++ suite.
- Facility collision/layout staging, four boss battles and the first real-hardware
  performance pass remain open.

### Next
- Run the full deterministic SD bake, compare v3/v4 package sizes, and execute
  `tools/zone_test/run.py` across every exterior and facility zone.
- Exercise near/far boundaries under forced texture pressure in Old 3DS mode, checking that
  exact planning admits quality upgrades without exceeding 8 MiB.
- Move facility room collision and layout preparation off the main thread.

## 2026-10-28 — Exact linear-memory accounting for model textures

### Done
- Fixed `render::Model::linearBytes()` counting only `C3D_Tex::size`, which is the base
  mip level even though `loadTexture` allocates and reads the complete baked mip chain.
  Model residency now counts each texture's validated full-chain `dataSize`.
- Included the 0x80-byte linear-allocation rounding for the separate vertex, index and
  texture allocations. Boss-scene admission and every other `Model::linearBytes()` caller
  therefore see a conservative representation of actual Old 3DS linear residency.
- Applied the same allocation rounding to the zone-local shared-texture cache's resident
  and pending totals. Its 8 MiB limit is now a hard allocation limit rather than a payload
  limit that could be exceeded by alignment padding across many atlas pages.

### Verified
- `git diff --check` passes after each focused code commit.
- Source review confirms `loadTexture` already requires
  `C3D_TexCalcTotalSize(tex->size, tex->maxLevel) == TexEntry::dataSize`, so the value now
  used by residency accounting is exactly the full mip-chain payload accepted by the GPU
  loader. Toon preflight independently uses the same full-chain bytes and 0x80 alignment.
- The host performance script could not run because it is hard-coded to `clang++`, which
  is absent along with every other C++ compiler in this checkout. devkitARM is also absent,
  so a 3DS build and Azahar memory pass could not be performed here.

### Asset size decisions
- None. Original texture payloads, mip levels, formats and resolutions are unchanged; only
  runtime accounting and admission became accurate.

### Known issues
- Correct accounting can reject a boss scene or model set that the old undercount admitted.
  That is intentional protection against linear-heap exhaustion, but the affected scenes
  need an equipped Old 3DS Azahar run to confirm their existing budgets retain enough margin.
- The TZ v3 near/far planner still sums duplicated per-chunk texture totals before selecting
  LOD. Shared-cache admission is exact and aligned, but exact planning still needs compact
  per-model pool-reference metadata.
- Facility collision/layout staging, four boss battles and the first real-hardware
  performance pass remain open.

### Next
- Build on an equipped devkitARM host and exercise Toon churn, boss entry and repeated PVS
  transitions while watching linear high-water marks and admission failures.
- Add compact TZ pool-reference metadata so the planner can budget the exact union of shared
  pages without synchronous scattered reads.
- Move facility room collision and layout preparation off the main thread.

## 2026-10-28 — Non-blocking shared-texture cache admission

### Done
- Changed TZ v3 LOD queue admission to start from the cache's exact unique resident-byte
  total instead of the duplicated sum in each chunk header. Incoming and outstanding jobs
  still reserve their conservative full texture totals until their tables are read, and the
  cache remains the final hard 8 MiB gate. Shared resident pages therefore no longer cause
  an avoidable far-LOD choice or premature retirement of the visible model during an upgrade.
- Removed SD I/O and PICA texture allocation from the zone texture-cache critical section.
  The loader now reserves pending bytes under the lock, performs the slow load unlocked, then
  publishes the allocation under the lock. Main-thread eviction and debug telemetry no longer
  wait behind a texture payload read, while resident plus pending allocations remain bounded.
- Kept publication safe for a future multi-loader implementation: a concurrently published
  equivalent page wins, takes the borrow, and the redundant new PICA allocation is reclaimed.

### Verified
- `git diff --check` passes after each focused code commit. Lock, byte-reservation and failure
  paths were reviewed for cache hit, load failure, metadata allocation failure, duplicate
  publication, last-reference eviction and zone destruction.
- No host C++ compiler or devkitARM exists in this checkout, and the generated full zone set is
  absent, so the 3DS build, host loader suite and Azahar transition timing could not run here.

### Asset size decisions
- None. This changes runtime scheduling and admission only; original texture payloads, formats,
  resolutions and the TZ v3 on-disk representation are unchanged.

### Known issues
- The near/far planner still sums duplicated per-chunk texture totals before selecting an LOD.
  Queue admission is now less conservative once the plan exists, but fully exact planning still
  needs compact per-model pool-reference metadata in a future TZ format.
- Pending jobs reserve their complete texture totals even after loading begins. This can delay a
  second job conservatively, but it cannot exceed the linear-memory cap or stall the main thread.
- The changes need an Old 3DS Azahar PVS/LOD walk and real-hardware SD test. Measure frame-time
  spikes while the Debug page samples texture residency and verify the cache never exceeds 8 MiB.
- Facility collision/layout staging, four boss battles and the first real-hardware performance
  pass remain open.

### Next
- Add compact TZ pool-reference metadata so planning and pending-job reservation can count the
  exact union of shared pages without synchronous model-table reads.
- Measure repeated near/far crossings and PVS changes on the equipped rig, including slow-SD
  conditions and forced budget pressure.
- Move facility room collision and layout preparation off the main thread.

## 2026-10-28 — Bounded shared-texture lifetime and streaming progress

### Done
- Fixed TZ v3 shared texture residency retaining zero-reference pages until the whole zone
  unloaded. Crossing PVS cells now releases a pooled atlas page immediately after its last
  borrowing model is GPU-fenced, so texture residency follows the current visible set instead
  of growing monotonically toward the complete zone pool.
- Enforced the 8 MiB zone-texture limit at the cache allocation point using full mip-chain
  payload bytes. This is the exact unique allocation total for shared pages and prevents a
  future planner/accounting error from silently overrunning the Old 3DS linear-memory budget.
- Fixed an existing LOD replacement deadlock: when retaining the old and replacement models
  simultaneously would exceed a hard geometry or conservative texture cap, the old LOD is
  now fenced and retired before the new load is queued. Normal transitions still keep the old
  LOD visible until publication.

### Verified
- `git diff --check` passes, and the lifetime/lock ordering was reviewed across successful,
  partial-failure, unwanted-job, LOD-swap and zone-unload paths.
- The cache uses the texture table's validated `dataSize`, which is the complete mip chain;
  `C3D_Tex::size` is only the base level and is deliberately not used for budget telemetry.
- This checkout has neither a host C++ compiler nor devkitARM, and its Python environment lacks
  Panda3D, so the zone host suite, 3DS build and Azahar memory walk could not run here.

### Asset size decisions
- None. Original texture resolutions, formats and on-disk payloads are unchanged; this round
  only bounds their runtime lifetime and allocation.

### Known issues
- The streaming planner still uses conservative per-chunk texture totals when choosing near
  LODs, so it can decline an upgrade that would fit after TZ v3 sharing. Runtime allocation is
  now exact and bounded, but exact planning needs compact per-model pool-reference metadata to
  avoid restoring scattered synchronous SD reads at zone entry.
- The fixes need an Old 3DS Azahar PVS walk and real-hardware run. Watch `zone texture`/linear
  telemetry across repeated cell and near/far crossings; it should fall after evictions and
  remain at or below 8 MiB without loader stalls.
- Facility collision/layout staging, four boss battles and the first real-hardware performance
  pass remain open.

### Next
- Add compact TZ pool-reference metadata (or a bounded loader-produced equivalent) so near-LOD
  planning can count unique shared pages without main-thread table reads.
- Measure repeated multi-cell street transitions on the equipped rig, including forced
  budget-pressure LOD replacements and linear-memory recovery.
- Move facility room collision and layout preparation off the main thread.

## 2026-10-28 — Shared TZ v3 texture residency

### Done
- Added a zone-local texture residency cache for TZ v3. Chunks that reference the same
  validated texture-pool payload now share one C3D linear-memory allocation and upload,
  rather than allocating and reading that atlas page again for every near/far or PVS
  model that uses it.
- Kept standalone TTM models and pooled interior TTM v2 files on their existing ownership
  path. Cache identity includes the absolute pool offset, size, dimensions, format, mip
  count and wrap modes, so only equivalent GPU textures alias.
- Made cache borrowing explicit in `render::Model`: swaps carry the cache owner and keys,
  partial load failures release successful borrows, and model unload never deletes shared
  C3D data. Cache-reference release happens before taking `LinearGuard`, preserving one
  lock order between the zone loader and main thread.
- `Zone::textureBytes()` now reports unique TZ v3 allocations on 3DS, so the debug overlay
  reflects the actual linear-memory saving instead of the sum of duplicate chunk tables.

### Verified
- `git diff --check` passes and the implementation is split into a focused code commit.
- Reviewed loader/main-thread lifetime and synchronization: the loader is joined and the
  GPU is fenced before models are destroyed; staging jobs and resident models release
  borrows before the cache destroys its allocations.
- A 3DS build could not run in this checkout: `make` stops immediately because `DEVKITARM`
  is unset and no devkitARM installation is present. The generated SD zone set is also
  absent here, so an Azahar resident-memory A/B measurement was not possible.

### Asset size decisions
- None. This changes runtime residency and SD reads only; texture formats, resolutions and
  all original asset data are unchanged.

### Known issues
- The cache and its new `textureBytes()` telemetry need an equipped Old 3DS Azahar run and
  a real-hardware run. Verify transitions and repeated near/far LOD changes while watching
  linear free memory for both savings and leaks.
- TZ v3 admission remains conservative: the streaming plan sums per-chunk texture bytes,
  including duplicates, before queueing. This cannot exceed the existing 8 MiB budget but
  may reject a future visible set that would fit after sharing.
- Facility collision/layout staging, street transition timing, four boss battles and the
  first real-hardware performance pass remain open.

### Next
- Build on the equipped rig and compare `linear free` / zone texture telemetry before and
  after loading a multi-chunk street, changing cells, and crossing the near/far threshold.
- Deduplicate the streaming-plan texture budget from the TZ v3 pool keys, then consider
  bounded eviction only if a measured zone pool is larger than the residency budget.
- Move facility room collision and layout preparation off the main thread.

## 2026-10-28 — Zone validation leaves the main thread

### Done
- Removed the largest remaining set of scattered synchronous reads from `Zone::load` on
  3DS. Detailed TTM texture, batch and (when necessary) index validation now runs in the
  existing zone loader thread immediately before that chunk is allocated and loaded. A
  street/facility arrival no longer probes both LODs of every chunk before streaming can
  begin.
- Validation results are cached per chunk and LOD. A corrupt chunk still fails before any of
  its model-controlled counts or offsets reach GPU allocation, and an invalid result is not
  repeatedly read from SD on retry. Container ranges, disjoint sections, PVS/floor tables,
  budgets and collision remain validated synchronously before the zone is accepted.
- Kept eager detailed validation in host builds so the malformed-zone suite continues to
  reject corruption at `Zone::load`, independent of which cell a test visits.
- Facility floor logs now include the complete synchronous floor-preparation time in
  milliseconds, giving the next device/emulator pass a direct comparison point for the
  metadata and deferred-validation changes.

### Verified
- `git diff --check` passes for each change and the branch is clean after committing.
- Source review confirms the loader is the only user of the open TZ file after `load`
  returns; validation finishes before `Model::loadFrom` and its linear/GPU allocations.
- The zone host suite could not run in this checkout because importing its fixture/baker
  support requires the absent `panda3d` module. This image also has no host C++ compiler or
  devkitARM, so a 3DS build was not possible here.

### Asset size decisions
- None. Runtime scheduling and logging changed; baked data and original asset quality are
  unchanged.

### Known issues
- Deferred validation needs an equipped Azahar and Old 3DS run. The previous measured
  facility floor transition was 0.2-0.4 s and a street load was 1.0-1.3 s; the new
  `level: floor ... ms` line should be used for the former.
- Collision blobs and their cell-index validation are still loaded synchronously for every
  room. Facility layout assembly itself also remains on the main thread.
- TZ v3 removes duplicate texture payloads on disk, but resident chunks still upload their
  own copies to linear memory. Sharing those uploads is the next large memory/I/O win.
- The four boss battles and real-hardware Old 3DS memory/frame-time measurements remain
  unverified.

### Next
- Build and compare facility/street transition logs in Azahar, then on Old 3DS hardware.
- Stage room collision/zone metadata as a background floor-preparation job, with an atomic
  main-thread publish after all rooms validate.
- Add a ref-counted TZ v3 texture residency cache so chunks sharing a pool payload also share
  one PICA texture allocation and upload.

## 2026-10-28 — Facility and zone metadata I/O

### Done
- Reduced synchronous SD transactions while entering a facility floor or street. TZ v2/v3
  chunk tables are read in one operation, TTM texture and batch tables are validated in
  bounded blocks, and facility floor-triangle records are read 64 at a time instead of one
  seek/read per triangle (up to about 850 records per floor).
- Finished the same treatment for the small TLF layout tables: all floor pieces and all door
  frames are now read once per table. Their existing hard bounds (at most 32 records) keep
  stack use below 6 KiB and no persistent memory or asset size is added. Over-limit door-frame
  counts now reject the floor instead of being silently truncated.

### Verified
- `git diff --check` passes.
- Source review confirms record processing order and on-disk structures are unchanged; reads
  still fail closed on short/corrupt files.
- This checkout's minimal Linux image has no C++ frontend/devkitARM and no Panda3D virtualenv,
  so the host suites and 3DS build could not be executed here. The attempted zone unit test
  stopped at the missing `panda3d` module; the performance suite stopped at missing `clang++`.

### Asset size decisions
- None. This round changes only runtime I/O; no original assets were converted or downscaled.

### Known issues
- The facility-floor improvement still needs measurement in Azahar and on hardware; the last
  measured floor transition paused the main thread for 0.2-0.4 s before these I/O changes.
- Street loads remain synchronous and last measured at 1.0-1.3 s, though their fade hides the
  stall. Room TZ validation and collision loading still occur on the main thread.
- The four boss battles and a real-hardware Old 3DS performance/memory run remain unverified.

### Next
- Build and run the facility chunks on an equipped rig, comparing elevator-fade hitch duration
  before and after these metadata changes.
- Move facility room preparation off the main thread, or split it into a background staging
  phase with main-thread publication; keep all linear allocations guarded.
- Share each TZ v3 texture upload across resident chunks so the on-disk de-duplication also
  reduces linear-memory use and upload work.

## 2026-10-27 (rounds 4-5) — DA office and country club won, racing speed, reconnect fixes

### Done
- **Reconnect never rejoins a dead dynamic zone** (round 4). A drop on a facility floor (or
  in a minigame, race, golf or estate zone, all >= 61000) reconnected into that zone after
  the AI had freed it: an empty floor with no Cogs, no elevator and no way out. Last round's
  "DA office floor elevator has no position" was exactly this. The session now returns to the
  last static zone (the facility lobby; a street cell becomes its playground) and the world
  follows with a toast. nettest has a dynamic-zone drop case.
- **Arrivals settle on the new place's floor** (round 4): winning the DA office dropped the
  toon at Lawbot HQ 68 units in the air (the reset used the facility's collision).
- **Karts were held to ~20 units/s.** `Racing::update` took a flat 10% off the velocity on
  every frame the kart touched a wall. Panda's `PhysicsCollisionHandler.apply_net_shove`
  (read from the Panda source) only counts a kart moving *into* the wall, removes that
  component, and applies friction 0.1 x cos(velocity, wall normal). On Screwball Stadium the
  kart slides along the barrier for most of a lap, so it never finished one and the AI kicked
  it as too slow. The rule is now `racing::Vehicle::wallShove`, shared by the runtime and the
  host drive test (which had no wall response at all, so it never saw this); new head-on /
  grazing / moving-away vectors. Host: all 12 tracks run 3 laps, Screwball in 129 s.
- **A drop mid-race got the next session ejected.** `resetWorld` cleared every object family
  except racing and golf, so after a reconnect Racing rebuilt itself from the dead race and
  acked its barrier; Astron ejected the new session (117, update to a nonexistent object).
  Both are now cleared on disconnect; `racing_test/client` checks it (and fails without it).
- `azahar_test.sh`: runs really are muted now (Azahar ignores a value while its `\default` flag
  is true), headless Linux uses the Null audio sink, and the kill pattern no longer leaves an
  Xvfb server behind per run (51 had piled up on this box). The VP/CFO `rounds` steps re-board
  from the lobby after a reconnect instead of idling.
- autotest: the facility `next floor` step rides on to the top floor on purpose (floor
  elevators are never locked, `DistributedClubElevatorAI.isLocked = 0`) and stops on the floor
  with the boss Cog.

### Verified (Azahar 2126.2, Old 3DS mode, Linux/Xvfb, live server, `AI_DEV_STRONG`)
- **DA office won end to end** (round 4): floor 1 (lasers, security camera, room battle),
  floor elevator, floor 2, boss battle, `StageReward`, back in Lawbot HQ. 26-30 fps.
- **Country club won end to end**: kart boarding, floor 1 (maze, mole field, golf green,
  room battle), floor elevators through floors 2-4 to floor 5, the president-room boss
  battle, `CountryClubReward`, `level: 10600 defeated -> zone 10000`. No PARSE/EJECT lines.
  ~30 fps, up to 177 draws / 14.2k tris, heap 3.9 MB, linear free >= 11 MB. A floor change
  costs a 0.2 s + 0.4 s main-thread hitch (floor `.tlf` and room loads), behind the elevator
  panel.
- **Cogdominium won end to end** (first run on this rig): field office boarding, the maze
  (14 Cogs, 9 coolers, gags thrown, exit), the executive-suite battle, `BuildingReward`,
  `victor ready`, the building back to a toon building. 30 fps, 57 draws, 9.3k tris.
- **Speedway Practice race end to end** (first time on any rig): owned kart, Practice
  block, barriers, three laps on Screwball Stadium (~42 s a lap after the fix), the
  authoritative result, back in the Speedway with 20 tickets. 29.9 fps (min 25), <= 55 draws.
- Host: racing_test (all parts), nettest (all, including both reconnect cases).

### Asset size decisions
- None this round.

### Known issues
- **The Azahar emulated-clock stall still ejects runs here (345)**, and it now blocks the boss
  battles entirely. Six more this round: Cogdo after its win, the Speedway twice, and all
  three Sellbot VP attempts, each within seconds of entering the lobby or the boss office
  (one froze ~13 min of wall time over ~5 s of emulated time, during idle 30 fps frames). The
  client keeps sending heartbeats in emulated time (outgoing bytes rise by exactly one 4-byte
  frame per 10 s). Ruled out: CPU starvation (load < 3.5 on 8 cores while Azahar spins) and
  the failed audio sink (the Null sink stalls the same way). Still unseen on the Mac rig.
  **The four boss battles therefore remain unverified in an emulator**; they need the Mac rig
  or hardware.
- A facility floor change freezes the main thread 0.2-0.4 s (behind the elevator panel).
- The tailor keeps the clerk camera (the counter is baked into the interior mesh).
- Street loads take 1.0-1.3 s on the main thread (behind the teleport/door fade).
- Not run on this rig yet: the four boss battles (VP, CFO, CJ, CEO), the estate, golf
  (unmerged `golf2`), interior-life steps. No real-hardware run yet.
- Never relink while Azahar runs the tree's `.3dsx`; test from a copy.

### Next
- The four boss chunks (`boss: board` onward) on the Mac rig or hardware; this rig can't hold
  them.
- Move facility floor loads off the main thread (the shared loader already handles props).
- First real-hardware run (frame budget, the socket stall question).

## 2026-10-27 (later) — SD set halved, Cog buildings winnable, battle hitches, clerk camera

### Done
- **SD data set 992 MB -> 603 MB** (586 MB apparent), with no change to any asset's quality:
  - Zone `.tz` files copied every atlas page into every chunk that used it (near and far):
    271 MB of texture copies for ~12 MB of distinct payloads. **TZ v3** stores each payload
    once in a per-file pool; chunk TTMs point into it (`Model::loadFrom` unchanged). zones
    389 -> 131 MB (2100.tz 22.0 -> 6.2 MB), level 68 -> 58 MB. `tz.py <files>` converts
    existing bakes losslessly (v3 -> v2 gives back the original bytes); bakers write v3.
  - The 538 interiors held 120 MB of copies of ~2 MB of textures. `ttm_pool.py` writes one
    `interiors/textures.ttp` and pooled TTM v2 models (170 -> 42 MB); deterministic,
    idempotent, `--unpool` restores the exact v1 bytes; `bake_sd_all.sh` runs it.
- **Cog buildings could not be won.** Three bugs, found by the first strong-toon run:
  - The interior elevator was never found: `DistributedElevatorIntAI`'s `bldgDoId` is the
    interior's doId, not the exterior building's. Toons were stranded on floor 1.
  - Boarding was edge-triggered on entering the sphere, so a toon already standing in it
    when the doors opened never boarded. It also accepted the `opening` state, which
    upstream never does: boarding then wedged the AI (`no transition from opening to
    allAboard`) and the floor never started.
  - After the top floor's reward the toon stayed inside forever: `extZoneId` was parsed
    but unused. `DistributedSuitInterior.enterReward` now takes the toon back to the street
    at the building's elevator, and `setVictorReady` is sent once the building is in
    `waitForVictors`, so the AI turns it back into a toon building.
- **Battle freezes removed.** The battle face-off froze ~140 ms (attack props loaded on
  the main thread) and each Cog death ~280 ms (lose model and clip loaded synchronously).
  Both now stream through the shared background loader, the props at battle start and the
  lose model when a battle first places the Cog.
- **Clerk camera.** The Gag Shop, tailor, pet, kart, fisherman and party clerks now use
  upstream's purchase-movie camera (1 s ease to the clerk-relative over-the-shoulder view).
  The autotest drivers walk up to clerks like a player instead of teleporting inside
  the counter.
- Cog building and Cog HQ shells load only on streets (they were loaded synchronously in
  every playground, interior and HQ: ~250 ms and ~350 KB linear per load).
- `battle_validate` counts the clothing a battle really holds (it counted the whole 1 MB
  `clothes.ttx`, which is streamed) and passes with the full SD set.
- A 10 s `net: link` log line (bytes in/out, heartbeat age, send backlog) for diagnosing
  disconnects.

### Verified (Azahar 2126.2, Old 3DS mode, Linux/Xvfb, live server with `AI_DEV_STRONG`)
- v3 zones 2000/1000/5000/3000 and pooled interiors 2519/2521/2522/8501 load and render
  correctly (screenshots); zone_test (the real C++ loader on the host) passes for v2 and v3,
  with new corruption cases for the pool.
- Factory (steps 147-162) to the foreman, won, back to Sellbot HQ: 26-30 fps.
- Cog building (3 floors) won end to end: floors 1-3, elevator rides, reward, street,
  `victor ready`. No frame hitch in any of the battles.
- Gag Shop, tailor and kart shop camera framing (screenshots).
- Cashbot mint (`go mint` .. `mint: boss`): boarded, walked every room and hallway, room
  battle, boss battle won (`level: 12500 defeated -> zone 12000`), back in Cashbot HQ.
  29.9 fps in the mint; heap 3.6 MB, linear free >= 9.5 MB.
- DA office, floor 1: boarded, walked, laser field hits and hazard, security camera, room
  battle. 30 fps, 87 draws, 10.4k tris, linear free 9.2 MB. The floor elevator was never
  found (see Known issues).

### Asset size decisions
- None downscaled. The size cut is pure de-duplication; every texture keeps its baked
  resolution and format.

### Known issues
- The tailor's upstream toon-facing clothes camera needs its counter hidden; the counter is
  baked into the interior mesh, so the tailor keeps the clerk view.
- Street loads take 1.0-1.3 s on the main thread; the teleport/door fade covers them.
- **Six runs on this rig were ejected for heartbeat timeouts (345)**, in Cog buildings and
  facilities. The `net: link` line shows the client queueing a heartbeat every 10 s of
  emulated time right up to the drop; the log clock shows Azahar's emulated time stopping
  for 5-10 minutes and resuming exactly when Astron closes the socket, even with the
  timeout raised to 300 s (`ASTRON_HEARTBEAT_TIMEOUT_MS`, new, test rigs only). Facilities
  also average only ~35-50% emulation speed here. The stall looks like Azahar's socket
  emulation (software GL, Xvfb, no audio device); it has not been seen on the Mac rig and
  needs a hardware run to rule out the client. Fps/ms numbers here are emulated time.
- **DA office floor elevator**: on floor 1 `level.elevatorPosition` never returns a position,
  so the next-floor step cannot board (AGENTS.md: stage entry was already listed as open).
  The country club has the same floor-elevator path and is untested.
- The net loop now calls `recv()` only when `poll()` reports the socket readable (it ran on
  every ~10 ms idle iteration, a SOC call each on hardware).
- Still not run on this rig: DA office floors 2+ and its boss, country club, speedway, Cogdo, the four
  bosses, interior-life steps. No real-hardware run yet.
- Never relink while Azahar runs the tree's `.3dsx` (RomFS is read from it); test from a copy.

### Next
- Find why the DA office floor elevator has no position, then the club and boss chunks.
- Pin down the Azahar socket stall (try `graphics_api` software / Vulkan, an audio sink,
  or a hardware run).
- Shared textures in residency too (one upload per zone texture instead of per chunk).
- First real-hardware run.

## 2026-10-27 — Linux test rig, front-end polish, Toontorial end to end

### Done
- **Everything now builds and runs on a plain Linux x86_64 box** (no sudo, no Docker daemon):
  - devkitARM / libctru / citro3d / citro2d: the layers of the `devkitpro/devkitarm` Docker
    Hub image, fetched with curl and unpacked (pkg.devkitpro.org answers 403 here).
  - Azahar 2126.2 Linux AppImage, extracted and run under Xvfb (OpenGL, software GL).
    `tools/dev/azahar_test.sh` now works on Linux as well as macOS.
  - The real Open Toontown server: `tools/server/setup_linux.sh` (micromamba conda-forge
    toolchain, astrond built from Astron, the Panda3D fork built at `setup.sh`'s pinned
    commit). `start.sh`/`stop.sh` are portable. `smoke_test.py` passes.
  - The full SD data set: `tools/pipeline/bake_sd_all.sh` (6 parallel lanes, 16m47s on 8
    cores, 992 MB). Bakers that also write committed `romfs/`/`source/`/`docs/` outputs run
    with those outputs redirected to scratch, so a bake never dirties the tree.
  - Host test suites build with GCC 12 (x86); they were previously only run with Apple clang.
- **Front end**: the title shows the original `phase_3/maps/toontown-logo` at the
  proportions of `progress-background`'s logo card (new `bake_title_logo.py`; RGBA8
  256×128, 128 KB RomFS). The bottom screen names the configured `host:port`. Before any
  session exists, Backoff says "Can't reach the server. Trying again..." instead of
  "Connection lost". Centred labels shrink to fit; the Toontorial prompt no longer runs off
  both screen edges.
- **Debug tab**: every row fits the 320 px screen (the net row and the log line overflowed).
- **Toontorial bugs (found by the first live run on this rig)**:
  - After beating the Flunky the game was stuck on the reward forever: the battle panel
    consumes A/Continue, and Continue was ignored in the tutorial while the main-loop hook
    waited for an A it never saw. Continue is now the acknowledgement.
  - The tutorial Flunky id was never cleared when the AI deleted it, so the tutorial never
    advanced to HQ Harry. It is cleared on OBJECT_LEAVING.
  - The autotest driver teleported onto flush door/tunnel triggers and stood still; a
    walking toon's probe touches them, a standing one is pushed back just out of reach.
    The driver now runs into the HQ back door and the TTC tunnel like a player.
- **x86 portability bug**: the HQ leaderboard pickle decoder compared a `uint8_t` with
  `'\x85'`..`'\x87'` (negative on x86, so TUPLE1–3 never matched in host tests).
- Host tests no longer depend on one Mac's absolute paths (cog thief, target, suit refs).
- **Map tab indoors** showed a meaningless grass-green square; interiors now get a plain
  floor-plan panel labelled "Indoors".
- **Facility elevators missing from a fresh SD bake**: `bake_sd_all.sh` now runs
  `coghq_elevators.py`, without which the factory/mint/DA office elevators had no anchors
  (not placed, not boardable).

### Verified (Azahar 2126.2, Old 3DS mode, Linux/Xvfb, live local server)
- Title → connect → Pick-a-Toon → Make-a-Toon (all 5 steps, Pick-a-Name and typed name)
  → create → TTC: 29.9 fps, heap 3.5 MB, linear free 15.4 MB, VRAM free 4.2 MB.
- Toontorial: Tom's interior, Mickey's intro, Tom's quest, the street, the Flunky battle
  (throw + squirt), reward, HQ Harry, out of the HQ. See test-log for the tunnel result.
- Offline world script (moves, tabs, padnav, book steps): 27–30 fps, 172 draws, 13.3k tris.
- Online world script, steps 0–163, in three chunks (`AI_MINIGAME_ID=8`): playground,
  clips/emotes, treasure, Mickey, fishing, trolley + Catching game + gag purchase, door,
  Gag Shop/pet/kart/fisherman/party clerks, tunnel, street, street battle (all movie
  tracks), teleports, DD boat and swimming, DG, BR, all four Cog HQs, Toon HQ quests,
  tailor, and the Sellbot factory (board, button, stomper, lift, barrel, gear, mixer,
  conveyor, door, crates, goons, room battle). No PARSE/EJECT/crash lines.
- Host suites: 30 of 32 pass on x86. `golf_test` needs golf data from the unmerged `golf2`
  branch; `cog_thief_test` passes after the path fix.

### Asset size decisions
- Title logo: the 256×256 upstream texture is shown at 205×72 px, so it is stored
  256×128 RGBA8 (RGBA4 banded the soft outline). No other asset was downscaled this round.

### Known issues
- The SD data set bakes to 992 MB, about twice the 480 MB the README states; `zones/`
  alone is 389 MB (37 `.tz`, some over 11 MB). Needs a size review before shipping SD packs.
- `battle_validate` fails its 4 MiB battle-scene budget when the full `clothes.ttx`
  (about 1 MB) is installed instead of the Make-A-Toon subset.
- Golf is not merged (`golf2` branch); `golf_test` cannot run without its data.
- No real-hardware run yet; all frame numbers are emulator numbers.
- Azahar on Linux runs here without audio (no DSP firmware, no sound device).
- The autotest presses A every frame in the tutorial, so the Flunky reward panel is only
  shown for one frame in automated runs; a human sees it until they press Continue.

- The scripted test toon is a brand-new 15-laff toon with no gags bought, so it goes sad
  in the street battle and the factory room battle; the foreman, Cog building, mint, DA
  office, club, boss and minigame passes after step 163 were not run this round.
- Factory frames dip to 21–27 fps in Azahar while screenshots are captured (shot I/O
  4–15 ms/frame); run with `shots off` for frame-rate numbers.

### Next
- Give the world script a stronger toon (or dev Cogs) and run steps 164+ (Cog building,
  mint, DA office, club, bosses, estate, golf, racing) on this rig.
- Review the zone `.tz` sizes against the SD budget.
- First real-hardware run.
