# Test log

One entry per meaningful change. Always state emulator vs. hardware, Old vs. New 3DS mode.

**Important:** Azahar's CPU timings are *emulated* and moved by about 40% between Azahar
2123.3 and 2126.2 for the same build. Its GPU time is a constant 0.50 ms. Use
emulator numbers only to compare builds on the same emulator version. Real budgets
need hardware.

## How to run the automated pass

1. Create an empty file `sdmc:/3ds/toontown3ds/autotest`. In Azahar that is
   `~/Library/Application Support/Azahar/sdmc/3ds/toontown3ds/autotest`.
2. Boot the `.3dsx` or the installed `.cia`. The script (`source/core/autotest.cpp`)
   drives input for about 40 s.
3. It writes one `stats:` line per second to `log.txt` and dumps render targets as
   `shot_*.bin`.
4. Decode the dumps with:
   ```bash
   .venv/bin/python -I tools/dev/decode_shot.py <path>/shot_*.bin --out-dir /tmp/shots
   ```

## Entries

### 2026-10-23 — Phase 0 skeleton
- **Target:** none.
- **Result:** not built. devkitPro was not installed on the dev machine.

### 2026-10-23 — Phase 1, `.3dsx`
- **Target:** Azahar 2123.3, Old 3DS mode, 1× resolution, Vulkan.
- **Result:** PASS. The full autotest passes, with no panics in the Azahar log.
- **FPS (top):** locked at 29.9–30.0 for the whole run (`C3D_FrameRate(30)`).
- **Memory:**

  | Pool | Measured |
  |---|---|
  | Heap | 133 KB used of 24 MB |
  | Linear | 19.9 MB free of 24 MB (about 4 MB used: models 2.0 MB + cmdbuf 512 KB + citro2d/system) |
  | VRAM | 4.1 MB free |

- **Scene:** TTC terrain, gazebo, 3 mailboxes and the local toon. Base cost is 42
  draws and 1,912 tris.
- **CPU per frame (emulated):**

  | Stress toons | CPU | Draws | Tris |
  |---|---|---|---|
  | 0 | 3.5 ms | | |
  | 8 | 6.0 ms | | |
  | 16 | 8.5 ms | | |
  | 32 | 13.4 ms | 490 | 21.5k |
  | 48 | 18.3 ms | 714 | 31.2k |
  | 64 | 23.3 ms | 938 | 41k |

  That is about 0.30 ms CPU per extra 611-tri, 14-batch toon.
- **Issues:**
  - Fixed: the default 256 KB command buffer overflowed (svcBreak PANIC) at about
    700 draws/frame. The vertex-buffer config is now bound once per model, the
    command buffer is 512 KB, and there is a 90% guard. 64 toons now use 52% of it.
  - Fixed: Debug-tab text re-parsed every frame cost about 2 ms CPU. It is now
    cached and refreshed every 10 frames.
  - Fixed: Map markers drawn with `C2D_DrawCircleSolid` did not appear after the
    3D pass (citro2d proctex path). They are now squares. Also added
    `Renderer::endWorld()` to reset alpha test and depth map before citro2d.
  - Fixed: the Map arrow was too equilateral to read its direction.
  - Expected (Phase 1): no collision. The toon walks through the raised plaza
    (z≈3–4) on a flat z=0 plane.
- **Checks:**
  - Movement speeds match upstream: 20 units/s forward, 10 back, ×0.4 walking with B.
  - Tank turning, L/R orbit, touch tabs, D-pad tabs and the toon colour picker
    (tint uniforms) all verified via logs and render-target dumps.

### 2026-10-23 — Phase 1, `.cia`
- **Target:** Azahar 2123.3, Old 3DS mode.
- **Result:** PASS. It installs with `azahar -i`, boots from NAND, the RomFS loads,
  SD logging works, and the full autotest passes.
- **Fixed in the RSF:** an invalid `AffinityMaskExt` key, a duplicate
  `FileSystemAccess`, `RomFs: RootPath`, and a missing `MemoryType: Application`.
  Without that last one Azahar hit "Unreachable code" in kernel memory setup.

### 2026-10-23 — Phase 1, `.3dsx`
- **Target:** Azahar 2126.2, Old 3DS mode.
- **Result:** PASS. The regression pass after the emulator update ran clean.
- **CPU:** 2.1 ms idle and 8.1 ms at 32 toons (vs 3.5 / 13.4 ms on 2123.3, with the
  same build).

