# AGENTS.md — Toontown 3DS

Guidance for humans and AI agents working on this repo. Read fully before changing
code. When a decision here changes, update this file in the same change.

## 1. What this project is (and is not)

A **native C++ Nintendo 3DS client** that speaks the Astron client protocol to an
**unmodified Open Toontown server** (Astron + UberDOG + AI, sv1.0.47.38). It renders
pre-baked versions of the original assets.

It is **not** a port of Panda3D, it does not embed Python, and it does not run the
upstream client code. The upstream Python is a **specification** that we read and
re-implement in a small, focused way.

Toolchain: devkitARM, libctru, citro3d, citro2d, picasso. Output: `.3dsx` and `.cia`.
Nothing else is linked unless it is added to this file with a justification.

## 2. Upstream facts (verified in Phase 0)

These come from `ref/open-toontown` (code) and `ref/resources` (assets), both cloned
on 2026-10-23.

### Assets
- **8.6k files, 668 MB.** File counts:
  - 3980 `.bam` (BAM v6.24)
  - 2560 `.jpg`, 792 `.rgb` (SGI), plus a few `.png` (textures; alpha often in a separate `.rgb`)
  - 1034 `.ogg` (audio)
  - 93 text `.dna` files
- **Stock `panda3d==1.10.15` loads these `.bam` files.** This is why the asset pipeline
  runs offline in Python and the runtime never sees `.bam`.
- **DNA is text.** `storage*.dna` maps a `code` to a node in a `.bam` (for example
  `store_node ["wall" "wall_lg_brick_ur"]`).
  - Zone files place props, landmark/flat buildings, walls, windows, cornices, doors,
    signs and so on.
  - Street files (`phase_5/dna/toontown_central_2100.dna` and others) are split into
    **visgroups** with explicit `vis [...]` lists. That is a ready-made potentially-visible
    set (PVS) and our streaming unit.
  - Buildings are assembled procedurally from DNA (`flat_building` = wall stacks + windows
    + cornice). Upstream does this in C++ `libtoontown` (not in the repos), so **we
    re-implement DNA assembly in the offline baker.**
- **Measured model costs:**

  | Asset | Tris | Other |
  |---|---|---|
  | `toontown_central.bam` (playground base terrain) | 1205 | 52 geoms, 22 textures |
  | DNA props and buildings on top | | about 70 props, 8 landmarks, 5 flats |
  | `gagShop_TT.bam` | 772 | 85 geoms, 25 textures (3.5 MB raw) |
  | Toon, dog M-shorts, LOD 1000 | about 1490 (legs 590, torso 470, head 430) | |
  | Toon, LOD 250 | about 390 | |

  - The **85 geoms** in the gag shop make it draw-call bound, so baking must merge
    batches and atlas textures.
  - Toon skeleton: legs have **11 joints**, which fits a PICA200 uniform bone palette.
  - Animations run at 24 fps (for example run = 16 frames, neutral = 50 frames).
  - Toons are three separate actors (head, torso, legs) attached at joints, with
    per-species and per-size variants.

### Network (Astron client protocol)
- Client agent: TCP **7198**. Every message is framed as a `u16 length` prefix followed
  by the body. Little endian throughout.
- `CLIENT_HELLO(1)`: `u32 dcHash, string "sv1.0.47.38"`.
  - The DC hash is **547155168**, computed by Panda's `DCFile.getHash()` over `otp.dc`
    then `toon.dc`.
  - **Never hand-edit IDs.** All class and field numbers come from
    `tools/dcgen/dcgen.py` → `source/net/dc_generated.h`.
- **Login.** `AstronLoginManager` (doId **4670**, the only anonymous uberdog) handles it.
  - Send `requestLogin(token)`. The server replies `loginResponse(JSON blob)`.
  - A dev server accepts any token and auto-creates the account.
  - One request may be outstanding at a time. Violations, bad DNA and duplicate logins
    are **ejected** by the server.
- **Avatars.**
  - Before asking for the list: `addInterest(4618, zone 3)` → `requestAvatarList` →
    `PotentialAvatar[]`.
  - `createAvatar(15-byte DNA netString, slot)`
  - `requestPlayAvatar(avId)` → `CLIENT_ENTER_OBJECT_REQUIRED_OTHER_OWNER (173)` for our
    `DistributedToon` (class 65).
- **Districts.** `DistributedDistrict`s appear under interest (4618, 3).
- **Entering a zone.** For example the TTC playground, zone 2000:
  `CLIENT_OBJECT_LOCATION(avId, shard, 2000)`, then interest (shard, 2), then
  interest (shard, 2000).
- **Movement.** These fields are `ownsend broadcast`:
  - `setSmPosHpr` (90): X/Y/Z as `int16/10`, H/P/R as `int16%360/10`, T in 16-bit
    centiseconds. Send it at **5 Hz** while moving.
  - `setSmStop` (81): send it when movement stops.
  - `setAnimState("run"|"walk"|"neutral", int16/1000 multiplier, T)` (479).
- **Heartbeat.** Send `CLIENT_HEARTBEAT(5)` every 10 s. The server times out at 60 s.
- **Anti-cheat.** Sending any field that is not `clsend`, or not `ownsend` on our own
  object, gets us disconnected. The client must whitelist outgoing fields at compile
  time.

## 3. Hardware targets and budgets

The baseline is **Old 3DS**:

- 268 MHz ARM11, effectively one core.
- PICA200 GPU.
- 64 MB app memory (`SystemMode 64MB`, APPMEMTYPE 0).
- 6 MB VRAM.

New 3DS may enable `osSetSpeedupEnable(true)` (804 MHz + L2), more draw distance or LOD
bias. Nothing may *depend* on it.

### Memory budget (Old 3DS, enforced in `source/core/memory.*`)

| Pool | Budget | Holds |
|---|---|---|
| Code + static data | ≤ 4 MB | `.text`/`.data`/`.bss` (watch the `.map` file) |
| Regular heap (`__ctru_heap_size`) | 24 MB | game state, net buffers, decompression scratch, UI |
| Linear heap (`__ctru_linear_heap_size`) | 24 MB | vertex/index buffers, textures, cmd buffers, audio buffers |
| VRAM | 6 MB | render targets (top RGBA8 + D24S8 ≈ 0.75 MB) + hottest textures |
| Headroom | ≥ 10 MB | system, HBL, fragmentation |

Sub-budgets inside linear memory (asset streaming enforces them; each is a hard fail in
debug builds):

| Sub-budget | Size |
|---|---|
| Current zone geometry | 6 MB |
| Current zone textures | 8 MB |
| Toons (local + up to 8 remote, shared parts) | 4 MB |
| UI + font atlas | 2 MB |
| Audio | 2 MB (music streamed, never fully resident) |
| Reserve | 2 MB |

