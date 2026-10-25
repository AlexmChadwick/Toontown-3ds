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

### 2026-10-24 — Estates: client merged, server side missing
- **Server side missing.** Copilot's estate client is merged, but stock Open Toontown
  never generates an EstateManager. `toontown/estate/EstateManagerAI.py`,
  `DistributedEstateAI.py` and `DistributedHouseAI.py` are empty 5-line classes.
- **Home button.** Map → Home now says "Estates aren't available on this server". The
  estate, house, closet and pet code is unexercised until a server implements estates.
- **Assets.** The estate models were baked without textures; this needs a rebake.

### Not yet tested
- Real hardware (Old or New 3DS).
- Stereo 3D toggle. Code path exists; untested in emulator.
- New 3DS mode and the C-stick.
- Sleep/HOME-menu resume.


### 2026-10-24 — animrt: streamed Toon clips and runtime feature hooks (host/build only)

- **Target/build:** branch `animrt`, devkitARM 3DSX build with
  `DEVKITPRO=$HOME/devkitpro DEVKITARM=$HOME/devkitpro/devkitARM make -j4`.
  Zero warnings in source; toolchain crtn.o emits its GNU-stack linker warning.
  No Azahar or hardware run, per task instructions. FPS, device heap/linear/VRAM
  and frame timings are **not measured**.
- **ASan/UBSan:** new `tools/anim_test/run.py` passes playback reverse/subrange/
  hold/restart/seek, source-loop periods, timestamp wrap and all 25 emote mappings.
  Real Anim/ToonCache with host GPU/model stubs passes synchronized attachment/
  vertex samples, shared frame pairs, generation cancellation, sparse fallback,
  malformed/truncated TTA rejection, GPU-safe reuse, no streaming linear allocations
  and cap admission. Synthetic peak **4,000,128 bytes**, hard cap **4,194,304**;
  fixed active/staging pool **589,824**, stream attachments **6,912** heap bytes,
  neutral attachments up to **4,608**, loader stack **32,768** regular-heap bytes.
  These are cache bounds and host allocation observations, not device measurements.
- **Network:** 391 descriptor vectors; single client, two clients and reconnect all
  pass, including emote access/send/receive, invalid IDs, delayed timestamp bounds,
  duplicate input and clear sentinel. DC hash stays **547155168**.
- **Existing conformance:** Catch (398 drops/110 suit walks), Cannon (150 shots),
  Tug (288 forces), Ring (18 patterns), Maze (100 collisions), Diving (150 frames),
  Pairing (6 seeds), Vine (20 swings/40 jumps), fishing, Toon DNA (20,000 random
  cases and 19,584 creation builds), shop (20,000), tailor, classic characters,
  projection (405 points), zone format units (5), and terrain + assembled TTC
  collision all pass. Collision reference work was redirected through this
  worktree's `ref/ref/resources`; Python used `-I -B` for shared imports.
- **Generation:** clip/timing and SpeedChat tables regenerate byte-identically;
  dcgen output also matches byte-for-byte. Source audit checks 653 BAM headers,
  including 10 sparse Bow/Curtsy combinations.
- **Unavailable suites:** full suit-path tests need the missing server Panda fork
  (`panda3d.toontown`); full zone-integrity tests need the external baked TZ set.
  Zone format units and the pure Catch suit-walk schedule comparison did run.
- **Runtime handoff:** see `docs/design/toon_anims.md`, final implemented-runtime
  section and six-step Azahar checklist. Install the full anims SD Toon set;
  verify 30 fps/<=6 ms animation, <=4 MiB Toon cache and <=24 MiB total linear,
  local + eight remotes, all emotes and sparse parts, fishing/trolley/minigames,
  teleport and battle clips. Precise new hand props, facial assets and teleport
  hole/clipping choreography remain separate asset work and are not claimed tested.

## 2026-10-24 — merges (animrt, racing, golf, battlefx, shtiker) and the Sellbot factory as a real level

Azahar, Old 3DS mode, muted, against the local stock server (`AI_DEV=1`). Builds at main
`c7ecc22`…`HEAD` (this entry), zero warnings in `source/`.

- **15 fps regression (animrt) fixed.** `ToonCache::pump()` called `C3D_FrameSync()` at the
  loop top, a second vsync wait per frame next to `C3D_FrameBegin(SYNCDRAW)`: 15.0 fps with
  29 ms in the "audio" lap. pump now runs right after FrameBegin (which already fenced the
  previous GPU frame): 29.9 fps in TTC; 26–30 fps during emote/clip shots (the dips are the
  ~100 ms shot dumps). Racing's own build held 29.9 fps.