### 2026-10-23 — Phase 2 integration (DNA playground, animation, collision, audio, SpeedChat, ETC1)
- **Target:** Azahar 2126.2, Old 3DS mode, `.3dsx`, run with `tools/dev/azahar_test.sh`.
- **Result:** PASS. The full autotest passes, with no panics.
- **Scene:** the whole TTC playground assembled from DNA (`ttc_sz.ttm`) plus the animated
  local toon:

  | Measure | Value |
  |---|---|
  | Tris / draws | 13.3k tris, 171 draws |
  | Command buffer | 13% |
  | FPS | 29.9 |
  | CPU (emulated) | 3.8 ms |
  | Heap | 452 KB |
  | Linear | 19.0 MB free of 24 MB |

- **Zone file:** `ttc_sz.ttm` is 2.0 MB in linear memory with ETC1/ETC1A4 textures
  (was 3.7 MB at 16 bpp).
- **Animation:** neutral/walk/run/jump-idle/wave morph between per-part 12 fps frames.
  Parts compose correctly in render-target dumps.
- **Collision:** `ttc_sz.ttc` (1226 tris, 76 capsules, 17 triggers, 115 KB heap).
  - Ground follows the terrain: 3.3 on the prop base at spawn, 4.0 on the plaza,
    0 on the grass.
  - Walls stop and slide the toon.
  - The camera pulls in front of walls.
- **64 animated stress toons:** 1067 draws, 52k tris, 69% command buffer, 30 fps,
  18 ms CPU (emulated).
- **Audio:** NDSP OK and the TTC music streams (Azahar needs a placeholder
  `sdmc:/3ds/dspfirm.cdc`; real hardware needs a real dump).
- **Issues:** one sample at 26.9 fps during the reverse step. Possibly a music-stream
  read hitch; watch for it.
- **SpeedChat tab and nametag:** draw correctly. The "Toon" nametag sits over the head.

### 2026-10-23 — Phase 4 (DNA toons, front end, NPCs/treasures, HUD, zone change)
- **Target:** Azahar 2126.2, Old 3DS mode, local Open Toontown server.
- **DNA toons:** 4 bots of different species/gender/size render with their own heads,
  clothing and lashes. Parts stream in on the loader thread at 29.9–30 fps (was 7.8 fps
  with synchronous loads). LOD 250 is used beyond ~45 units.
- **Front end (`autotest_fe`):** a new account → Make-a-Toon (cat girl, colour,
  shirt) → "Brown Cat" created, tutorial skipped, in zone 2000. Every screen was checked
  in the dumps.
- **NPCs:** Fisherman Freddy and two Party Planners at their DNA anchors, with nametags.
- **Treasures:** 5 parsed. The grab request is sent; the AI refuses it at full laff, as
  upstream does.
- **HUD:** laff meter + jellybean jar from the server stats.
- **Zone change:** Gag Shop door → zone 2519 through the quiet zone on the real
  server, and the shop clerks generate. Tested with stand-in interior files; the real
  interiors are pending. Tunnel 2132 resolves, and "not installed" shows a toast.
- **Performance:** 166–171 draws, 13.1–13.3k tris, 30 fps, heap 1.9 MB (incl. a 1 MB
  socket buffer), linear 18 MB free.

### 2026-10-23 — Streets, interiors, all playgrounds, teleport
- **Target:** Azahar 2126.2, Old 3DS mode, local server.
- **TTC playground from `2000.tz`:** 87–89 draws (was 171 as one model).
- **Silly Street (2100):** in through the tunnel and confirmed by the server as zone
  2132. 41 draws / 5.6k tris at the entrance. The toon walks out of the tunnel mouth
  facing the street.
- **Gag Shop interior (2519, Antigravity bake):** through the door, then back out to the
  playground. Clerk Clara and Clerk Clark stand at their anchors.
- **Teleport (Map tab):** TTC → Donald's Dock → Daisy Gardens → The Brrrgh. Each hood
  loads in about 0.5 s and plays its own music, and the server confirms each zone. The
  Brrrgh runs at 139 draws / 10.3k tris.
- **Caveat:** the host Mac was busy during these runs, so the fps samples are noisy.