**Measure, don't guess.** The bottom-screen debug overlay always shows heap, linear and
VRAM usage plus frame time. Every Azahar or hardware test logs those numbers in
`docs/test-log.md`.

### Frame budget
- **Target 30 fps locked** on Old 3DS, which is 33.3 ms per frame. Never trade
  stability for 60 fps.
- **Rough GPU envelope** to validate in Phase 1: ≤ 15k triangles and ≤ 150 draw calls
  per frame on the top screen.
- **CPU per frame:** ≤ 6 ms for animation and skinning, ≤ 3 ms for networking.
- **Draw distance:** fog, plus the far plane at about 120 units in the playground.
  Streets use visgroup PVS (only `vis[]` groups of the current cell are drawn and
  resident).

## 4. Architecture

```
source/
  main.cpp          app lifecycle, main loop (fixed 30 Hz sim tick, render once per vblank pair)
  core/             memory budgets, logging (to sdmc:/3ds/toontown3ds/log.txt), timing, config
  render/           citro3d world renderer (top), citro2d UI renderer (bottom), texture/mesh cache
  world/            baked-zone loader + streamer, collision (ground + walls), camera, toon controller
  anim/             skeletal animation (baked joint tracks, GPU skinning, LOD)        [Phase 1-2]
  net/              socket thread, Astron framing, datagram pack/unpack, dc_generated.h,
                    DO table (doId → object), login/avatar/zone state machine        [Phase 3]
  ui/               bottom-screen panels: status, minimap, chat stub, settings      [Phase 1-2]
shaders/            .v.pica (static mesh, skinned mesh, ui)
tools/pipeline/     offline asset baker (Python + stock panda3d)
tools/dcgen/        DC → C++ header generator
```

Key decisions:

1. **Offline everything.** `.bam`, `.dna`, `.jpg`/`.rgb` and `.ogg` are all converted on
   the PC into our own formats. The runtime has no DNA parser, no BAM reader, no JPEG or
   Vorbis decoder, and no DC parser.
   - DC: a generated header holds IDs and the hash.
   - Each received class gets a **generated field-skip table**, so unknown fields in
     `ENTER_OBJECT_*_OTHER` can be skipped safely. This is added in Phase 3 by extending
     dcgen.
2. **Baked zone format (`.tz`, little endian, versioned).**
   - Contents: a header, a texture table (PICA-native formats, pre-tiled), mesh chunks
     per cell, a collision mesh per cell, spawn/landmark points, and minimap data.
   - Cells: playgrounds are split into a uniform grid. Streets use DNA visgroups.
   - Each chunk is one contiguous blob that can be read straight into `linearAlloc`
     memory.
   - **TZ v3** (current) stores each distinct texture payload once in a per-file pool;
     chunk TTMs point into it (`docs/design/streets.md`). Interiors share one
     `interiors/textures.ttp` (pooled TTM v2, `tools/pipeline/ttm_pool.py`). Never let a
     baker copy the same atlas page into every chunk file again: that doubled the SD set.
3. **Baked mesh data.**
   - Positions are quantized to `int16`; UVs use `int16` or `u8`.
   - Vertex color is `u8×4`.
   - Indices are `u16`, as triangle lists or strips.
   - Batches are merged per texture after atlasing. The ≤150 draw calls per frame target
     drives this.
4. **Textures.**
   - Max 128×128 for props and buildings, 256×256 for terrain and atlases, mipmapped.
   - Format by use:
     - **ETC1** for opaque textures.
     - **ETC1A4** when there is alpha.
     - **RGBA4** for UI.
     - **L8/A8** for Toontown's 1-channel palettes. `avatar_palette_1lmla` is an alpha
       mask, so the original "colorable" textures stay small.
   - Tiling and encoding happen offline (port the tex3ds approach; do not shell out to
     the runtime).
   - **Current:** textures are ETC1/ETC1A4 by default (`tools/pipeline/etc1.py`), and
     1–2 channel textures stay L8/LA8/A8. `TT_ETC1=0` restores 16 bpp for A/B tests.
   - **Phase 1 status (historical):** the baker (`tools/pipeline/ttm.py`) emitted 16 bpp formats only:
     RGB565, RGBA5551 (binary alpha), RGBA4, LA8, L8, A8. Each has a full mip chain down
     to 8×8. ETC1/ETC1A4 (4/8 bpp) is a planned Phase 2 follow-up, since TTC textures alone
     are about 1.5 MB at 16 bpp.
   - **Texture orientation:** rows are stored top row first, untiled order = image file
     order. This matches tex3ds; the first row in memory samples as t=1, which equals
     Panda's v=1.
   - **Panda `TransparencyAttrib.M_dual`** is baked as alpha test (>0.5) instead of
     sorted blending.
   - **"Draw in front"** (eyes over face, pupils over eyes) uses a per-batch `layer`
     that adds a depth bias (`kLayerDepthBias` in `render/renderer.cpp`).
5. **Toons.**
   - Runtime animation catalog: 51 stable clip IDs, shared model/clothing cache,
     bounded two-frame active/staging windows (12 three-part groups, 576 KiB).
     The complete Toon cache has a hard 4 MiB admission limit. Call `ToonCache::pump`
     before GPU frame submission; it fences publication and retired-resource reuse.
     Whole Toon action files are never eagerly loaded. Battle `playClip` uses this
     same pool. See `docs/design/toon_anims.md` for controls, hooks and validation.
   - Animation tracks are baked per (species head, torso size, legs size) at a reduced
     rate (12 fps, interpolated), for a minimum set: neutral, walk, run, jump.
   - Skinning runs on the GPU, with ≤ 20 bones per draw (PICA has 96 float uniforms;
     4×3 matrices).
   - Color (DNA `armColor`, `legColor`, `headColor`) is a per-draw uniform. Clothes come
     from an atlas.
   - LOD 250 or 500 is used for remote toons, and 1000 only for the local toon on New 3DS.
6. **Threads.**
   - The main thread runs simulation and rendering at prio 0x30.
   - The net thread is a blocking socket with a ring buffer, prio 0x31, on core 0.
     Never core 1 unless New 3DS is checked and the code is guarded.
   - The optional streaming I/O thread reads from SDMC or RomFS into preallocated
     buffers. No allocation happens off the main thread.
   - The APT hook pauses the sim and net heartbeat logic correctly on HOME and sleep.
     Heartbeats keep flowing while suspended where possible; otherwise we reconnect.
7. **Storage.** A small boot set goes in RomFS (UI, fonts, the default toon). Zones and
   audio live on SD at `sdmc:/3ds/toontown3ds/data/` and are streamed, so the CIA and
   3DSX stay small.
