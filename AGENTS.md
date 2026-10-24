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
  Every visible set fits 117 draws / 15,000 tris / 810 KB geometry; the per-zone table
  is in `docs/design/streets.md`. Street cell changes come from floor zone ids
  (`Client::changeStreetCell`).
- ✅ **All 536 building interiors** (`bake_interior.py`, `docs/design/interiors.md`),
  entered with the upstream zone formula `zone - zone%100 + 500 + block`.
- Classic characters (Mickey, Minnie, …) are in progress on the `chars` worktree.
- Next:
  - async zone loading (loads take 0.4–1.5 s on the main thread today)
  - door/tunnel movies (`setTunnelOut`/`setTunnelIn`, `DistributedDoor.requestEnter`)
  - SpeedChat emotes
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
| `tools/dcgen/dcgen.py` | DC → `source/net/dc_generated.h` (IDs, hash, field descriptors) |
| `tools/gen_speedchat.py`, `tools/gen_toon_tables.py` | generated SpeedChat and ToonDNA tables |
| `tools/server/` | local Open Toontown server (`setup.sh`, `start.sh`, `stop.sh`, `smoke_test.py` bot) |
| `tools/dev/azahar_test.sh` | unattended Old 3DS emulator pass (restores the user's Azahar config) |
| Host tests | `tools/nettest/`, `tools/collision_test/`, `tools/toon_dna_test/`, `source/ui/test/` |

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
