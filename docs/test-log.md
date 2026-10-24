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

### Not yet tested
- Real hardware (Old or New 3DS).
- Stereo 3D toggle. Code path exists; untested in emulator.
- New 3DS mode and the C-stick.
- Sleep/HOME-menu resume.