- **Emotes (animrt):** autotest `emote: 0 wave`, `6 dance`, `13 bow`, `12 belly flop`,
  `9 applause` all play (dev AI grants the 25 emotes); screenshots show each pose.
- **Battle FX (battlefx):** in a Silly Street fight (0.25 s shot series) the tart sits in
  the toon's right hand (JNT `def_joint_right_hold` on the drawn torso) and lands on the
  Cog's chest with the purple tart splat during the pie-small react. The Cog-death burst is
  now upstream's `explosion.bam` (0.4, eye billboard, 0.6 s) instead of a screen-filling
  kapow. Effects are small at the current fixed battle camera (MovieCamera port pending,
  Antigravity task `battlecam`).
- **Golf (golf):** Acorn Acres loads; the `golf: board` helper did not seat the toon in a
  kart on the stock server (no golf log lines) — open; to re-test with the golf author's
  checklist (`docs/design/golf.md`).
- **Sellbot factory (`world/level.*`, bake_level.py):**
  - Interior is SelbotLegFactory as 94 zone chunks (worst visible set 10,065 tris / 84
    draws / 748 KB textures); entrance placement (20.7, 21.2, 3.7) matches
    FactoryLevelMgr's first InterestingLocation (20, 21, 0).
  - Visibility interest follows the zone underfoot (4–6 zones); the entrance door
    (CogDoorHandShake, DistributedDoorEntity) opens on contact, blocks while closed, and its
    VisibilityExtender shows the Lobby; the Lobby desk collision stops the toon as upstream.
  - Lobby Cold Callers / Mover & Shaker / Name Dropper generate as DistributedFactorySuit,
    walk their GoonPathData paths and are drawn as real Cogs. Touching one sent
    `DistributedFactorySuit.requestBattle`; the level battle generated on its spec battle
    cell (20.8, 157.4) with all three cell cogs joining (faceoff taunt "Hold it, Toon!").
    With the dev gag pouch the toon won (Reward → Resume, stays in the factory); dead cogs
    vanish (a stale nametag fixed afterwards: cogs in a battle are left to world/battle.*).
  - The entrance-hall button (CogDoor_Button) is a floor the toon stands on; stepping on it
    sends requestInteract and the AI's "playing" state sinks it and turns it green.
  - 26.9–29.9 fps inside, heap 2.9–3.9 MB, linear free 17.9 MB.
  - Not yet: stompers, lifts, barrels, goons, crates, gears/conveyor/paint mixers, the
    foreman and the factory end; mint / DA office / country-club rooms on the same runtime.

## 2026-10-24 — Sellbot factory, every entity and the end (world/level.*)

Azahar Old 3DS, muted, stock server with `AI_DEV=1` (plus `AI_DEV_STRONG=1` and
`AI_DEV_WEAK_COGS=1` for the foreman run). `AUTOTEST_ONLY="go factory,factory: …"`.

- **Buttons:** stand on the entrance-hall button → `requestInteract` → it sinks and turns
  green (AI "playing").
- **Stompers (zone 17):** the motions run from the level start; standing beside 20001
  squishes for 3 laff per stomp (`setOuch`), with Toon.enterSquish and a 4 s stun.
- **Lift 10041:** rides 43.8 → 208.8 with the toon within ~1 unit of the floor;
  `setAvatarEnter`/`Leave` once each way (hysteresis).
- **Barrels (warehouse):** `requestGrab` → `setGrab` by us → the barrel greys (the AI adds
  the gags).
- **Gears (lava room):** standing on a vertical gear's rim (z 66.6).
- **Paint mixer:** carried.
- **Conveyor:** carried at exactly 2 units/s.
- **Goons:** a catwalk goon's radar zaps for 7 laff (beam flashes red); dropping onto a goon's
  head stuns it (collapse pose), on entering only.
- **The end:** walking into the foreman's cell (skelecog Head Hunter level 9, now drawn as a
  robot body with its tie) starts the cell battle; it ends in `FactoryReward` (now mapped to
  Reward, like Building/Mint/Stage/CountryClubReward), `setDefeated` arrives, and the toon
  teleports to the Sellbot HQ courtyard 11000 (FactoryInterior.handleFactoryWinEvent).
  Without weak cogs a lone dev toon (137 laff, level-5 gags) lost to the foreman group,
  which is a four-toon fight upstream: sad → courtyard, also as upstream.
- **Performance:** 26–30 fps throughout (the dips are the 1 s / 0.25 s screenshot series),
  heap 3.9 MB, linear free 14–18 MB.
