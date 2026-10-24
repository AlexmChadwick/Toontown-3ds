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

### Not yet tested
- Real hardware (Old or New 3DS).
- Stereo 3D toggle. Code path exists; untested in emulator.
- New 3DS mode and the C-stick.
- Sleep/HOME-menu resume.