### 2026-10-23 — All interiors (Antigravity: 536 buildings)
- **Target:** Azahar 2126.2, Old 3DS mode, local server.
- **Interior zone formula fixed** to upstream `zone - zone % 100 + 500 + block`
  (`DistributedBuildingAI.getExteriorAndInteriorZoneId`). The exit target is
  `zone - zone % 100 - 500`.
- **Silly Street building 70:** street cell 2154 → zone **2670**. The shopkeeper Dan
  Dribbles generates at his anchor. The way out returns to street 2100, and cell
  tracking then sets the interest to cell 2114 (20 zones).
- **SD data set:** about 147 MB of interiors (536 zones; the 265 legacy aliases are
  not installed).

### 2026-10-23 — All exterior zones from `.tz` v2 (Codex streets2)
- **Target:** Azahar 2126.2, Old 3DS mode, local server. Codex's bake of all 25
  exterior zones with budgeted near/far LOD and DNA sign text.
- **TTC playground (`2000.tz` v2):** 88–90 draws / 10.4k tris. Sign lettering renders
  on the landmark buildings.
- **Gag Shop 2519:** in and back out.
- **Silly Street (cell 2132):** 44 draws / 5.2k tris. Building 70 → interior 2670, and
  back to street 2100.
- **Teleport:** Donald's Dock (`1000.tz`) at 79 draws / 13.7k tris, then Daisy Gardens
  and The Brrrgh load. No PANIC, and no command-buffer guard hits.
- **Known issue (fixed below):** zone loads ran on the main thread, about 0.4–1.5 s
  (2100.tz took 1.46 s). During a load the fps dropped to 6–18 for about a second.

### 2026-10-23 — Asynchronous chunk streaming
- **What changed.** `Zone` reads chunks on a loader thread (core 0, priority 0x31) into
  staging models, and the main thread swaps them in. A chunk changing LOD keeps drawing
  its old LOD until the new one arrives. Per-index validation moved from an SD scan at
  load time into `Model::loadFrom`, in memory.
- **Header load (parse + validation):**
  - 2000.tz: 535 → 90 ms
  - 2100.tz: 1.46 s → 0.25 s
  - 1000.tz, 5000.tz, 3000.tz: 127–145 ms
- **Arrival.** `Place::finishLoading` blocks until the first visible set is resident,
  which takes 0.36–0.80 s. Arrivals have no pop-in, and the whole transition is 0.4–0.9 s.
- **Walking.** Running across TTC holds 29–30 fps; before, it dipped to 20–23 fps while
  chunks streamed. Zone changes still drop the fps for about one second.

### 2026-10-23 — Classic characters (Antigravity chars bake)
- **Target:** Azahar 2126.2, Old 3DS mode, local server.
- **TTC:** the AI generates DistributedMickey (class 67) with `setWalk a->b`. Mickey
  loads in 0.18 s (`mickey_800` plus neutral and walk clips) and walks his graph. The
  "meet mickey" autotest shot shows him animated, with his nametag and drop shadow.
  The playground holds 29.9 fps at 101 draws / 13.3k tris.
- **Other playgrounds:** Daisy (DG, class 72, 17-node graph) and Pluto (BR, class 81)
  spawn and walk. Donald at the wheel (DD, class 80) is received but hidden, because
  the boat it rides is not implemented.
- No PANIC.

### 2026-10-24 — Zone transition fades
- **What changed.** Every zone change (door, tunnel, teleport) now fades the top screen
  to black over 0.4 s. For a door or tunnel, the toon keeps walking forward during the
  fade. The zone loads behind a fully black frame, then the screen fades back in.
- **Azahar, Old 3DS mode:** all 9 zone changes of the autotest succeed:
  - TTC ↔ Gag Shop
  - TTC → Silly Street → building 70 and back
  - teleports to DD, DG and BR
- No PANIC.

### 2026-10-24 — Street cogs: path search and net parse (host only, no Azahar)
- **Target:** PC (clang++, macOS arm64), not a 3DS build: `tools/suit_test/run.sh` and the
  `tools/nettest` single-client scenario. `make -j4` also builds with zero warnings in `source/`.