- **Autotest:** helpers now trigger on a step's first frame (`stepBegan`) instead of
  `stepTime() < 0.05`, which a screenshot stall could skip.
- **Still open in the factory:** crates and grids, crusher cells, cut scene, ambient sounds,
  the foreman-confronted message.

## 2026-10-24 — Cashbot mint floors (multi-room levels, world/level.*)

Azahar Old 3DS, muted, stock server with `AI_DEV=1 AI_DEV_STRONG=1 AI_DEV_WEAK_COGS=1`
(weak Cogs now also revive with 1 HP). `AUTOTEST_ONLY="go mint,mint: …"`: board the Coin Mint
elevator in the Cashbot HQ courtyard (`elevator_origin_1`).

- **Floors load from the layout bake.** Floors 0 (7 rooms), 10 (9), 14 (10) and 17 (11 rooms,
  21 pieces, 20 door frames) came up. The server's room ids matched the baked `MintLayout`
  every time, the toon spawned on room 0's entrance point, and one interest went out
  (`[2, mint zone, every room's zone]`).
- **Walk (floor 10).** The toon was teleported through all 17 pieces in order. Each room and
  hallway was detected from its floor (`toonEnterRoom`) and the ±1 / ±2 window followed.
  Hallways, door frames and the next room line up, as the bake predicted.
- **Battles.** Mint skelecogs (level 11–12) on their battle cells, `MintReward`.
- **The end (floor 17).** The supervisor's cell battle ends in `MintReward`; `setDefeated`
  sends the toon back to Cashbot HQ 12000 (`handleMintWinEvent`). Leaving the facility
  now works even when the target is the HQ still loaded underneath it.
- **Going sad.** Without weak revives a lone toon lost to the supervisor's group (four
  toons upstream). It was teleported to Donald's Dreamland 9000, which is
  `getSafeZoneId(defaultZone)` as upstream (the AI sets `defaultZone` to Cashbot HQ). This
  replaced our old "current hood" rule, which left a sad toon inside the facility.
- **Performance.** Worst view 5,055 tris / 87 draws (floor 10). 26.7 fps mean over the
  walk with a screenshot every second; the 109 ms `dplace` maxima are the shot dumps (the
  factory runs show the same). Heap 4.1 MB, linear free 10.5–15 MB.
- **Not yet run.** DA office (stage) and country-club floors: they are baked and load
  through the same code, but entering them (Lawbot lobby elevators, Bossbot golf karts)
  is untested.

## 2026-10-24 — DA offices and country clubs (same floor runtime)

Azahar Old 3DS, `AI_DEV=1 AI_DEV_STRONG=1 AI_DEV_WEAK_COGS=1` (strong toons now get every
gag track; autoplay picks sound against groups).

- **DA office.** The DA Office A elevator (13200, `elevator_origin_0`) leads to
  `stage_13300_2_0` (5 rooms). All 9 pieces were walked and detected.
- **Floor elevator.** The guard battle was won in one sound round, then `requestBoard`
  and a seat (`fillSlot`, seat 0). A sad toon is refused (reason 2, min laff), as upstream.
  The ride to the next floor (`setStageZone`, now carried by `FacilityNextFloor` because the
  AI deletes the old stage at once) is **not yet seen in Azahar**.
- **Country club.** A Bossbot golf kart (`DistributedCogKart.setPosHpr`) boards to
  `club_10600_3_0` (5 rooms). All 9 pieces were walked and detected, after piece detection
  switched to the floor nearest the feet (tunnel floors reach into rooms).

### 2026-10-24 — bosses2 VP implementation (runtime 8b972d6)

- **Platform/build:** macOS host ASan/UBSan and devkitARM Old-3DS-target build.
  `DEVKITPRO=$HOME/devkitpro DEVKITARM=$HOME/devkitpro/devkitARM make -j4` passed,
  zero compiler warnings in source. The toolchain still emits its crtn.o
  `.note.GNU-stack` linker warning. ELF text/data/bss: 1,237,800 / 44,116 /
  235,200 bytes; combined 1,517,116, below the 4 MiB code/static cap.
- **Boss host tests:** `sh tools/boss_test/run.sh` passed. 87 independent Panda DC
  vectors, nested barrier lists, eight-toon membership, truncated/oversized field
  rejection, damage recovery, swept pie collision/flight, phase/barrier policy and
  cage ending. The recording transport exercises the actual Client's exact
  hitBoss(1), clsend senders, membership/phase denial, healing, pie-stock handling,
  BattleFinal reserve barrier, round placement and static-lobby reconnect.