8. **Render rules.**
   - Every target is cleared every frame (`C3D_RenderTargetClear` / `C2D_TargetClear`).
   - Call `C2D_Prepare()` before 2D drawing after any custom shader use.
   - Use one `C3D_FrameBegin`/`C3D_FrameEnd` pair per frame.
   - Stereo 3D is off by default (`gfxSet3D(false)`). When it is enabled in settings, the
     top screen renders twice with an IOD from `osGet3DSliderState()`. That roughly
     halves the triangle budget, so stereo forces LOD down.

## 5. Dual-screen and control design

**Top screen (400×240).** Shows only the 3D world, nametags over toons, and a minimal
HUD (laff meter, a toast for incoming chat).

**Bottom screen (320×240, touch).** It is never blank and never a mirror of the top
screen. Tabs along the bottom edge:

| Tab | Contents |
|---|---|
| Map | minimap: top-down baked image of the zone, our arrow, other toons as dots |
| Chat | SpeedChat-style tree menu (touch); typed chat comes later |
| Toon | name, laff, jellybeans, DNA preview |
| Gags | later phase; a placeholder in Phase 1-2 |
| Settings | stereo on/off, LOD bias, server address, debug overlay |
| Debug | memory, frame time, net RTT/bandwidth (default on during development) |

**Controls:**