- **suit_path vs libtoontown:** 17 streets, 6,800 random queries (5,329 with a path). All
  paths, all 452,391 legs and 1.3 M walk positions match `panda3d.toontown`
  (`DNAStorage.getSuitPath`, `SuitLegList`); 0 failures. 0.65 s total on the PC.
- **nettest:** descriptor vectors 621 checked, 0 failed (includes DistributedSuit/Planner).
  A fake-server planner plus Flunky generate and update in the client; the events carry
  the right fields. `tools/nettest/build.sh` did not link (`toon_dna.cpp` missing); fixed.
- **Not tested:** cogs on screen (Azahar), frame cost, SD load, hardware. The runtime
  (`world/suits.*`) is untested beyond compiling.

### 2026-10-24 — Boat, minimaps, Gags tab, stairs, street cogs, chatter merge
- **Target:** Azahar 2126.2, Old 3DS mode, local server.
- **Boat (DD).** The AI's DistributedBoat state cycles, and the ship sails `dd-e-w` /
  `dd-w-e` with Donald at the wheel aboard. The static boat was removed from `1000.tz`.
  The "see boat" shot shows it. Riding the deck is not yet implemented.
- **Minimaps.** All 25 `.tmap` files load (256×256 ETC1). The TTC, DD, DG and BR Map
  tab shots show the baked image with the player arrow.
- **Gags tab.** Shows the server inventory for a new toon (Cupcake 1, Squirting
  Flower 1, 2/20 gags) and experience toward the next level.
- **Stairs.** Running down the steps from the TTC spawn (z 3.3 → 0) no longer switches
  to the jump/fall clip; only a real jump does.
- **Street cogs (Silly Street).** The AI generates cogs, the suit planner names street
  2100, and paths are rebuilt (e.g. 204 legs). Placeholder boxes walk about 4.8 u/s
  along the street with "Short Change Level 1" nametags. 29.9 fps.
- **Chatter merge.** The host test passes. In-game chatter has not been seen yet: the
  AI's chatter timer did not fire during the run.
- No PANIC.

### 2026-10-24 — Fishing, sounds, profiler, shop, boat riding (wave 2 merges)
- **Target:** Azahar 2126.2, Old 3DS mode, local server.
- **Fishing (TTC pond).** The toon walks onto a fishing spot (`.pts` anchor), sits, and
  the rod appears with the fishing panel. A cast at 78 % power lands the bob 21.8 units
  out, and a bite catches a Five Star Fish (57 oz, practice mode). B leaves the spot.
  Stock Open Toontown generates the pond but no DistributedFishingSpot, so the catch is
  rolled locally with FishGlobals' rules and nothing is credited. 29.9 fps.
- **Gag Shop clerk.** Touching Clerk Clara sends avatarEnter. The AI answers setMovie
  mode 3 (NO_MONEY) for a toon with 0 jellybeans, which is correct, so the purchase
  panel was not exercised.
- **Profiler.** At the fishing spot the main thread is busy for 3.2 ms of the 33.4 ms
  frame, of which zone drawing is 1.2 ms.
- **Sounds.** Music and ambience load per place. Door, teleport and chat sounds are
  wired in but have not been listened to (the emulator runs muted).
- **Boat.** The boat model and deck load asynchronously ("boat: model ready"). The
  autotest's deck drop missed (it landed in the water), so boarding is still untested.
- No PANIC.

### 2026-10-24 — First street battle against the stock AI
- **Target:** Azahar 2126.2, Old 3DS mode, local server, Silly Street.
- **Battle flow, all against the real AI:**
  - The toon bumps a walking Cold Caller (level 3), and requestBattle → DistributedBattle
    generates at the battle cell (−190, −220).
  - FaceOff, then faceOffDone, then WaitForInput.
  - Our requestAttack: Cupcake (throw L0), then Squirting Flower (squirt L0). Each
    PlayMovie (5.2 s) is followed by movieDone.
  - Out of gags, the toon passes (track 98) for 4 rounds while the cog attacks.
  - The toon reaches 0 laff: setState Resume, the battle object goes away, and the toon
    goes sad and teleports back to the TTC playground. This is the correct outcome for a
    15-laff toon against a level-3 cog.
- **Display.** The bottom panel shows the cog (name, level, HP bar), the gag grid with
  counts, and Pass/Run. Damage numbers pop over the fighters (−6 on the toon).