- **Existing regressions:** network unit (391 descriptor vectors), single client,
  two clients and disconnect/reconnect passed. All 27 suites recorded by
  `tools/boss_test/regressions.py` passed: 17 minigame suites, battle FX, book,
  fishing, animation/cache, zone format, shop, Cog buildings, tailor, classic
  character and ToonDNA. DNA: 46,106 netstrings and 19,584 Make-A-Toon builds.
  The animation harness was updated to model FrameBegin's documented GPU fence
  before pump; protected runtime cache/avatar code is unchanged. Its peak was
  4,000,128 bytes, within the 4 MiB shared cache budget.
- **Extra host checks:** projection 405 grid points, zero failures. Terrain
  collision: 400 Panda floor queries, 200 camera segments, 300 random walks /
  90,000 frames; zero mismatches/tunnels/penetrations outside tolerance. ASan/UBSan
  passed. Scratch/reference files stayed under this worktree's sdmc-out.
- **VP assets:** 12 models, 96 clips, four collision files, three metadata files;
  all 115 outputs and the manifest byte-identical across two complete processes.
  Weighted skinning is checked against Panda within 0.001 units. Decoded-asset
  offscreen preview inspected: VP body/torso/head/treads assemble correctly.
  `asset_check.py` passed magic/version/vertex-order, animation variation,
  metadata/count/hash and aggregate-budget checks.
- **Asset budget:** geometry + active/pending largest clip sets 5,409,224 bytes
  (6 MiB cap); textures 364,000 bytes (8 MiB cap); combined 5,773,224 linear bytes,
  excluding existing shared actor/UI/audio pools. Collision heap incl. stamps
  31,568; clip attachment pair <=13,824 heap bytes. Unique models: 2,588 triangles,
  42 batches. SD disk set 26,098,096 bytes. See design/boss_assets.json for hashes
  and per-model/per-clip costs.
- **FPS / full-app heap-linear-VRAM / GPU timing:** N/A on the host. Azahar and
  hardware were deliberately not run. No live stock-server boss victory is
  claimed tested. Approximate combat hit volumes, condensed cinematics, normal
  toon costumes and local-only cosmetic pie flight are documented limitations.
  Old 3DS Azahar entry/round/pie/reward/return, production suit/merit requirements,
  multiplayer behavior, SD failure/reconnect and peak draw/memory checks remain
  required; use the exact checklist in design/bosses.md before claiming gameplay
  verification.
- **Local evidence:** sdmc-out/bosses2/{build,host,nettests,determinism,asset_check}.log,
  regressions/results.json, collision/test.log, projection.log, vp_preview.png.


### 2026-10-24 — bosses3 Cashbot CFO (runtime 9a6fc86)

- **Platform:** macOS host tests and devkitARM cross-build only. Azahar, hardware,
  a live CFO server session and a live victory were deliberately not run. FPS,
  animation CPU time, whole-app heap/linear/VRAM and command-buffer peaks are
  unmeasured; do not treat these checks as gameplay/performance validation.
- **Build:** the required DEVKITPRO/DEVKITARM `make -j4` passed, with zero warnings
  in source/. The existing devkitARM crtn.o GNU-stack linker warning remains.
  ELF text 1,342,384, data 51,444, BSS 333,744; sum **1,727,572 bytes** (<4 MiB).
- **Boss host suite:** Panda supplies 148 independent required/RAM/custom wire
  vectors. Pure decoders pass every truncated prefix transactionally. Tests cover
  crane bounds/physics, normalized impact, goon radar/head contact and damage
  parsing. Real Client tests pass exact cable/impact payloads, ownership/phase/
  membership rejection, full required+OTHER child generation and truncated
  regeneration, smoothing reset, helmet ownership, one-shot reward confirmation,
  battle orientation, child leaving and static CFO-lobby reconnect. ASan/UBSan
  and -Wall/-Wextra/-Werror are enabled.
- **Baked collision:** ASan/UBSan host reader passes all 13 stock CFO teleport,
  Cog-round and crane-round positions. First vault ground is -0.0015, second
  vault ground 0.0126 (expected zero, tolerance .25). Collision heap **12,348**.
- **Network:** tools/nettest/run_tests.sh passed on local port 7398: 391 independent
  descriptor vectors, single-client login/zone/heartbeat, two-client visibility,
  and disconnect/reconnect. No EJECT or test failure.