| Input | Action |
|---|---|
| Circle Pad | move (forward/back = walk/run by magnitude; left/right = turn, Toontown-style tank controls) |
| L / R | rotate the camera around the toon; double tap recenters |
| A | jump |
| B | hold to walk slowly or cancel |
| X | toggle the chat tab |
| Y | toggle the map tab |
| D-Pad | UI navigation on the bottom screen (for players who don't use touch) |
| START | menu; SELECT: debug overlay |
| C-Stick / ZL / ZR (New 3DS, optional) | free camera orbit / zoom |

The game must be fully playable without the New 3DS extras.

**Pad navigation** (`docs/design/pad_navigation.md`). Every bottom-screen control is reachable
with buttons through `ui/focus.*` and the per-panel focus tables in `ui/pad_tables.*` (host
tested by `tools/padnav_test/`).
- On the Map, Toon and Setup tabs nothing is focused until D-Up or D-Down is pressed, so A
  still jumps; B lets go of the focus.
- Panels that own the screen (`ui/panel_owner.h`) take the D-Pad; A presses the focused
  control and touch moves the focus.
- B closes the book; L / R turn book pages and cycle the battle target.
- Every focus change and press is logged as `padnav: <panel> focus <n>` /
  `padnav: <panel> activate <id>`.

## 6. Phase plan and milestones

Each phase ends with a stop-and-report: test-log entries plus an update to this file.

**Phase 0 — Analysis + skeleton.** Status: done.
- This file.
- A skeleton that clears both screens and shows memory stats.
- The Makefile (3dsx + cia), the RSF and `dcgen`.

**Phase 1 — Renderer skeleton.** Status: done in Azahar (Old 3DS mode). Not yet run on
hardware. See `docs/test-log.md`.
- M1.1 ✅ The build runs in Azahar in Old 3DS mode. Baseline: 133 KB heap, about
  4 MB linear, 30 fps locked.
- M1.2 ✅ World shader (`shaders/world.v.pica`), `.ttm` models loaded from RomFS, a
  follow camera and tank controls. The baked-model format is `.ttm`, not `.tz`; zone
  chunking (`.tz`) arrives with M2.1.
- M1.3 ✅ `bake_model.py` handles the TTC terrain, the gazebo and the mailbox. Textures
  are 16 bpp, not yet ETC1 (follow-up below).
- M1.4 ✅ The dog/M/shorts toon at LOD 500, posed at frame 0 of `neutral`. Body, shirt
  and shorts colours apply through tint uniforms. The Toon tab picks colours.
- M1.5 ✅ Five tabs: Map with markers, Chat placeholder, Toon colours, Setup (stereo
  toggle, stress count) and Debug (cached text).
- M1.6 ✅ (emulator only) Stress test up to 64 toons: 938 draws, 41k tris, 52%
  command buffer, 30 fps held. About 0.3 ms CPU per toon (emulated; varies ±40% by
  Azahar version). **The section 3 envelope stays provisional until it is measured on
  hardware.**
- Follow-ups carried into Phase 2:
  - ETC1 texture encoding.
  - Real hardware measurements.
  - A test of the stereo toggle.
  - Sleep/HOME resume test.
  - Why `C2D_DrawCircleSolid` (proctex) fails after the 3D pass.

Lessons recorded for agents:
- **Command buffer.** The citro3d command buffer overflows into an svcBreak PANIC.
  Keep per-draw state changes minimal: one `BufInfo` per model (indices are rebased to
  absolute at load), and `C3D_GetCmdBufUsage()` guards each model.
- **State leaks into citro2d.** `C2D_Prepare()` does not reset alpha test or the depth
  map. Call `Renderer::endWorld()` before any citro2d drawing.
- **Text is expensive.** Parsing citro2d text costs milliseconds of CPU; cache
  `C2D_Text` for anything not changing every frame.
- **Verify without a human.** Touch `sdmc:/3ds/toontown3ds/autotest` to run the
  scripted pass. Render-target dumps and `tools/dev/decode_shot.py` give screenshots
  that don't depend on the host screen.
- **One Azahar run at a time.** `tools/dev/azahar_test.sh` takes a lock and restores the user's
  Azahar config on exit (including INT/TERM).
  - Overlapping runs once killed each other's emulator (it looked like app crashes) and left
    the user's config in test mode.
  - `make` does not repack RomFS when only a data file changes; delete the `.3dsx`.
  - Never rebuild while Azahar runs the tree's `.3dsx`: the emulator reads RomFS from that
    file on demand, so a relink mid-run feeds the test shifted data. Copy it first
    (`cp toontown3ds.3dsx /tmp/run.3dsx; tools/dev/azahar_test.sh /tmp/run.3dsx ...`).
- **Main-thread stack.** It is 256 KB (`__stacksize__` in `main.cpp`). `main()` keeps a 16 KB
  frame of subsystems; the 32 KB default crashed at startup (PC 0).
- **CPU faults** on the main thread are logged by `core/crash.*` (type, pc, fault address,
  registers).
- **Street interest includes the branch zone** (`Street.doEnterZone`): the cell, its vis list
  and `zone - zone % 100`, which holds every `DistributedBuilding`. Without it, buildings,
  doors and field offices vanish on the first cell change.
- **No teleports while busy.** Teleports are refused on the trolley, in a minigame or its gag
  purchase, and in battle (the book is disabled there upstream). Leaving mid-purchase stranded
  the session for the rest of the game.
- **Check what the stock AI really creates before building a feature.** `EstateManagerAI`,
  `DistributedPhoneAI`, `DistributedMailboxAI`, `DistributedTrunkAI` and `CatalogManagerAI` are
  stubs on this server, so estates, the catalog, mail and accessories cannot run. The `catalog`
  branch is parked unmerged.
- **Bakes are order-independent.** Every asset must bake to the same bytes alone or in a batch.
  Dynamic (`.ttf`) fonts need a fresh copy per sign; process-wide caches key on content.
- **QA by play-through.** Run the world script in chunks split at `go ...` steps (each facility
  from boarding) with `AI_MINIGAME_ID=8`, and review the logs and every shot. Fix root causes
  first: one stuck panel or session can fake dozens of "wrong place" shots.
- **Emulator time must keep up with real time.** Run Azahar passes alone: parallel builds,
  host suites or App Nap (a hidden Azahar window) slow emulated time. The client's 10 s
  heartbeats then arrive minutes apart, and Astron ejects it with code 345. That fakes
  "connection closed by server" bugs. `azahar_test.sh` turns App Nap off for the run, and
  agent builds run under `nice -n 19`. Compare log time with real time before blaming the client.
  On the Linux/Xvfb rig (software GL) facilities run at ~25-40% speed even alone; start the
  server with `ASTRON_HEARTBEAT_TIMEOUT_MS=300000` for those passes.
- **3DS sockets differ from Azahar** (`net/net_platform.cpp`). SOC has no POLLERR/POLLHUP: 0x08
  is POLLWRNORM (the real "writable" bit, which libctru's poll.h calls POLLERR) and 0x10 is
  POLLWRBAND (libctru's POLLOUT). `getsockopt(SO_ERROR)` returns the raw SOC -26 after a good
  connect (libctru#412). Connect completion is decided by calling `connect()` again (0 / EISCONN),
  never by SO_ERROR or POLLERR. Every connect failure names its step, errno and revents on screen
  and in log.txt.
- **Every hand-parsed field needs a layout check, and every handled class must be in the
  table.** Golf was dead twice over:
  - `DistributedGolfKart.setState` was read without its i16 timestamp.
  - The golf classes were missing from dcgen's `TABLE_CLASSES`, so `findClass()` dropped
    every generate.

  `net: PARSE ...` now logs any message a handler reads only partly or past its end.
- **Labels are not tests.** Several autotest steps (`cog building: *`) existed without driver
  code. A step must log what it did and fail visibly when its target is absent.
- **Codex helpers cannot commit.** The Codex sandbox cannot write `.git`. That includes a
  worktree's metadata in the main checkout. Give Codex a standalone clone; the main agent reviews,
  commits and fetches the branch.

**Phase 2 — Playground + movement.** Status: mostly done in Azahar (Old 3DS); see
`docs/test-log.md`.
- ✅ M2.1 `bake_zone.py` (`dna.py` + `assemble.py`) builds the full TTC playground:
  12.7k tris, 157 batches, 1.5 MB ETC1 textures.
- ✅ M2.2 `bake_collision.py` writes the `.ttc` file; `world/collision.*` does
  GravityWalker-style slide, ground and camera ray.
- ✅ M2.3 controller and camera.
- ✅ M2.4 per-part vertex animation (`.tta`, `shaders/morph.v.pica`,
  `world/toon_avatar.*`).
- ✅ M2.6 audio: DSP-ADPCM `.tsnd`, `audio/audio.*`, music streaming plus footsteps.
- Open:
  - M2.5 baked minimap image (the map shows markers only).
  - Per-visgroup chunking and atlasing of zones (TTC is one 157-batch model).
  - Sign text.
  - DNA-driven species and clothing for remote toons (the bake is done; the runtime
    still uses the default dog).

**Phase 3 — Networking.** Status: M3.1–M3.4 work against the real Open Toontown server
from Azahar (`tools/server/`), with a PC bot as the second player:
- login, auto-created toon, zone 2000
- remote toons spawn, move, animate and get nametags
- our pose, anim states and SpeedChat are seen by the other client
- M3.5 is partial: reconnect exists, but there is no bandwidth readout or
  HOME/sleep test yet.
- **Anim state rule:** `setAnimState` takes **state** names from
  `ToontownGlobals.ToonAnimStates` ("run" for all ground locomotion, "jumpAirborne",
  ...), never clip names. An invalid name makes stock Open Toontown's AI crash in
  `banManager` and takes the whole district down. `Client::sendAnimState` whitelists
  names.

**Phase 4 — Playground life + front end.** Status: in Azahar (Old 3DS) against the real
server.
- ✅ **DNA-driven toons.** `world/toon_cache.*` shares parts and clothing by DNA, with
  LRU in the 4 MB budget. Loading runs on a **loader thread**: libctru's `linearAlloc`
  is not thread-safe, so every runtime linear alloc/free holds `tt::mem::LinearGuard`,
  and toons draw the resident default body until their parts are ready.
- ✅ **Front end** (`game/front_end.*`). Title → connect → Pick-a-Toon → 5-step
  Make-a-Toon → playground. `requestSkipTutorial` is sent for new toons. The
  `autotest_fe` trigger drives the whole flow.
- ✅ **Zone objects.** NPC toons stand at the DNA anchors listed in the `.pts`
  sidecar of `bake_zone.py`. Treasures can be grabbed (the AI refuses at full laff:
  `validAvatar` requires hp < maxHp). Our laff and jellybeans drive `ui/hud.*`.
- ✅ **Zone changes.**
  - `Client::changeZone`: quiet zone, then the target zone, with a vis list for
    streets.
  - `world/place.*` resolves zone files by id and finds tunnel/door exits from
    collision triggers.
  - Trigger tests must use GravityWalker's event sphere (centre r − 0.1, radius
    r × 1.04), because door triggers are flush with walls.
- ✅ **Sleep/HOME.** An APT hook flushes the log; on wake the client force-reconnects
  and rejoins the same toon and zone.
- ✅ **Every exterior zone streams from `.tz`** (`world/zone.*`, TZ v1 and v2). All 25
  playgrounds and streets are baked by `bake_street.py` with per-visgroup near/far LOD,
  PVS, floor zone ids, triggers, `.pts` anchors and DNA sign lettering (`sign_text.py`).
  Chunks stream on a loader thread (`Zone` jobs swapped in by `update`).
  Every visible set fits 117 draws / 15,000 tris / 810 KB geometry; the per-zone table
  is in `docs/design/streets.md`. Street cell changes come from floor zone ids
  (`Client::changeStreetCell`).
- ✅ **All 536 building interiors** (`bake_interior.py`, `docs/design/interiors.md`),
  entered with the upstream zone formula `zone - zone%100 + 500 + block`.
- ✅ **Classic characters** (`world/cchar.*`, `docs/design/chars.md`). Mickey, Minnie,
  Goofy, Daisy, Donald, Pluto, Chip and Dale (plus costume variants) are generated by the
  AI as `DistributedCCharBase` subclasses; the class id picks the baked actor. `setWalk`
  legs replay CharWalkState's interval quirks, seeked by the server timestamp.
  - Walk graphs: `bake_char.py` writes JSON, and `char_paths_bin.py` converts it to
    `.tcp` binaries.
  - Not yet done:
    - Dale's `setFollowChip` orbit
    - `setChat` chatter
    - Donald at the wheel rides the moving `DistributedBoat` (`world/boat.*`, `docs/design/boat.md`);
      the local toon rides it too (deck collision, `setParent` 3) and the boat has bell/foghorn/creak/lap
      sounds (built, not yet run in Azahar); remaining: animated piers
    - Character and boat models now load on the shared background loader
      (`world/asset_loader.*`: fixed slots, thread prio 0x31, staging `Model`/`Anim` swapped in by the
      main thread; never hold `LinearGuard` around `Model::load`)
- ✅ **Facility levels** (`world/level.*`, `net/client_level.cpp`, `docs/design/levels.md`).
  - The Sellbot factory runs end to end.
  - Every mint / DA office / country-club floor is baked by the stock upstream layout
    code (`bake_facility_floors.py`, `.tlf`). The rooms merge into one floor-space
    `LevelSpec`, and each room's `.tz` is drawn with its placement.
  - Cashbot mints are tested end to end in Azahar. Stage and club entry are still open.
- ✅ **Street battles** (`world/battle.*`, `ui/battle_panel.*`): cog contact, the whole
  DistributedBattle flow, a gag picker from the inventory, placeholder movie effects, and
  going sad back to the playground.
- ✅ **Trolley and minigames** (`world/trolley.*`, `game/minigame_session.*`,
  `game/catch_logic.*`): boarding, the minigame framework, the Catching game, and the
  PurchaseManager exit. Other games are aborted (the AI pays 1–7 beans).
  `AI_MINIGAME_ID=<id> tools/server/start.sh` forces a game.
- ✅ **Fishing** (practice mode; the stock AI spawns no spots), the **Gag Shop clerk**,
  **boat riding**, **minimaps**, the **Gags tab**, a **frame profiler** (Debug tab),
  and **sounds** (doors, teleport, place music, ambience).
- ✅ **Place transitions** (`world/transition.*`, `docs/design/transitions.md`):
  - door, tunnel and teleport movies, plus death
  - tunnels run from the tunnel model's `tunnel_origin`, and doors from `.pts` door anchors
  - `requestEnter` with the real `DistributedDoor`
- ✅ **Cog bosses** (`world/boss_*`, `docs/design/bosses.md`): VP, CFO, CJ and CEO.
  - In Azahar the VP and CFO board and reach their first Cog rounds, and the VP reached
    `BattleTwo` once.
  - The pie / crane / court / golf phases are not yet seen.
- ✅ **Battle cinematics** (`world/battle_camera.*`, `world/battle_movies.*`):
  MovieCamera and every gag track.
- ✅ **Facility room games** (`world/level_games.*`, `docs/design/level_games.md`): laser fields,
  security cameras, crates, mazes, mole fields and golf greens.
- ✅ **Cogdominiums** (`world/cogdo_*`, `docs/design/cogdo.md`): field offices with the maze
  game (crane / boardroom only behind `AI_DEV_COGDO_GAME`; flying is unsupported, since
  there are no assets).
- ✅ **Name shop** (`docs/design/names.md`): Pick-A-Name, typed names, name states, deleting
  toons.
- ✅ **Interior life** (`docs/design/interior_life.md`): the HQ leaderboard, the pet shop fish,
  door leaves, knock-knock doors (real `KnockKnockDoorSphere`s from `bake_knock_doors.py`) and
  landmark interior signs.
- ✅ **Service clerks** (`docs/design/clerks.md`): pet shop, kart shop, fisherman, party planner.
- ✅ **Swimming** (`world/water.*`): Donald's Dock and Acorn Acres, with the
  DDPlayground/OZPlayground depths, the swim state, splash and sounds. `Renderer::setFog` gives
  linear PICA fog (DD white fog, underwater blue).
- ✅ **HUD**: the real laff meter (`bake_laff_meter.py`: species head in the head colour, eyes,
  teeth, frown, numbers). Jellybeans show on the Toon tab and the book's Gags page, as upstream
  has no jar on the HUD.
- ✅ **Cog attack movies** (`world/cog_attacks.*`, `docs/design/cog_attacks.md`): per-attack
  timing, props, sounds and toon reactions, seen in Azahar.
- ✅ **Kart racing** (`racing2`, `docs/design/racing.md`): stock purchase, Practice boarding and
  barriers are seen in Azahar. Lap counting on the device is being verified.
- ✅ **Multi-floor Cog buildings**: interior elevators (`DistributedElevatorInt`), countdown and
  boarding. Merged, not yet run in Azahar.
- ✅ **Toon Escape and Photo Fun** are real games (`games2`). All 16 trolley games run their
  lifecycle against the stock AI (`tools/dev/minigame_pass.sh`). A visual-fidelity pass per game is
  in progress.
- **Toontorial** (`docs/design/tutorial.md`): the protocol, scripts and baked zones are in. On the
  device the tutorial place does not load yet (under investigation).
- **Golf**: the protocol is fixed. ODE-parity ball physics is in progress on the `golf2` branch
  and is not merged.
- **Cross-play** with Toontown Mobile: the contract is in `docs/design/crossplay.md`; two-client
  tests are pending.
- Next:
  - finish the items above
  - a real hardware run

**Phase 2 — original plan.**
- M2.1: A DNA baker for the TTC playground (`toontown_central_sz.dna` + storage files)
  that assembles props, landmarks and flat buildings, then merges, atlases and grids
  them.
- M2.2: Collision. Bake floor and wall polygons from the `.bam` collision nodes and DNA
  collide nodes. Add a ground raycast, capsule-vs-wall sliding and a jump arc.
- M2.3: Toon controller with upstream speeds (`otp/otpbase/OTPGlobals.py`: forward 16, reverse
  8 units/s, rotate 80 °/s, × ToonSpeedFactor) and a third-person camera that avoids walls.
- M2.4: Skinned animation (neutral, walk, run) with blending.
- M2.5: Minimap image baked from a top-down render, plus the Map tab.
- M2.6: Audio. NDSP and the TTC music, baked to IMA-ADPCM or PCM16 and streamed from SD.

**Phase 3 — Networking.**
- M3.1: Socket thread, framing, HELLO and heartbeat. Connects to a local Open Toontown
  server.
- M3.2: Login with a token, avatar list, create a toon from fixed DNA, play the avatar.
- M3.3: District discovery, then enter zone 2000. Remote toons generate from
  `ENTER_OBJECT_REQUIRED_OTHER` (DNA → baked parts) and leave on `OBJECT_LEAVING`.
- M3.4: Send `setSmPosHpr`/`setSmStop`/`setAnimState`. Apply remote smoothing (buffered
  interpolation about 200 ms behind the timestamp).
- M3.5: Robustness. Handle disconnect/EJECT with a reconnect UI and backoff. Survive
  HOME/sleep. Cap the number of visible remote toons (8, nearest first). Report
  bandwidth.

**Later:** more zones, street visgroup streaming, the tutorial path, SpeedChat,
combat/gags, shops, tasks, pets, the full name shop, and stereo polish.

## 7. Explicitly NOT in early phases (0–3)

- Panda3D, Python, any embedded interpreter, a runtime DNA/BAM/DC parser.
- Battles, cogs, gags, toon-up, the inventory system, quests/ToonTasks, NPC
  interaction, shops, the catalog, estates, pets, fishing, gardening, racing, golf,
  minigames, trolley, parties, cog HQs, boss battles, friends lists, whispers, typed
  chat, the name shop (toons keep the server default name).
- The tutorial. The client skips straight to the playground; the server allows this.
- Street zones (2100 and others), interiors, doors, tunnels between zones. Only the TTC
  playground (2000) is in scope.
- Particles (`.ptf`), dynamic shadows (a blob shadow quad is fine), the full lighting
  model.
- Music beyond one looped track. SFX other than footsteps and jump.
- Stereoscopic 3D beyond the opt-in toggle.
- Online play against public servers. Development targets a local server you run.

## 8. Asset pipeline (offline)

```
ref/resources/*.bam ─┐
ref/resources/*.dna ─┼─► tools/pipeline (Python 3.12 + panda3d 1.10.15)
ref/resources/*.jpg ─┘      ├─ dna.py        DNA text parser + storage resolver   [P2]
                            ├─ assemble.py   DNA → Panda scene graph (flat buildings etc.) [P2]
                            ├─ bake_model.py flatten, merge, quantize, collision extract [P1]
                            ├─ textures.py   resize, atlas, ETC1/ETC1A4/RGBA4/L8, tile  [P1]
                            ├─ anim.py       joint tracks → baked palettes             [P2]
                            └─ audio.py      ogg → IMA-ADPCM / PCM16 (bcwav-like)      [P2]
                                    │
                                    ▼
                      sdmc-out/3ds/toontown3ds/data/{zones,toons,audio}/*.tz|.tt|.ta
                      romfs/baked/ (boot set)
```

- **Python rule.** Run Python with `-I`. Use only stock wheels. Every tool is
  deterministic (same input gives byte-identical output) and prints a budget report.
- **Budgets fail the bake.** The baker fails if a zone exceeds its sub-budget, and
  `tools/pipeline/inspect_bam.py` gives quick stats.
- **Formats are versioned.** Every format starts with a magic and a version. The runtime
  refuses mismatches with a clear on-screen error, never a crash.

### Pipeline tools (Phase 1)

| Tool | Purpose |
|---|---|
| `tools/pipeline/bake_model.py` | static `.bam` → `.ttm` |
| `tools/pipeline/bake_toon.py` | head/torso/legs actors posed at one frame → `.ttm`, with tint slots |
| `tools/pipeline/preview_ttm.py` | decodes a `.ttm`, including untiling, and renders it with Panda offscreen. Use it to check every format change **before** touching the 3DS loader |
| `tools/pipeline/ttm.py` | format spec and writer; the C++ mirror is `source/render/model.cpp` |
| `tools/pipeline/bake_zone.py` | DNA zone → `.ttm` (`dna.py` parser + storage, `assemble.py` placement rules) |
| `tools/pipeline/bake_collision.py` | `.bam`/DNA → `.ttc` collision (spec in `collide.py`) |
| `tools/pipeline/bake_toon.py` | toon parts and anims (`.ttm` + `.tta`, spec in `tta.py`), clothing `.ttx`; SD set in `sdmc-out/` |
| `tools/pipeline/bake_audio.py` | `.ogg` → `.tsnd` (DSP-ADPCM / PCM16; spec in `audio_tsnd.py`) |
| `tools/pipeline/bake_level.py`, `bake_level_props.py` | facility levels (factory, mint / stage / country-club rooms) → `.tz` + `.tlv` (spec in `tlv.py`), and their moving props |
| `tools/pipeline/bake_facility_floors.py` | every mint / DA office / country-club floor, assembled by the stock upstream layout code → `.tlf` (`docs/design/levels.md`) |
| `tools/dcgen/dcgen.py` | DC → `source/net/dc_generated.h` (IDs, hash, field descriptors) |
| `tools/gen_speedchat.py`, `tools/gen_toon_tables.py` | generated SpeedChat and ToonDNA tables |
| `tools/server/` | local Open Toontown server (`setup.sh` on macOS, `setup_linux.sh` on Linux x86_64, `start.sh`, `stop.sh`, `smoke_test.py` bot) |
| `tools/pipeline/ttm_pool.py` | one shared `textures.ttp` per model directory (interiors), TTM v2; `tz.py <files>` repacks `.tz` to v3 |
| `tools/dev/azahar_test.sh` | unattended Old 3DS emulator pass (restores the user's Azahar config) |
| `tools/dev/make_sd_package.sh` | assembles `dist/sd/` (3DSX + SD data set + placeholder `server.txt`) for the SD card; rsync, no network |
| `tools/pipeline/bake_suit_paths.py` | street DNA → `.tsp` suit point/edge graph (spec in the file; cogs walk it, `source/world/suit_path.*`) |
| `tools/pipeline/bake_suit_placeholder.py` | the `romfs/baked/suit_placeholder.ttm` stand-in cog box |
| `tools/pipeline/bake_timer_clock.py`, `bake_fishing_gui.py`, `bake_book_tabs.py`, `bake_purchase_gui.py`, `bake_pick_a_toon.py`, `bake_mat_gui.py` (shared `ui_atlas.py`) | ToontownTimer's alarm clock, fishingGui's cast interface, the Shtiker Book page tab icons, the purchase screens' sign plank and bean jar, AvatarChooser's background, squares and trashcan, and Make-A-Toon's round, gender and shuffle buttons → RomFS `.ttm` atlases plus generated `source/ui/*_atlas.h` geometry |
| `tools/pipeline/bake_loading_screen.py` | ToontownLoadingScreen's art: progress-background's radial `bg` as a 3D quad (`loading_bg.ttm`, drawn behind the front-end Toon) and toon_council's scroll for the TOON TIP (`loading_gui.ttm` + `source/ui/loading_atlas.h`); tips in `source/game/loading_tips.h` |
| `tools/pipeline/bake_mat_room.py` | Make-A-Toon's room (`tt_m_ara_mat_room`): floor / background / spotlight plus each shop's walls and props → pooled `romfs/baked/mat_room/` (TTM v2 + `textures.ttp`), and the roomDrop / roomSquish / propSquish joint tracks → `source/ui/mat_room_tracks.h`; drawn behind the Toon with MakeAToon's camera |
| `tools/pipeline/bake_golf_physics.py` | golf BAM collision nodes → versioned `.gph` meshes (grass, slick, cup, hard) |
| `tools/gen_suit_tables.py` | upstream cog tables → `source/world/suit_tables.h` |
| `tools/shop_test/` | gag shop rules and inventory blob vs the real upstream `InventoryBase` (`docs/design/shop.md`) |
| Host tests | `tools/suit_test/` (suit paths vs the real libtoontown), `tools/nettest/`, `tools/collision_test/`, `tools/toon_dna_test/`, `tools/padnav_test/` (focus model and panel focus tables), `source/ui/test/` |

## 9. Verification rules

- Every meaningful change is tested in **Azahar in Old 3DS mode**. It is installed at
  `/Applications/Azahar.app`; logs go to `~/Library/Application Support/Azahar/log`.
- When possible, test on real hardware too. Azahar's GPU timing is **not**
  representative; only hardware numbers count for the frame budget.
- **Log every test** in `docs/test-log.md` with:
  - the date and build
  - Old or New 3DS, and emulator or hardware
  - fps
  - heap, linear and VRAM usage
  - any issues
- Never claim a feature works without a test-log entry.
- `make` must build with zero warnings in `source/` (third-party excluded).

## 10. Conventions

- **C++17.** No exceptions, no RTTI, no iostreams, and no STL containers that allocate
  in per-frame paths. Use fixed pools and arenas, all in `namespace tt`. 4-space indent.
- Files are named `snake_case`. Types are `PascalCase`, functions `camelCase`,
  constants `kName`.
- **Licensing.** `ref/` holds pinned submodules: never vendor their files into the
  tree. Baked Disney assets may be committed under `romfs/baked/` (the owner's
  decision, 2026-10-23). They stay outside the MIT license; keep
  `THIRD_PARTY_NOTICES.md` current when adding upstream-derived code or data.
- Keep this file current. A decision that is not written here does not exist.

## 11. Later-feature boss work (2026-10-24)

The Sellbot VP has a native `world/boss_battle.*` place controller, reached through
one grouped main-loop hook. It follows AI phases, uses the public Battle API for
both Cog rounds, then runs bounded local pie/gear pools. New boss senders are
clsend-only and guarded by membership/phase; BattleFinal reserve barriers belong
to their battle object. The DC hash is still 547155168. Disconnects during the VP
reconnect to static SellbotLobby, not the abandoned dynamic office.

`tools/pipeline/bake_boss.py` writes the ignored SD `data/boss/` set: four morph
parts, 96 clips, arena/elevator, separate cage/ramps/collision, pie/gear props,
lobby anchor and rescued-NPC DNA. TTA/TTM keep their existing versions; new BPA1,
BSC1 and BND1 metadata are versioned. Stable float64 weighted skinning avoids
Panda's cross-process accumulation noise. The shared loader keeps one active
four-part clip set and at most one pending set, publishing after the GPU fence.
Geometry plus the largest clip pair is 5,409,224 bytes; textures are 364,000 bytes.

This is host/build-verified work, **not Azahar- or hardware-verified**. CEO remains an implementation plan; CFO and CJ are covered in sections 12–13. Full
contract, supported visual simplifications, asset manifest and mandatory Azahar
checks are in `docs/design/bosses.md` and `docs/design/boss_assets.json`.


## 12. Cashbot CFO runtime (2026-10-24)

BossBattle also owns Cashbot CFO encounters, using its existing main-loop hook.
`net/client_cfo.cpp` / `cfo_state.cpp` decode independent CFO crane/safe/goon/
treasure objects into fixed pools (4/9/16/32), with transactional publication and
clsend-only, phase/member/owner-guarded senders. `world/cfo_logic.*` and
`cfo_mechanics.*` implement bounded crane physics, goons, physical throws,
helmet ownership, gear attacks, the ending and explicit Resistance reward
confirmation. The Cog round reuses public Battle movies/camera. Reconnect goes
to static CashbotLobby 12100; successful return goes to CashbotHQ 12000.

`bake_boss.py --cfo` shares deterministic BossCog extraction with VP, adds native
CFO models/clips and reuses the unchanged level/props goon bakes. Both vaults are
resident together because measured geometry fits the budget. Models and active/
pending clips plus the goon reserve are admitted below 6 MiB, textures below
8 MiB. Goons have a 1.5 MiB aggregate clip cap; CFO four-part clip cap is 2,004,480
bytes. Missing/corrupt assets show an error with B return. No protected level,
Toon-cache/avatar, battle-camera/movie, game or book code was changed.

This work is host/build verified only. No Azahar or live CFO victory is claimed.
Exact asset budgets, controls, simplifications, CJ/CEO field contracts and the
mandatory Azahar checks are in docs/design/bosses.md and cfo_assets.json.


## 13. Lawbot CJ runtime (2026-10-24)

BossBattle's existing frame hook dispatches Lawbot encounters to `CjBattle`.
Its lobby anchor is cached once at zone entry. A targeted CJ elevator destination
remains valid when the AI clears its seat in the same TCP turn. The independent
VP/CFO `BossBattle::board` implementation is unchanged. CJ child pools are bounded
8/12/8/10 (cannons/chairs/gavels/lawyers); incoming snapshots publish transactionally.
The real jury round uses guarded, owner-only ballistic shots and waits for the
AI's LOAD before firing again. Defense evidence sends hitBoss(1), never the
unused DC-only pan methods or client healBoss. Scale damage/weight, prosecution
healing, bonus, summon inventory and promotions remain server-owned.

`bake_boss.py --cj` adds 16 models, 96 clips and CJS1/BPA1/BND1 metadata under
ignored SD data/boss. Existing VP/CFO output hashes are unchanged. CJ model
geometry plus the largest active/pending clip pair is 3,411,950 bytes; runtime
reserves 3,592,110 bytes, textures 785,664 bytes. Collision uses 48,440 heap bytes.
No protected level/game/toon/battle-camera/movie or main transition files changed.

This is host/build verified only: no Azahar, live CJ victory or hardware frame
budget is claimed. Exact simplifications, controls, CEO field plan and required
Azahar checks are in docs/design/bosses.md; complete hashes in cj_assets.json.

## 14. Cogdominium runtime (2026-10-25)

`CogdoRuntime` owns a frame while our toon belongs to DistributedCogdoInterior.
Its marked main/client/building hooks preserve existing facility and boss paths.
The pinned stock server chooses one maze and then an executive-suite battle;
all departments use the Mover & Shaker shell. Flying is unsupported (no shipped
level models); crane and boardroom are opt-in forced prototypes only. Barrels
have incoming-state support, but this AI revision does not naturally route to
BarrelRoomIntro. Never invent a successful gate, floor, laff or SOS award.

The worktree-local Cogdo baker uses stock fast-simplification==0.2.0 (installed
outside the linked .venv), architectural-face preservation, ETC1 with three
workers, and independent-process hash verification. The self-contained SD kit
is committed explicitly under sdmc-out/3ds/toontown3ds/data/cogdo; that exception
includes only the Cogdo kit, not other generated SD data. The runtime validates
CGM1/CGW1/CGP1/BND1 metadata and streams one model group at a time. Maze geometry
is 350,748 bytes, textures 739,136 bytes. Six nearest render Cogs share the
existing actor pool; all 64 possible simulations and 256 memo slots are bounded.

`tools/cogdo_test/run.sh` checks Panda field/truncation vectors, the real Client,
seeded cells/spawns, exact walking and boss-drop RNG, cooler indexing, navigation,
crane bounds, collision and budgets. Make and host tests are verified only;
Azahar, live victory, multiplayer timing and hardware budgets remain mandatory
main-agent checks in docs/design/cogdo.md. No protected level/boss/toon/battle
camera/movie/transition files were edited.

### Service clerks (clerks branch, 2026-10-25)

- `docs/design/clerks.md` specifies pet/kart/fisherman/party sessions. Native panels
  use bounded ordered movies, AI deadlines and authoritative owner resends. SELL
  NOFISH=3/TROPHY=4 must not be confused with PARTY modes. No local grants.
- This pinned PetManagerAI sends empty offers and lacks create/delete methods;
  party canBuyParties is false; fishManager is absent from the AI repository.
  Native exposes these limitations without inventing services. Host fixtures for
  positive sales/adoption are explicitly separate from stock-server behavior.
- Kart previews compose seven or fewer original LOD1 components through the shared
  loader after the GPU fence, bounded to 512 KiB geometry / 1 MiB textures. The
  generated SD preview kit at `sdmc-out/3ds/toontown3ds/data/clerks/karts/` may be
  committed as the owner's requested completed asset delivery, like the Cogdo kit;
  its Disney-derived contents remain outside MIT. Install it at the same SD path.
- Pet seed generation is CPython Random/PetUtil compatible, validated independently
  across all six hoods. Existing estate Doodle geometry gains authoritative palette
  tint; variant Doodle geometry is outside this change.
- Host verification and offscreen decoded-asset checks are recorded in test-log.
  No emulator/hardware claim is made: this task explicitly prohibited Azahar.

### Performance profiling and bounded streaming (perf, 2026-10-25)

Every frame owner separates update/draw/load publication/UI/GPU wait/audio; tails
are `other`. Screenshot I/O is `shot`; autotest `shots off` disables dumps without
changing inputs/timings. Frames >=100 ms log a hitch immediately. Compare corrected
profiler baselines: former early-owner busy included wait and capture I/O could
inflate dplace. Stats count actual uniform/texture/VBO API calls.

Renderer state reuse preserves ordered draws and invalidates at shader/morph and
world/2D boundaries. Fixed exact text keys retain original C2D glyph buffers.
Facility draw data depends on immutable spec revision; dynamic gameplay stays live.
Model/anim batches use the shared loader's existing 24 slots, <=4 active per batch,
and publish/replace/free GPU data only at the completed-frame fence. Facility entry
holds the entrance with a loading indicator until initial chunks/props complete;
later PVS streaming does not freeze movement. Do not add an IO-thread allocation
or LinearGuard around file reads. Native timings/resident peaks remain unmeasured;
see docs/design/performance.md for host evidence, fixed costs and required Azahar
A/B checks. This task prohibited emulator execution.

### Toontorial protocol and assets (tutorial, 2026-10-26)

New avatars choose **Play the Toontorial** or **Skip** before creation; Skip remains the
default. TutorialManager's runtime-allocated zones are never baked by id: `world/tutorial_zones.h`
maps them to canonical 20000/20001/20501/20502 assets at the client boundary and translates
destinations back before Astron location updates. `tools/gen_quest_scripts.py` emits the five
stock tutorial QuestScripts and rejects unrecognised source data. `tools/tutorial_test/run.sh`
checks the generated scripts, tutorial sender signatures/clsend keywords, and zone mapping.
See `docs/design/tutorial.md` for bake commands, command presentation and the required
emulator verification; host/build checks do not establish a live tutorial play-through.

### Goofy Speedway racing runtime (racing2, 2026-10-25)

- `docs/design/racing.md` records the stock purchase, boarding, barrier, physics,
  lap, KartDNA, gag, results and return contracts. All ownership, tickets and
  results remain authoritative. Solo Practice is allowed by the pinned AI.
- `tools/racing_test/run.sh` checks the actual upstream control/pie methods with
  Panda physics, native lap/grid behavior, all outgoing DC packings and original
  track collision. No emulator/hardware result is implied by those host tests.
- TRC1 stores the original 4,000-point forward/reverse curve, unsorted original
  start locators, gag positions and exact floor tags. Never substitute pad collision
  for track collision or invent a midpoint checkpoint that upstream does not enforce.
- Four bounded KartDNA instances reuse the clerk component loader, with all three
  original LODs and generated seat/wheel anchors. Models publish at the shared
  frame fence. Hard asset/ToonCache budgets remain in effect.
- The requested completed racing asset delivery is committed explicitly under
  `sdmc-out/3ds/toontown3ds/data/racing/`, following the Cogdo/clerk SD kit precedent.
  Those 634 Disney-derived binaries are excluded from MIT; see
  `THIRD_PARTY_NOTICES.md` and the per-file racing_assets.json SHA-256 manifest.
- Full-frame Old 3DS 30 FPS, stereo, memory high-water marks and live stock-server
  payout are pending the main agent's Azahar/hardware pass. Rigid wheels, the
  unavailable-in-the-51-clip-catalog feedPet flourish and presentation/audio gaps
  are listed explicitly in racing.md; do not claim complete visual parity.
