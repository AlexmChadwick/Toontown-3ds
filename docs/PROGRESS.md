# Progress log

Dated entries: what was done, how it was verified, and what is next. Test numbers live in
`docs/test-log.md`; architecture and decisions in `AGENTS.md`.

## 2026-10-29 (UI round 13) — battle Fire/SOS, ToontownTimer, indoor Map, fitted NPC balloons

Round 12 (`2b6b93a`, no entry of its own) moved the laff meter to a pocket at the left of the
bottom tab bar, added per-toon laff cards to the battle panel (species head in the head colour,
laff, this round's pick from `setChosenToonAttacks`) and hid the top-screen meter while the bottom
screen shows ours.

### Done
- **Battle picker: InventoryNew's Fire / Pass / Run / SOS block.** The four buttons sit in
  upstream's 2 x 2 arrangement (Fire | Pass over Run | SOS) in its image_color (0, 0.6, 1); Fire is
  grey without pink slips. Cog cards and gag rows are a few pixels shorter, which lets the Toon
  laff cards grow to 40 px.
  - **SOS** opens TownBattleSOSPanel's Rescued Toons: track-coloured cards with name, stars and
    "N Remaining", eight a view with a page button (or L / R), cards greyed when out of calls or
    when a Lure / Trap card cannot help (checkHealTrapLure). A card sends
    `requestAttack(NPCSOS, -1, npcId)`. With no cards: "No friends to call!" (TTLocalizer).
  - **Fire** opens FireCogPanel on the Cog cards ("Fire which Cog?", "PINK SLIPS LEFT: n") and
    sends `requestAttack(FIRE, -1, suitId)`.
  - B / BACK returns to the attack panel with the focus held before (padnav), and the status bar
    shows each panel's title.
- **SOS and Fire rounds play.** The movie used to skip every track outside 0..6, so a cross-play
  partner's (or our) Fire / SOS / NPCSOS / PETSOS round showed nothing. Rounds now follow
  Movie.__doToonAttacks: Fire (throw, "Fired!", the Cog's defeat), SOS ("<friend> HELP!"),
  NPC SOS Toons (MovieNPCSOS's greeting, then their gag as that track and level in its track's
  place; Toons Always Hit / Cogs Always Miss / Restocking announcements), pet tricks (heals).
- **Battle camera keeps the battle's own timing.** `Battle::buildMovie` records each step and
  BattleCamera frames those steps; the camera used its own durations and drifted after every Cog
  defeat (and would have drifted on every special).
- **Minigame countdowns use ToontownTimer's alarm clock** (`bake_timer_clock.py`: phase_3.5
  `clock_gui` / `alarm_clock`, 128 x 128 RGBA8, 64 KB RomFS) with OTPTimer's per-digit label
  geometry, black digits, red for the last five seconds. The info card splits "Time: N" out of
  every game's status lines (all 15 formats), so no game needed changing.
- **Map indoors** shows the playground or street outside, its markers and legend, with our arrow
  at the door we came in by (an interior is `zone - zone % 100 + 500 + block`); arriving indoors
  some other way shows that map without the arrow. Entering a playground shop reloads nothing.
- **NPC dialog balloons fit their text** (word-wrapped once per page with the UI font) instead of
  a fixed 176 px box around "Goodbye!".
- **Locked gag tracks keep their faded track colour** in the battle picker, book Gags page, Gag
  Shop and trolley purchase (upstream tints every row and only hides the buttons).
- Autotest: SOS by touch and back; `pad: battle demo` steps show sample Rescued Toons and pink
  slips for the panels (display only; those actions are dropped); a quest-page shot series; the
  `move: door` step runs into the Gag Shop door like a player (after the trolley return it stood
  on the flush trigger forever, so the shop steps never ran from that point).

### Verified (Azahar 2126.2, Old 3DS, Linux/Xvfb, shared live server, fresh tokens)
- Street battle, `AUTOTEST_ONLY="see cog,pad: battle"`: SOS opened by touch and closed with B
  (focus restored), demo SOS cards on two pages, the Fire panel, then a gag chosen with the pad;
  `requestAttack` lines as expected, no PARSE / EJECT. 27 fps while shooting every second
  (shot I/O ~109 ms), heap 3.4 MB, linear free 15.6 MB, VRAM free 4.2 MB.
- Trolley → Catching game with the clock counting down (red at 5..0), gag purchase, back to TTC,
  Gag Shop door → shop opens; Map in the shop shows TTC with the arrow at its door.
- Toon HQ talk: short pages in short balloons. Front end (`AUTOTEST_FE=1`) re-checked.
- Host: padnav_test 154 checks, battle_cam_test passes (GCC 12); `make`: zero warnings in source/.

### Asset size decisions
- `timer_clock.ttm`: the original clock rendered at 128 x 128 RGBA8 (64 KB) so it stays sharp at
  88 px on the bottom screen; no other new assets.

### Known issues
- **SOS cards and Fire were never sent to the live server**: this stock server gives a fresh toon
  no Rescued Toons (VP only) and no pink slips (CEO only), and the rig cannot reach those bosses.
  The panels were shown with sample data; the request layout matches DistributedBattleBase.
- SOS / Fire movies are simplified: the SOS Toon does not teleport in (the calling toon performs
  its gag, with the NPC's greeting over it) and a fired Cog plays its normal defeat instead of
  flying off. Friend SOS cannot be offered (no FriendManager on the stock server, so no friends),
  and `battleSOS` is not sent.
- Long NPC dialog pages were not on screen this round (the test toon's tasks only gave short
  pages); the balloon sizing uses the UI font's 30 px line, the same as the old wrap.
- Locked gag tracks were not seen live: this dev server gives every test toon every track.
- The battle round timer is still the yellow number pill (a 30 px ToontownTimer would cover the
  fourth Cog card's name).
- The earlier known issues stand (no real-hardware run; Azahar clock stalls on this rig).

### Next
- A VP rescue and a CEO win on a faster rig, to send a real NPC SOS and Fire.
- MovieNPCSOS teleport-in with the NPC's own toon model; MovieFire's propeller exit.

## 2026-10-28 (round 8) — SpeedChat ToonTask phrases on the wire, Cog health light, RewardPanel, Toontown buttons

### Done
- **SpeedChat TOONTASKS > MY TASKS works end to end.** Round 7 built the menu rows but never sent
  them, and remote task phrases were dropped as unknown fields. The client now sends
  `setSCToontask` (said aloud) and `setWhisperSCToontaskFrom` (whispers), and receives both:
  phrases are decoded with `decodeTTSCToontaskMsg` into the speech balloon and whisper log. New
  `net: chat tx/rx sctask ...` lines. The rows rebuild when our quests change.
  `tools/sc_test/run.sh` now also drives the real Client (rx, tx byte layout, truncated
  payloads), and its binary is no longer committed. Host test scripts that link `source/net`
  add the quest sources.
- **Battle Cog cards**: the head icon (suit_heads atlas), and a meter in the Cog's own chest-light
  colours (`Suit.healthColors` / `healthGlowColors`, with `updateHealthBar`'s thresholds and its
  red/grey blink when critical or defeated). This replaces the flat grey slab. Before the max HP
  is known the meter is drawn full, not empty. New `battle: cog <id> <code> hp a/b` log line.
- **Battle reward panel follows `RewardPanel.incrementExp`**: the bar shows absolute experience
  against the next level, with "6000/10000" centred in a track-coloured bar on a darker frame
  and "N to Go!" past 10000. Before, it showed only the progress inside the current level, so most
  bars looked empty. The panel now uses the space the defeated Cogs leave.
- **Front-end buttons are Toontown's glossy `quit_button` pills** (`theme::pill`; colours sampled
  from the QuitBtn_UP / QuitBtn_RLVR textures). They are yellow at rest, and the cyan rollover
  face marks the current choice or the primary action. Delete is red, disabled grey.
- The Map tab logs a missing `.tmap` instead of failing silently.
- Autotest: SpeedChat TOONTASKS > MY TASKS with the D-pad, then say the first phrase.

### Verified (Azahar 2126.2, Old 3DS, Linux/Xvfb, shared live server)
- MY TASKS: rows "I need to finish a ToonTask." / "...see an HQ Officer." / "...go to a Toon HQ."
  for the trolley task. `net: chat tx sctask 110/1000/1000/0`; the balloon and recent-chat strip
  show the phrase.
- Street battle in Silly Street: Cog cards with heads and blinking lights (this server runs
  `AI_DEV_WEAK_COGS`, so every Cog has 1 HP; log `hp 1/20`), the reward panel, and the Map with
  the baked street minimap and legend.
- Front end with a fresh account: title, Pick-a-Toon, all five Make-a-Toon steps, Toontorial
  prompt, delete prompt.
- Host: sc_test (3152 vectors plus the Client checks), nettest unit (391 descriptor vectors),
  padnav_test (137 checks), all built with GCC 12. `make` has zero warnings in source/ (it had
  one from round 7).

### Asset size decisions
- None. The Cog card reuses the existing 32 KB suit-heads atlas.

### Known issues
- Receiving `setSCToontask` from another client is covered only by host tests; no second client
  said a task phrase during this round's runs.
- During street battles with `movie_*` shots every 0.25 s, Azahar runs at 18-22 fps (shot I/O
  takes about 109 ms per dump). Run with `shots off` for frame numbers.
- Only the front end uses the new pills. In-game panels keep the raised-lip `theme::button`.
- Agents on this box share `/tmp`: use unique output dirs for `azahar_test.sh` runs. Reused
  names like `/tmp/ui_shots1` mixed this round's shots with another agent's.
- The earlier known issues stand (no SOS/Fire in battle, no real-hardware run).

### Next
- Two-client check of task phrases (3DS and web / stock Panda).
- Bring the pill style (or upstream's InventoryNew buttons) to Pass/Run and the map buttons, if it
  reads well next to the paper panels.

## 2026-10-28 — bottom-screen UI: upstream Map rules, markers, task cards, Name Shop, purchase grid

### Done
- **Map tab and book Map page follow upstream MapPage.** The client now reads
  `setTeleportAccess` / `setHoodsVisited`. Hood buttons are blue (teleport), paper (visited, walk
  there) or grey; a locked press explains itself in the place label instead of teleporting. A map
  teleport sends `checkTeleportAccess(hood)` first, as `MapPage.__buttonCallback` does (the
  3DS used to teleport anywhere without it: a protocol divergence, now fixed). The left button
  is "Playground" / "Cog HQ" off a playground (`backToSafeZone`) and "Go Home" otherwise,
  greyed when the server has no EstateManager (stock Open Toontown) instead of a dead button.
- **Map markers from data.** A hardcoded TTC-only marker list (Phase 1) is replaced by markers
  built per zone from the baked `.pts` anchors: Toon HQ, shops, trolley, fishing docks, tunnels,
  plus other players, with a legend in the map's empty right margin.
- **Toon tab task card**: a finished task names the task and "Return to: <NPC>" and the place,
  like the book page (it said only COMPLETE and a location).
- **Options page**: the Debug-tab toggle and test-toon stepper show only after SELECT turns the
  Debug tab on, so players never see developer rows.
- **Trolley gag purchase**: gag icons in the grid and the Gag Shop's info line (have / added /
  bag full); the Gag Shop now also says when the bag is full.
- **Name Shop**: drawn check boxes for Title / First / Last name (was "Title [X]" text), and one
  Last-name box (Prefix and Suffix were two buttons for one flag). "Color", not "Colour".
- Toontown capitalisation in battle and front-end strings ("The Cog wants a fight!", "Waiting
  for other players..." from TTLocalizer).
- `dcgen`: names `setTeleportAccess` / `checkTeleportAccess` (DC hash unchanged).
  `tools/padnav_test/run.sh` honours `CXX`.

### Verified
- Azahar 2126.2, Old 3DS, Linux/Xvfb, live shared server: Map in TTC, Silly Street, DD and DG;
  Toon tab, Options, street battle panel, trolley purchase, the whole front end. Map tab 29.9
  fps, heap 3.2 MB, linear free 15.6 MB. Details in test-log.
- Host: padnav_test (137 checks), nettest unit + descriptor vectors (391). The fake-server part
  of nettest needs `ref/ref/open-toontown` and was not run.

### Asset size decisions
- None. The `.tmap` minimaps (32 KB each) were baked for testing only, outside the tree.

### Known issues
- **No SOS or Fire in the battle panel.** Upstream's TownBattle has SOS (friends, rescued NPC
  Toons) and Fire (pink slips); the client has no NPCSOS/FIRE request or movie support. Rescued
  Toons come only from the VP, which this rig cannot reach (emulator stall).
- Teleport access is not enforced in autotest runs (fresh test toons have none); the gating was
  checked by eye with a fresh toon.
- This rig's data set has no `interiors/`, so the door/clerk steps were not re-run this round.
- The Debug tab itself keeps its dark developer palette (it only appears after SELECT).

### Next
- Battle SOS (NPC Toons) once a VP rescue can be tested; Fire with pink slips.
- Re-check the Map back button inside a Cog HQ lobby and a facility on a full data set.

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