- **Existing regressions:** all 29 suites in tools/boss_test/regressions.py passed:
  17 minigames, battle FX/camera, book, fishing, animation streaming/playback,
  facility runtime, Toon DNA, zone unit tests, shop, Cog building, tailor and
  classic characters. Separate racing and estate contracts passed (estate's
  hardcoded reference path was redirected in memory to this worktree's ref/ref).
  Projection passed 405 grid points with zero failures.
- **Assets:** two independent complete CFO bakes produced byte-identical manifests
  and all **115** referenced outputs (16 models, 96 clips, collision, anchor and
  Resistance NPC DNA). Re-baking VP with the shared code preserved all **115**
  original hashes in docs/design/boss_assets.json. Header/hash/animation-variation
  validators pass for both bosses. The first QA pass caught a tread-prefix/report
  refactor error; corrected before these final reproducibility checks.
- **Budget:** new model geometry **194,132**, largest four-part clip **2,004,480**,
  model+active/pending clips+1.75 MiB goon reserve **6,038,100** (<6,291,456),
  new textures **658,688**, including goon texture allowance **789,760** (<8 MiB).
  Attachment records add at most 13,824 regular-heap bytes for the clip pair.
  SD set **20,207,009** bytes. Unique model inventory: 4,918 tris / 80 batches;
  actual instanced frame costs still require Azahar/hardware measurement.
- **Reused assets:** no goons were rebaked. The ignored existing facility goon
  files are not present in this worktree. Install the existing level/props model,
  walk/collapse/recovery clips and cone with the main data set; runtime validates
  actual headers and enforces a 1.5 MiB aggregate goon-clip cap before use.
- **Visual QA:** decoded TTM/TTA actor and arena offscreen previews inspected.
  Correct part attachments and a visible floor/vault environment. The preview's
  detached-camera issue was fixed before inspecting the arena. These are Panda
  decode previews, not screenshots of the 3DS renderer.
- **Required next validation:** docs/design/bosses.md has exact dev flags, assets,
  controls, production entry requirements, two-client ownership/throws/helmet,
  goon/treasure, 500-damage ending/reward and Old 3DS memory/performance checks.
  Autotest labels cfo: board / rounds / cranes / end record one-second shot series;
  they issue physical controls/guarded requests and never force damage/phases.
- **Evidence:** sdmc-out/bosses3/{build_final,boss_tests_final,net_tests,
  cfo_collision,asset_check,vp_asset_check,preview}.log, determinism.json,
  elf_size.txt, cfo_preview.png, cfo_arena_preview.png and regressions/results.json.

### 2026-10-24 — bosses4 Lawbot CJ (runtime 26987be, final follow-up on branch)

- Platform: macOS host ASan/UBSan + devkitARM build only. **Azahar/hardware not
  run, per task instruction.** FPS, runtime heap/linear/VRAM and frame GPU/CPU
  measurements are therefore unavailable. Do not infer a live CJ win from this.
- Build: `DEVKITPRO=$HOME/devkitpro DEVKITARM=$HOME/devkitpro/devkitARM make -j4`
  passes, zero warnings in source/. Inherited crtn.o missing GNU-stack linker
  warning remains. ELF text/data/BSS: 1,397,096 / 51,444 / 617,560 bytes in the
  final build; total 2,066,100 (<4 MiB).
- `sh tools/boss_test/run.sh`: PASS with ASan/UBSan and strict host warnings.
  177 independent Panda fields; required/OTHER child enter streams; every
  truncated prefix leaves CJ snapshots unchanged; class/index limits, owner/
  phase/ammo/reload guards, exact hitChair/index and hitBoss(1), barriers,
  ballistic/scale/gavel logic, summon bitmask updates and both battle poses.
  Actual CJ TTC loads 265 triangles, 0 caps, 3 triggers, 32×51 grid, 48,440 bytes
  heap. Seven floor queries at stock entry, both initial Cog sides, solo cannon,
  witness and courtroom points all return -0.0252 (within 0.25 of zero).
- `PORT=7498 bash tools/nettest/run_tests.sh`: PASS. 391 generated descriptor
  vectors, unit packing, single-client login/zone, remote suit, two clients,
  heartbeat and forced reconnect all pass. DC hash remains 547155168.
- `python -I -B tools/boss_test/regressions.py`: 33 suites PASS, covering the
  original 29 plus level games, estates, racing and the current factory/LevelSpec
  contract. The extra standalone factory test initially failed because it still
  required removed factory.h/FCT1. Its runner now checks the current Level API,
  bakes two real upstream TLV specs to local scratch and executes the existing
  ASan/UBSan LevelSpec reader/room-placement test. No protected runtime/baker
  source changed. Factory test reads 572 real factory entities and the mint room.
- Asset validation: CJ 116/116 file hashes and TTA shape/vertex/attachment
  checks PASS; 16 TTM, 96 TTA, TTC + BPA1/CJS1/BND1. Two independent final CJ
  processes produced identical manifests and every file SHA256. VP/CFO rebakes
  match their committed manifests exactly, 115/115 hashes each. CJ final manifest
  is docs/design/cj_assets.json; no existing VP/CFO asset changed.
- CJ measured bytes: models 192,110 geometry; largest four-part clip 1,609,920;
  geometry + largest active/pending pair 3,411,950; reserved runtime cap aggregate
  3,592,110; textures 785,664; disk 16,528,929. Manifest unique geometry is
  6,773 triangles / 64 batches. This is not the instanced worst-frame total.
- Decoded native TTM/TTA previews checked for attached boss parts and textures.
  Courtroom/scale previews use native decoded models; actual 3DS camera and
  conservative collision volumes still require Azahar inspection. UI error/B
  return and GPU-fenced clip publication are built, not emulator-verified.
- Intentional visual simplifications: condensed introduction/travel, shared
  Big Wig juror visuals instead of random suit/propeller movies, simplified
  seated/tilting chairs and evidence collision volumes; no bespoke CJ audio.
  CEO remains a plan, now with exact own field lists and types/flags.
- Evidence: ignored sdmc-out/bosses4/{build,boss_tests,nettests,regressions,
  cj_collision,cj_asset_check,vp_asset_check,cfo_asset_check,preview}.log,
  cj_first_manifest.json, cj_repeat_final.log, vp_rebake.log, cfo_rebake.log,
  regressions/results.json, cj_{preview,arena_preview,scale_preview}.png.
- Mandatory main-agent checks: docs/design/bosses.md “CJ verification and
  main-agent handoff” lists elevator/seat race, both Cog sides/reserves, two-client
  cannon ownership/ammo/chairs, evidence weight/bonus, scale alignment, all hazard
  codes/HP, real victory/summon/promotion, loss/sadness/reconnect/second encounter,
  missing assets and eight-toon Old 3DS memory/frame/command peaks. Use autotest
  `from cj: board` for the four shot-series stages, then a manual production run.

## 2026-10-24 — merges: battle cinematics, VP, room games, CFO, CJ, CEO (checkpoint)

Azahar Old 3DS, muted, `AI_DEV=1 AI_DEV_STRONG=1` (+ `AI_DEV_WEAK_COGS=1` for bosses).

- **Battle cinematics.** After the merge every movie shot looked at the street's origin
  (`BattleCamera::sample` took the battle centre from `actorPos(battleId)`). With
  `Battle::center`, trap (four rounds), lure (quicksand swallowing the lured Cog) and heal
  movies frame the fight; the victory dance plays. Sound and drop are not yet seen.
- **VP.** `DistributedVPElevatorAI` overrides `checkBoard` (REJECT_PROMOTION), so `ai_dev.py`
  now patches the VP/BB/club elevator subclasses. Lobby 11100 → board →
  `Elevator` / `Introduction` / `BattleOne`.
  - The lobby anchor was read from SD every frame (input 30+ ms, ~15 fps); it is now read
    once.
  - The lone dev toon lost the Cog round: weak Cogs didn't reach boss rounds (fixed on
    `bosses5` in `ai_dev.py`), and autoplay wasted rounds on trap and lure (now only when
    forced). A rerun is pending.
