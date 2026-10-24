# Hardware-readiness notes (Old 3DS)

The game has only run in Azahar so far, and Azahar is lenient. This file lists what was
reviewed or changed for real hardware, and what still needs a hardware run.

## Handled
| Area | What we do | Where |
|---|---|---|
| GPU memory from 2 threads | libctru `linearAlloc` is not thread-safe, so every runtime linear alloc/free and `C3D_TexInit`/`C3D_TexDelete` holds `tt::mem::LinearGuard`. The toon loader thread does the toon allocations. | `core/memory.*`, `render/model.cpp`, `render/anim.cpp`, `world/toon_cache.cpp` |
| Command buffer overflow (svcBreak PANIC) | 512 KB buffer, one `BufInfo` per model, a skip guard at 90% | `main.cpp`, `render/renderer.cpp` |
| citro2d state after custom shaders | `Renderer::endWorld()` resets alpha test / depth map / cull; `C2D_Prepare()` runs every frame | `render/renderer.cpp` |
| Cache coherency | `GSPGPU_FlushDataCache` after every vertex/index/frame upload; `C3D_TexFlush` after texture reads | `render/model.cpp`, `render/anim.cpp` |
| Heap split | Pinned at 24 MB heap + 24 MB linear (`__ctru_*_heap_size`), identical for HBL and CIA | `core/memory.cpp` |
| Thread priorities | Main 0x30. Toon loader and net 0x31 (run while main waits on vsync). Audio stream 0x30. All on core 0. | `world/toon_cache.cpp`, `net/connection.cpp`, `audio/audio.cpp` |
| SD writes | Log flushes at most once a second, plus on suspend/sleep/exit | `core/log.cpp` |
| Sleep (sockets die with Wi-Fi) | An APT hook flags the wake; the client force-reconnects and rejoins the same toon and zone | `main.cpp`, `net/client.cpp` |
| HOME menu | NDSP pauses itself. The frame dt is clamped to 0.1 s after resume. The heartbeat thread keeps running. | `main.cpp` |
| Text cost | `C2D_Text` is cached for the Debug tab, nametags/balloons and the HUD | `render/ui.cpp`, `ui/*` |
| DSP firmware | `ndspInit` failure runs the game silently (no crash). Hardware needs `sdmc:/3ds/dspfirm.cdc` (DSP1 dump). | `audio/audio.cpp` |

## To verify on hardware (first run)
1. **Boot memory.** Check that HBL on Old 3DS leaves ≥ 48 MB for our heaps. If boot
   fails, lower `kLinearBudget` (we use about 6 MB of linear today).
2. **Frame time.** Read the Debug tab in the playground (171 draws / 13k tris) and with
   16 stress toons. Emulator CPU numbers are unreliable (±40% between versions).
3. **Loader starvation.** The 0x31 threads only run while the main thread waits. If
   frames take the full 33 ms, toons may load slowly; consider core 1 with
   `APT_SetAppCpuTimeLimit` on Old 3DS.
4. **Wi-Fi.** Connect to a LAN server (`server.txt` `host=`), check RTT on the Debug
   tab, then sleep and wake to test the reconnect.
5. **The CIA build boots** (Azahar accepted it after the RSF fixes).
6. **`C2D_DrawCircleSolid`** (proctex) did not render after the 3D pass in Azahar;
   check it on hardware.
