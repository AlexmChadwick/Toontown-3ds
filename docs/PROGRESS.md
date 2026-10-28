# Progress log

Dated entries: what was done, how it was verified, and what is next. Test numbers live in
`docs/test-log.md`; architecture and decisions in `AGENTS.md`.

## 2026-10-28 (round 7) — Toontown bottom screen, receive latency, isolated test runs

### Done
- **Bottom screen is a Shtiker Book page, not a debug console.** It opened on the Debug tab in
  dark navy. Now: cream paper, the book's maroon cover as the tab bar with gold active tabs,
  Toontown's yellow buttons (`ui/tab_layout.h` holds the layout and palette). SpeedChat uses
  upstream's `SCColorScheme` defaults (near-white frames, green rollover, blue arrows, black
  text). The Map tab is the default and is titled by place name ("Toontown Central", "Silly
  Street, Toontown Central") instead of "Zone 2000 (9, 52)". Gags rows get the book's track
  colours and experience bars; the Toon tab's task cards are readable on paper. A **Book**
  button sits in the bottom-right corner, where upstream's is. **Setup and Debug are developer
  tabs**, hidden until SELECT (or Options > Debug HUD, which did nothing before).
- **Frame pacing cut a frame of latency.** The vblank wait sat inside
  `C3D_FrameBegin(SYNCDRAW)`, *after* input, the net pump and the simulation, so anything that
  arrived during the wait (most of a 33 ms frame) was drawn one frame later. The loop now waits
  at the top (`render/frame_pace.h`); every frame owner (world, front end, VP/CFO, CJ, CEO,
  Cogdo) begins with `frameBeginFlags()` so nothing waits twice. Input gains the same frame.
- **Remote toons are drawn where they are, not ~0.1 s × speed behind.** The 0.1 s exponential
  chase of the last 5 Hz sample is replaced by dead reckoning (sample + velocity × age, capped at
  0.25 s / 40 u/s, no vertical extrapolation) under a 0.05 s smoothing filter.
- **Latency is measurable.** The net thread stamps each message on arrival. New log lines:
  `net: rx chat sc|talk|whisper ... queued N ms`, `net: tx chat ...`, 1 Hz `net: rx pos` /
  `net: tx pos`, and a 10 s `net: rx latency` median/p90/max. Regression check:
  `tools/dev/latency_run.sh` (3DS probe `chat every 2`, PC bot `smoke_test.py --echo 100 101`,
  `tools/dev/latency_check.py`, fails above 45 ms one way). Cross-clock subtraction is not used:
  Azahar's 3DS clock is host *local* time with a 1 s RTC, so it is off by up to a second.
- **Placements are broadcast.** Poses were only sent while walking, so a teleport arrival, a
  zone entry while standing or a placed toon stayed where the others last saw it. A zone change
  or a >4 unit jump now sends `setSmPosHpr` + `setSmStop` (upstream's `startPosHprBroadcast`
  sends everything on place entry), held while a transition or battle owns the toon.
- **Test harness safe for parallel agents.** `azahar_test.sh` gives each Linux run private XDG
  dirs (config, nand, sysdata, sdmc, log), its own Xvfb display and app copy, launches the
  emulator via a per-run symlink and kills only that process. The old global
  `pkill -f squashfs-root/AppRun.wrapped` also matched other agents' `claude` command lines on
  this box. `SERVER=host:port TOKEN=...` writes `server.txt`; interrupted runs keep `log.txt`.
  `start.sh`/`stop.sh` take `SERVER_PORT`/`SERVER_RUN` for a private server.

### Verified (Azahar 2126.2, Old 3DS mode, Linux/Xvfb, private live server on 37198)
- Offline script: every tab and the book (screenshots); 29.9 fps outside screenshot seconds,
  same distribution before and after the pacing change; heap 3.5 MB, linear free 15.9 MB,
  VRAM free 4.2 MB. padnav_test (118 checks), nettest (all), book_test, perf_test pass.
- **Latency** (clean run, no VM pause): 57 echoed probes. Round trip 33 ms median (min 33, max
  67: one frame), one way bot -> 3DS handled ~16 ms median (max 34), socket -> handled 8 ms
  median (max 25); 3DS summaries median 8-15 ms. PASS against the 45 ms target.
- **Pose broadcasts** seen by PC bots: zone-entry pose and treasure placement in TTC; a real Map
  teleport TTC -> Daisy Gardens arrives at upstream's first DG drop point and the DG bot gets
  `setSmPosHpr` + `setSmStop`. Chat lines appear as `net: rx chat sc 1 from ... queued 15 ms`.

### Asset size decisions
- None this round.

### Known issues
- The latency numbers are emulator numbers on a shared rig whose VM pauses (one run showed a
  463 s pause and was discarded). The net thread still waits up to 10 ms in `poll()` before
  noticing a queued send; a hardware run should decide whether a shorter idle poll is worth
  the extra soc:U calls.
- The NTP one-way estimate assumes a symmetric path; the 3DS send side is the faster one, so it
  can understate receive latency by up to half the asymmetry. The `queued` figure is exact.
- Other trees on this box still run the old `azahar_test.sh` with the global pkill; their runs
  can still kill each other (this tree's emulator is now immune).
- `AUTOTEST_ONLY` can include steps whose drivers assume earlier steps ran (the `move: `
  door driver walked into the Gag Shop in a filtered run).
- Not run this round: facility/boss chunks. No real-hardware run yet.

### Next
- Hardware run: frame pacing, latency, the idle poll timeout.
- Two-client cross-play with the web client on the same server using `latency_run.sh`'s logs.

## 2026-10-28 (round 6) — the rig "stall" explained, boss rounds reached, boss UI fixes

### Done
- **The Azahar "emulated-clock stall" of rounds 3-5 is the rig's VM being paused**, not the
  client. A `date`/`sleep 30` probe beside the runs showed a 30 s sleep taking 189 s (Azahar got
  19 s of CPU in that window), and later a 34-minute gap. When the VM resumes, Astron's
  wall-clock heartbeat timer ejects the client (345). I first blamed Azahar's timed
  `poll()` (HLE `RunAsync`) and changed the client to zero-timeout polls; the stall came back
  in the next run, so that change is reverted (`eead0ce`). The server now runs with
  `ASTRON_HEARTBEAT_TIMEOUT_MS=3600000` and the lesson is in AGENTS.md.
- **Cog pool too small for boss rounds.** Boss rounds generate every Cog at once: the VP
  ~36, the CFO 71 (37 + 34). With 16 client slots, 20 VP Cogs were dropped. Reserves that
  joined had no DNA, gave 16 "Unknown by suit" attack movies, and every CFO battle Cog was
  invisible while it hit the toon. Now 80 client slots and 96 actors (static data 3.0 MB).
- **Boss/Cogdo bottom screen was broken polygons.** The VP/CFO, CJ, CEO and Cogdo frame owners
  drew the citro2d UI without `C2D_Prepare()` after the world shaders (an AGENTS.md rule).
  The battle panel and info panel now draw correctly.
- **No laff meter during bosses and Cogdo.** Those frame owners never drew the HUD. They now
  draw the same laff meter/toast as the main loop (one shared `baseHudValues` helper), and a
  toast ages out while they own the frame.
- Facility floor and zone loads read their record tables in blocks. Each floor triangle
  (up to ~850 a floor), TTM texture/batch record and TZ chunk record used to cost its own
  seek + read, an SD round trip on the main thread.
- Host tools: `zone_test` skips degenerate floors (zone 17000 has a zero-area one; the test
  failed on every full run). `decode_shot.py` creates `--out-dir`; it printed "wrote" into a
  missing directory.

### Verified (Azahar 2126.2, Old 3DS mode, Linux/Xvfb, live server, `AI_DEV_STRONG`, weak Cogs)
- **Sellbot VP, first time on any rig**: lobby → elevator → Introduction → BattleOne, two runs;
  gag movies, reserves joining, 26-27 fps, <= 79 draws / 16.2k tris, heap 3.3-3.5 MB, linear
  free >= 14 MB. Run 1 (16 slots) showed the dropped Cogs; run 2 showed the bottom screen fix.
  The solo test toon went sad in BattleOne (run 1). Run 2 was cut by a VM pause. BattleTwo and
  the pie phase were not reached.
- **Cashbot CFO, first time on any rig**: lobby (12100) → elevator → Introduction → BattleOne.
  The laff meter shows on top and the battle panel on the bottom. With 80 slots: no dropped
  Cogs, Cashbots and skelecogs visible in the movies. One run was cut by a VM pause, the next by
  another session's emulator run (shared rig).
- Host: nettest (all groups; one flaky failure under load, then PASS), boss_test,
  zone_test (all zones incl. 17000). `make`: zero warnings in `source/`.

### Asset size decisions
- None this round.

### Known issues
- Boss rounds past BattleOne (VP BattleTwo/pies, CFO cranes, CJ, CEO) are still unverified in
  an emulator. VM pauses and the solo test toon's attrition end the runs first.
- In boss battles the reserves-joining stretch shows the empty arena from a held camera.
  Upstream films the reserves arriving; not yet compared.
- A facility's first floor entry still spends ~0.45 s in `Level::activate` (props, 15 sfx)
  behind the entrance loading panel. Street loads now measure 0.2-0.27 s (the 1.0-1.3 s of
  earlier entries is stale).
- The tailor keeps the clerk camera (the counter is baked into the interior mesh).
- No real-hardware run yet; all frame numbers are emulator numbers.

### Next
- With the server's 1 h heartbeat timeout and an active session, run CFO, CJ and CEO rounds
  in chunks. Give the test toon a teammate (the PC bot) so the VP/CFO Cog rounds can be won.
- First real-hardware run.

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