- **CFO.** Lobby 12100 → board → `Elevator` / `Introduction` / `BattleOne`; the same round
  loss as the VP. The cranes are not yet reached.
- **Room games.** Each test step goes to the game's spec position (bringing its zone into
  view), then to the live object.
  - Found live: the factory crate, the DA office laser field and security camera, the
    country-club mole field and golf green. This club floor has no maze.
  - Crates and the mole hills render.
  - Not yet seen: the laser grid and golf holes in the shots; any game being played.
- **CJ / CEO.** Merged; not yet run in Azahar. The CEO is Codex's checkpoint: it builds clean
  and the net and boss tests pass.

## 2026-10-24 — transitions (doors, tunnels, teleports) after the merge

Azahar Old 3DS, `AUTOTEST_FROM="move: door" AUTOTEST_UNTIL="see boat"`.

- **Doors.** In and out of the TTC Gag Shop (2519 ↔ 2000), and a Silly Street building
  (2132 → 2671), run as door transitions with the fade.
- **Teleport.** "see cog" → 2100 plays the teleport-out / in path.
- **Tunnels: broken camera / track origin.**
  - `move: tunnel` starts 2000 → 2132, but the camera ends up inside the terrain (all-green
    frames).
  - Cause: `Transition::computeTunnelOrigin` uses the `.pts` point of the trigger owner,
    `linktunnel_tt_2132_DNARoot` at (−68.4, −202.6). The toon is in that tunnel's
    `tunnel_trigger` at about (23, −167), about 97 units away. All TTC street tunnels show the
    same ~97-unit offset.
  - Upstream tracks are relative to the tunnel model's `tunnel_origin` node, not the DNA
    root, so every zone's `.pts` needs `tunnel_origin` anchors per link tunnel (zone /
    street bakers), then a re-bake.