- **Performance.** 29.9 fps throughout; the main thread is busy for 3.0–3.1 ms of the
  33.4 ms frame.
- **Not yet:**
  - real cog models and animations: placeholder boxes until Codex's bake lands
  - a reward screen: we lost this fight
  - multi-toon battles and joining someone else's fight
- No PANIC.

### 2026-10-24 — Trolley and the Catching game, end to end
- **Target:** Azahar 2126.2, Old 3DS mode, local server started with
  `AI_MINIGAME_ID=8 tools/server/start.sh` (forces Catch).
- **Flow, all against the stock AI:**
  - The toon walks into the TTC trolley sphere, sends requestBoard, and gets fillSlot
    (seat 0). The countdown shows as a toast.
  - setMinigameZone(61000, 8), then setParent(hidden) and changeZone(61000).
  - DistributedCatchGame (class 131) generates. We send setAvatarJoined, show the rules
    for 4 s, then send setAvatarReady.
  - setGameStart. The schedule is 24 drops, 18 of them apples, identical to upstream
    (host-tested in `tools/catch_test`).
  - With the autotest steering, the toon catches 18 of 18 (claimCatch each) and then
    sends reportDone.
  - setGameExit, then setAvatarExited.
  - PurchaseManager (class 176) pays 13 jellybeans. We send requestExit, the AI answers
    setPurchaseExit, and we send setInventory(done=1).
  - changeZone(2000) teleports us back to the playground, and the jar shows 13.
- **Rendering.** The treehouse stage draws at 1,870 tris with the fruit models;
  29.9 fps.
- **Not yet:**
  - other minigames: aborted at once, which pays 1–7 beans
  - the trolley car does not drive (static in the zone bake)
  - sit/eat animations
  - cogs walking on the catch stage
  - the gag purchase screen
- No PANIC.

### 2026-10-24 — Cog HQ courtyards (Antigravity coghq bake)
- **Target:** Azahar 2126.2, Old 3DS mode, local server.
- **Autotest teleports into all four courtyards**, each confirmed by the server:
  - Sellbot HQ 11000
  - Cashbot HQ 12000
  - Lawbot HQ 13000
  - Bossbot HQ 10000

  Each loads in 0.18–0.37 s (`.tz` v2, 25–147 chunks) and holds 29.9 fps at 41–44
  draws / 3.7–4.1k tris.
- **Known issues:**
  - Cashbot HQ shows horizontal line artefacts near the drop point (−48 z). It looks
    like a thin far-LOD sliver; needs a look at the bake.
  - The Sellbot drop point faces a wall.
  - The street → HQ tunnels (5300, 9200, 3300) and the HQ doors (`cogdoor_*`) are wired
    in `place.cpp` but not driven by the autotest yet.
- No PANIC.

### 2026-10-24 — ToonTasks from an HQ Officer
- **Target:** Azahar 2126.2, Old 3DS mode, local server.
- **Flow:**
  1. Teleport into TTC Toon HQ (2520), where 4 HQ Officers generate at their desk
     anchors.
  2. Walking into one sends avatarEnter.
  3. The AI answers setMovie ASSIGN (mode 4: quest, reward, toNpc), and the dialog
     pages show on the bottom screen and in the officer's balloon.
  4. Done sends setMovieDone, and the AI clears.
  5. setQuests arrives, and the Toon tab lists the task: "WANTED — a Flunky".
- **Progress text.** The progress counter text is not shown yet for this quest type.
- **Autotest.** It can now start mid-script: `AUTOTEST_FROM="toon hq" tools/dev/azahar_test.sh ...`.
- No PANIC.

### 2026-10-24 — Gag purchase, a won battle, riding the boat
- **Target:** Azahar 2126.2, Old 3DS mode, local server. Each case was started part-way
  with `AUTOTEST_FROM`.
- **Gag Shop.**
  1. Clerk Clara answers avatarEnter with setMovie START.
  2. The panel opens with 26 beans and 0/20 gags.
  3. We buy 12 (6 Cupcakes, 6 Squirting Flowers), and Done sends setInventory(done=1).
  4. The AI completes it (mode 2): 14 beans left, and the inventory is resent.
- **Battle won.**
  1. On Silly Street, the toon bumps a level-1 Short Change.
  2. Two Cupcake throws defeat it, followed by Reward, rewardDone and Resume.
  3. The cog HP bar now drops with each hit, because the AI only resends setHP after
     the battle.