- **Not yet seen:** `move: teleport` from the map tab (cut off by the run's end).
- **Tunnel origin fixed.** `bake_zone.write_points` now writes `<tunnel>_origin` from the
  tunnel model's `tunnel_origin` node. All 25 zones were refreshed with
  `bake_street.py --zone all --points-only` (56 origins), plus the RomFS `ttc_sz.pts`, and
  `Transition` prefers that point.
  - 2000 → 2132 now walks into the tunnel with the camera outside, then comes out on the
    Silly Street side, as upstream.
  - Still open: about 4 s after arriving, the toon entered building 2670 (`door out start`
    with an unnamed exit). Find what walks it into that door trigger.
  - Note: `make` does not repack RomFS when a data file changes; delete the `.3dsx` to
    force it.

## 2026-10-25 — boss runs, the "crashes", test-harness fixes

- **The VP "crashes" were the test harness.** Two `azahar_test.sh` runs overlapped. The older
  run's exit trap ran `pkill -9` on every Azahar, killing the newer run's emulator, and the
  newer run had backed up the first run's *test* config as the user's. The user's Azahar
  settings (volume 0.6, New 3DS, 4x, layout 4) were restored from the last clean backup.
  `azahar_test.sh` now takes a lock (one run at a time) and restores on INT/TERM/HUP, with an
  interruptible wait.
- **CPU faults** on the main thread are now logged (`core/crash.*`: type, pc, fault address,
  registers), since Azahar runs without its exception handler.
- **VP.** Lobby → elevator → `Introduction` → `BattleOne`.
  - In one run the lone dev toon won the round, then `RollToBattleTwo` → `PrepareBattleTwo` →
    `BattleTwo`. That run was cut by the harness bug above.
  - In the next run it lost `BattleOne`. Cogs survived repeated all-hitting sound rounds,
    so the `AI_DEV_WEAK_COGS` clamp still does not reach the VP round's Cogs on the running
    server.
  - Dev AI: strong toons now get the 80-gag pouch (autoplay had run out of attack gags and
    healed every round).
- **CEO (checkpoint build).** Lobby 10100 → board → arena collision loaded → the banquet
  battle joined; the round was lost the same way. Antigravity's reviewed CEO is now merged
  (`42c9f14`); not yet re-run.

## 2026-10-25 — Cogdominium runtime, branch cogdo (host/build only)

- Build: prescribed devkitARM `make -j4`, based on 42c9f14; zero source warnings.
  External crtn.o emits the toolchain's GNU-stack linker warning.
- Platform: host sanitizer suites and offline Panda decoding. **Azahar and hardware
  were not run**, per task instruction. FPS, live heap/linear/VRAM and frame time:
  not measured; no claim of a tested live Cogdo victory or proven frame budget.
- `tools/cogdo_test/run.sh`: PASS. 77 Panda field vectors/every truncated prefix,
  nine real Client object streams, four inherited elevator/battle/building classes,
  phase/member/owner guards, per-toon barrel grabs and victor callback. Six seeded
  full maps/spawns, 3,600 exact walking/drop frames, cooler ordering, collision
  navigation, difficulty sentinel, bounded crane physics. All five collision meshes
  load; the maze has continuous ground from entrance to exit.
- Required `tools/nettest/run_tests.sh`: ALL NET TESTS PASSED. Required
  `tools/boss_test/run.sh`: parser/logic and real Client PASS. All 33 existing
  regression suites in `tools/cogdo_test/regressions.py`: PASS.
- Assets: 40 files (including manifest) byte-identical across independent baker
  processes. Every TTM header, batch index and texture bound validated. Offscreen
  decoded maze, suite, exterior and barrel previews compared with source models.
- Unique largest resident model group: maze geometry 350,748 B and textures
  739,136 B; 10,650 triangles / 92 batches before actors. Maximum floor collision
  allocation 30,492 B. Final .text+.rodata+.data+.bss = 2,401,560 B (<4 MiB).
  Runtime static Cogdo controller = 196,608 B. These are bake/link measurements.
- Flying is unsupported, without a fabricated course. Crane and boardroom are
  forced prototypes only. Barrel state handling is tested, but this pinned AI
  does not naturally route to BarrelRoomIntro. Full controls, actual server flow,
  simplifications, budgets and the required Azahar checklist: docs/design/cogdo.md.

## 2026-10-25 — Field offices end to end; street interest; polish merge runs (Azahar, Old 3DS)

- Build: main after 7c0045b plus this change; `make -j4`, zero warnings in `source/`. Azahar,
  Old 3DS mode, unattended `tools/dev/azahar_test.sh`; stock server with
  `AI_DEV=1 AI_DEV_STRONG=1 AI_DEV_WEAK_COGS=1 AI_DEV_COGDO=1`.
- **Field offices (`cogdo:` steps): PASS end to end.** Board block 6 on 2100 → ride to 2606 →
  `WaitForAllToonsInside` → maze (seed, 14 Cogs, 9 coolers; gags picked up and thrown, hits
  logged) → open maze exit → `setGameFinish` → executive suite battle won → `Reward` → back
  to street cell 2108 → `setVictorReady` sent at once (AI: no victor timeout).
  26.2–27.1 fps average (shot series running), worst 148 draws / 13.7k tris on the street,
  61 draws / 7.5k tris in the suite; heap 3.2–3.6 MB, linear free 14.3–17.4 MB.
- Four bugs found and fixed on the way:
  - **Street interest lacked the branch zone.** `changeStreetCell` (and `changeZone` into a
    street cell) sent only the cell + vis list. Upstream `Street.doEnterZone` puts
    `getBranchZone` first: it holds every `DistributedBuilding`, so after the first cell change
    every building (and its suit/cogdo state) left the client. Both paths now include it.
  - **Building pool of 16.** Street 2100 has 43 blocks; the field offices never got a slot.
    Now 64 buildings / 32 elevators; a full pool is logged.
  - **The interior join.** `setToons` is not a required field and the AI sends it only after
    everybody joined, so waiting to see ourselves in it deadlocked `WaitForAllToonsInside`.
    Like upstream's announceGenerate, an interior in our own zone now counts as ours.
  - **Victor ready.** The return used `changeZone(cell)` without the branch zone, so the
    building never arrived and the AI ejected us as "suspicious" after 30 s (fixed by the first
    item). Also: elevator `emptySlotN` is now parsed as the DC's `u32,i8,i16,i16` for every class
    (pinned by `TT_SIG`).
- **Crates: PASS.** The first two faces were refused by `CrushableEntityGrid.checkPush`
  (`setReject` now logged); side 3 was accepted: `setMoveTo` (3,6) then (3,9). Fixed: a reject
  never released the push, so a refused crate could not be pushed again, and a later push could
  be ended by the previous move's finished lerp. The autotest now tries the next face every 3 s.
- **Polish merge chain:**
  - street: door → clerk → interior door → tunnel → arrival → street door → teleport → battle
    movies for throw / trap / lure / sound / drop / heal: PASS. The door after the tunnel is the
    autotest's own step, not a bounce; its doId 0 is because the cell change was 0.1 s earlier.
  - DA office lasers: `hit` per tile on two fields, hazard fired: PASS.
  - Country club: mole field `setGameStart` (60 s, target 7) and golf `acceptJoin`: PASS.
  - fps: 27–30 on steps without screenshot series, about 20 with them (each dump costs about
    100 ms; the ~113 ms "cpu" lines alternate with ~4 ms ones).

## 2026-10-25 — Name shop merge (branch names), Azahar Old 3DS

- Build: main 3ceab52 (the `names` merge) plus a harness option; `make -j4`, zero warnings in
  `source/`. `tools/name_test/run_test.sh`, `tools/nettest/run_tests.sh` (twice, with the
  original tight -20..500 ms emote window restored) pass. Stock server, `AI_DEV=1`.
- `AUTOTEST_FE=1 tools/dev/azahar_test.sh ...` (new: the harness writes `autotest_fe` with
  the same from/until/only lines).
- **Delete toon: PASS.** The confirmation modal shows the toon's name; `requestRemoveAvatar` →
  "updated avatar list (0 toons)", slot 1 becomes "Make a Toon".
- **Typed name: PASS.** "Little Pepper" passes the NameCheck port; `setNameTyped` →
  `nameTypedResponse status=1` (pending: this server has no approver, `status = 1 # TODO`); the
  toon plays as its default "Brown Cat" while the wish name waits, as upstream.
- **Pick-A-Name: PASS** (second run with the typed step left out):
  `namePatternAnswer status=1`, the toon plays as "Frizzy" at once.
- Name-shop screens: four columns with < / > and toggles, Shuffle, Type Name / Pick Name, the
  name in the header. Front-end steps run at 30 fps.