- **Boat.**
  - The toon dropped mid-deck boards: `onPlatform` is set, and the toon is carried along
    the dd-w-e path while its heading turns (60 → 30° over 3 s).
  - The earlier failure was the autotest drop point: the deck floor spans boat-local
    y 2..18, not the origin.
- No PANIC.

### 2026-10-24 — Whispers with the PC bot
- **Setup.** `tools/server/smoke_test.py --whisper 100` whispers phrase 100 to every toon in
  view every 5 s.
- **Received.** The 3DS client receives every whisper (`net: whisper 100 from 100000039`)
  and turns it into a top-screen toast.
- **Sending.** The Chat tab header's "To:" button cycles through the toons in view; a
  SpeedChat pick then goes as setWhisperSCFrom to that toon only, with no public balloon.
  Sending was not exercised by the autotest.

### 2026-10-24 — All implemented minigames, real Cogs, tailor, toon battle clips
- **Target:** Azahar 2126.2, Old 3DS mode (muted from now on), local server with
  `AI_MINIGAME_ID=<id>`. Every game was reached by riding the TTC trolley, and none
  crashed. Beans are the PurchaseManager payout.

  | Game (id) | Result |
  |---|---|
  | Cannon (2) | Autoplay fired at (rot −5.6°, ang 68.8°) and hit the tower: **22 beans**. |
  | Tug of War (7) | The cog fell in the water: **16 beans**. Fixed: `reportPlayerReady` was sent before setGameStart, so the AI never sent Go and paid 1. |
  | Ring (5) | 16/16 rings: **21 beans**. |
  | Maze (6) | 3 beans. Autoplay does not steer, so this matches the treasures picked up. |
  | Match Minnie (11) | **8 beans**. Fixed: an invalid `bonusGlowCard` sent the stock AI into a crashing `logSuspicious` path, and the client never sent reportDone on the timer. |
  | Diving (9) | **15 beans** (one chest recovered). |
  | Jungle Vines (12) | 1 bean. Fixed: `setNewVine` sent our money as the avId. Autoplay still never catches a vine, so the result is unchanged. |
- **Real Cogs.** Bodies, heads, department suits and hand tints draw on Silly Street
  (Pencil Pusher walking) and in the 'cog showcase' step.
- **Toon battle clips.** throw, pushbutton, cringe and victory play on the local toon.
- **Tailor (2521).** Tammy the Tailor opens the Clothing Store panel; Done sends
  finished=2.
- **Stock AI bug.** If a client disconnects with a tailor session open, the AI dies
  (`DistributedNPCTailorAI.__handleUnexpectedExit` TypeError). The autotest now always
  cancels before it ends.
- **Full regression.** All 55 steps passed with Cog buildings, the factory and the
  mint/office merged.

### 2026-10-24 — Elevators into Cog HQ facilities; AI dev test mode
- **Dev test mode.** `AI_DEV=1 tools/server/start.sh` uses `tools/server/ai_dev.py`, which
  patches the stock AI in memory and leaves `ref/` untouched:
  - relaxes ToontownAccessAI and the elevator laff and Cog suit checks
  - tops player toons up to 60 laff and 40 beans
- **Factory boarding.** In Sellbot factory exterior 11200, the toon steps onto elevator 0
  (`elevator_origin_0`, from the new `tools/pipeline/coghq_elevators.py` anchors). Then:
  1. requestBoard is accepted.
  2. setFactoryInteriorZone(61000) arrives, and the client sends setParent(hidden) and
     changeZone(61000).
  3. The AI creates the factory (DistributedFactoryAI, factory 11500), and the client
    loads `factory.ttm` and the entity models.
- **Status (honest):** the factory, Cog interior and mint/office runtimes are thin stubs.
  - The toon is not placed at the entrance, and there is no factory collision.
  - Entities are drawn static.
  - Inside, the camera still shows the HQ.
  - Cog building elevators now hand off when their doors close with us aboard (the
    interior zone from the building block), but that path has not been exercised.

### Not yet tested
- Real hardware (Old or New 3DS).
- Stereo 3D toggle. Code path exists; untested in emulator.
- New 3DS mode and the C-stick.
- Sleep/HOME-menu resume.
