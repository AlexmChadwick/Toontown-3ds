# Chunked TTC zones: implementation and integration handoff

Date: 2026-10-23. Branch: `streets`. Scope: native zone container, offline bakes,
host verification and integration instructions. No existing repository files were
changed. `AGENT_TASK.md` explicitly forbids running Azahar, root `make`, modifying
AGENTS.md/test-log.md, or integrating in main.cpp in this worktree. The test record
below belongs with the main agent's eventual emulator test-log entry.

## Delivered files and reproduction

```sh
.venv/bin/python -I tools/pipeline/bake_street.py --zone all
.venv/bin/python -I tools/zone_test/run.py
.venv/bin/python -I tools/pipeline/street_preview.py \
  sdmc-out/3ds/toontown3ds/data/zones/2100.tz sdmc-out/previews/streets
$HOME/devkitpro/devkitARM/bin/arm-none-eabi-g++ \
  -std=gnu++17 -fno-rtti -fno-exceptions -march=armv6k -mtune=mpcore \
  -mfloat-abi=hard -mtp=soft -D__3DS__ -Wall -Wextra \
  -I source -I $HOME/devkitpro/libctru/include -fsyntax-only source/world/zone.cpp
```

The default baker processes Silly Street 2100 and playground 2000. It loads the four
street storages specified in zones.md, or the three playground storages, in order.
Sources in ref/ and .venv are read only. Texture encoding uses three workers; BLAS
and OpenMP are limited to one thread. The seed is fixed at 1. Outputs and budget
JSON files go in `sdmc-out/3ds/toontown3ds/data/zones/` (gitignored). Temporary TTM
files are removed automatically. No new dependencies are linked.

The tools import `dna`, `assemble`, `extract`, `ttm` and `collide` without changing
them. `street_atlas.py` wraps the extractor's texture collection. Atlases use
256x256 ETC1/ETC1A4 pages with 32px source tiles and four-pixel gutters. Common
textures occupy the first pages. Triangles are clipped at repeat boundaries with
all attributes interpolated; clamped edge regions are handled separately. Very
large UV repeats (>32 crossed tiles per input triangle) keep a standalone 128px
texture to prevent geometric expansion. Standalone textures retain mipmaps. Atlas
pages deliberately use a single mip to avoid cross-tile mip bleeding.

Batches merge by texture, blend and decal layer. Where both cull states share the
same atlas/state, the combined batch becomes two-sided; elsewhere original culling
is retained. This preserves every visible source face but can increase raster work.
The baker rejects visible sets exceeding 150 draws, 6 MiB geometry or 8 MiB texture
payload, and rejects invalid PVS references, UV overflow and invalid sections.

## Format summary

The authoritative byte layout is the `tz.py` module docstring. TZ01 v1 is little
endian. Every section/blob starts at a 128-byte boundary.

| Section | Entry bytes | Contents |
|---|---:|---|
| Header | 64 | Magic/version, branch, counts, offsets, total file bytes |
| Chunks | 80 | Server zone, PVS span, TTM offset/size, draw/tri/memory counts, AABB, floor span, name |
| PVS | 2 | Sorted unique chunk indices; each set includes its current cell |
| Floors | 40 | Three world-space float vertices, u16 chunk and server zone |
| Triggers | 156 | World AABB, kind, original 64-byte name and DNA owner |
| Collision blob | variable | Embedded unchanged TTC1 v1, whole-zone floor/wall/camera collision |
| Model blobs | variable | One unchanged TTM1 v1 per nonempty chunk |

Empty cells can have floors or PVS with zero geometry. Street chunks correspond
exactly to the 56 numeric DNA visgroups, retaining their explicit vis lists.
Playground chunks use a uniform **128-unit grid**, with render triangles owned by
centroid, their full bounds retained, and floor polygons clipped to grid boundaries.
The larger grid avoids multiplying draw calls; the 80-unit experiment needed 190
world draws. Geometry crossing its owning grid square remains visible through its
full AABB. Playground PVS conservatively includes any chunk whose bounds can be
within 150 units of a point in the square (120 far distance + 30 camera margin).
Playground cells all keep server zone 2000, so grid transitions cause no net traffic.

Each embedded TTM contains only the atlas pages/standalone textures it uses. Pages
are shared during baking, then duplicated between blobs. **Residency budgets count
all these copies**, including mip data. No sharing between runtime Model instances
is assumed. The table reports payload bytes; heap tables, collision, allocator
rounding and renderer resources are additional. Current results leave substantial
headroom for allocation rounding. Runtime metadata is capped at 256 chunks, 100k
floor triangles, 4096 triggers, 65536 PVS entries and an 8 MiB collision blob.

The host reader verifies section spans, disjoint TZ sections, PVS integrity, floor
ownership, finite bounds, TTM vertex/index/texture spans and indices, mip sizes,
collision table indices, draw counts and budgets. Runtime repeats the TZ/TTM checks
and delegates TTC validation to CollisionWorld. An unsupported version or damaged
file returns `false` and a readable `Zone::error()`; callers must display it.

## Bake measurements

All numbers are actual baked payloads before camera frustum/distance rejection.
They include repeat-UV subdivision and duplicated textures. A worst row takes the
maximum of each column over all cells; in these bakes the street's maxima coincide
at 2155. Source geometry is not decimated.

| Zone / selection | Chunks | Draws | Triangles | Geometry bytes | Texture bytes |
|---|---:|---:|---:|---:|---:|
| Silly Street 2100, entire file | 56 | 319 | 67,452 | 3,642,408 | 5,963,776 |
| 2100, worst PVS (2155) | 18 | 119 | 27,181 | 1,467,774 | 2,162,688 |
| TTC 2000, entire file | 16 | 118 | 20,342 | 1,098,468 | 2,268,320 |
| 2000, worst grid PVS | 16 | 118 | 20,342 | 1,098,468 | 2,268,320 |

Street 2100 embeds 781 standable tagged floor triangles and 88 trigger records.
Playground 2000 embeds 795 clipped floor triangles and 21 triggers. Full collision
includes walls, camera geometry, capsules and all floor polygons. Street has three
atlas/standalone textures globally; playground has ten (large tiled surfaces retain
standalone textures). PVS rows for every cell are in the adjacent `.json` reports.
The whole zone's geometry/textures are never a mandatory linear-memory residency.

**These are world draw counts, not a whole-frame guarantee.** Toons still need their
own allowance. PVS triangle peaks exceed the provisional 15k envelope even though
memory and draw targets pass. Renderer frustum culling and Zone distance rejection
reduce draws; hardware measurements and possibly LOD/geometry simplification are
still needed. No 30 fps claim is made.

## Runtime and required existing-file integration

`Zone` is main-thread synchronous. `load` reads/validates metadata, loads the regular
heap CollisionWorld and keeps the TZ file open. `update` finds the highest standable
floor under the feet (same four-unit step reach as CollisionWorld), chooses the
cell's PVS, frees outgoing models **before** loading incoming ones, and calls the
cell callback only after the whole new set succeeds. A missed floor query preserves
the previous set (for jumping/outside-floor safety). Allocation/I/O failure clears
the partial set, exposes an error and permits retry on the next update. Queries,
drawing and stable-cell updates allocate nothing.

The task permits describing necessary existing-file changes instead of making them.
Apply these changes in the main checkout before enabling the zone renderer:

1. Add `#include "world/zone.h"` to main.cpp and toon_cache.cpp. Ensure the build
   includes `source/world/zone.cpp` (the existing recursive source discovery should
   pick it up). Update AGENTS.md to allow street 2100 and describe this format.
2. **Serialize the allocator with ToonCache.** As the first statement in
   `bool ToonCache::loaderStep()`, insert `ZoneLinearGuard linearGuard;`. Zone takes
   the same process-wide LightLock internally around allocation/free operations.
   This covers threaded and synchronous fallback loaderStep calls. Never hold a
   ToonCache booking lock when acquiring this guard. Boot-only allocations and
   shutdown must happen before/after loader-thread lifetime as usual. Without this
   one-line integration change, concurrent toon and zone loads are unsafe.
3. Use `zone.collision()` in controller and camera queries instead of the old
   separately loaded playground CollisionWorld. Remove the old world model's load
   and draw, so it does not consume another geometry/texture budget.
4. Add the `Client::changeStreetCell` API below and route callback failures through
   the connection/retry UI. Existing Client's set-location/interest methods are
   private and single-zone, so an unmodified Client cannot perform street interest
   transitions. The zone module itself does not send network messages.

### Exact application-side lifecycle snippet

The following uses the existing Player getters, camera vector and `net` variable.
Place the bridge definition at file scope and the lifecycle statements at the
marked locations. The callback uses the concrete proposed Client API below.
Keep a bottom-screen loading/error panel active on any load/update failure and
display `zone.error()` there; logging below is also useful during integration.

```cpp
#include "world/zone.h"
#include "net/client.h"
#include "core/log.h"

struct ZoneNetBridge {
    tt::net::Client* client;
    uint16_t pendingCell = 0;
    uint16_t pendingZones[257] = {};
    int pendingCount = 0;
    bool retry = false;
};
static void streetCellChanged(void* ctx, uint16_t cell,
                              const uint16_t* zones, int count) {
    auto& b = *static_cast<ZoneNetBridge*>(ctx);
    b.pendingCell = cell;
    b.pendingCount = count;
    for (int i = 0; i < count; ++i) b.pendingZones[i] = zones[i];
    b.retry = true;
}

// After renderer init, on the main thread, before any frame starts:
tt::world::Zone zone;
if (!zone.load("sdmc:/3ds/toontown3ds/data/zones/2100.tz",
               "sdmc:/3ds/toontown3ds/zone_stream.tmp")) {
    TT_LOG("%s", zone.error()); // display zone.error() in the bottom error panel
}
// Immediately after static tt::net::Client net; in the existing main:
ZoneNetBridge bridge{&net};

// Replace the old player.setCollision/reset statements:
player.setCollision(&zone.collision());
player.reset(-130.0f, -155.0f, 0.0f, 0.0f); // verified floor in 2101
// In startNet, before net.start(netConn, cfg):
// cfg.zoneId = zone.currentZone() ? zone.currentZone() : 2101;

// Once per simulation/render iteration, outside C3D_FrameBegin/End:
const tt::world::Vec3 feet{player.x(), player.y(), player.z()};
if (zone.loaded() && !zone.update(feet, streetCellChanged, &bridge))
    TT_LOG("%s", zone.error());
if (bridge.retry && bridge.client->changeStreetCell(
        bridge.pendingCell, bridge.pendingZones, bridge.pendingCount))
    bridge.retry = false;

// Inside each existing world pass, after beginWorld and before toon draws:
const C3D_FVec eye = player.cameraEye();
zone.draw(renderer, {eye.x, eye.y, eye.z}, 120.0f);

// Main thread, after the last frame, before renderer/gfx shutdown:
zone.unload();
```

Replace the four map bounds references to terrain with:

```cpp
ctx.zoneMinX = zone.loaded() ? zone.boundsMin()[0] : -300;
ctx.zoneMinY = zone.loaded() ? zone.boundsMin()[1] : -300;
ctx.zoneMaxX = zone.loaded() ? zone.boundsMax()[0] : 300;
ctx.zoneMaxY = zone.loaded() ? zone.boundsMax()[1] : 300;
```

On reconnect set `bridge.retry = true` if `pendingCount != 0`, even if the feet did
not change cells. For playground use `2000.tz`; all interest lists deduplicate to
`[2000]`. Do not load/update/destroy Zone between FrameBegin and FrameEnd. Zone waits
for GPU completion before any residency change or destruction. Call update once per
frame, not once per stereo eye. The reserved scratch path is overwritten and removed;
it must be a writable SD path distinct from the container, never user data.

### Exact network change to implement in Client

Add this public declaration to client.h:

```cpp
bool changeStreetCell(uint16_t zone, const uint16_t* interestZones, int count);
```

Add the definition in client.cpp (same namespace, using its existing constants and
writers). `kZoneInterest` is the already allocated gameplay interest handle; reusing
it alters the existing interest without accumulating handles. Management-zone
interest on `kUberInterest` stays active. Counts are capped at 250 to fit the current
1024-byte writer; the supplied PVS lists need at most 20 including the branch.

```cpp
bool Client::changeStreetCell(uint16_t zone, const uint16_t* zones, int count) {
    if (state_ != State::InZone || !zone || !zones || count <= 0 || count > 250)
        return false;
    DatagramWriter loc = beginMessage(msg::CLIENT_OBJECT_LOCATION);
    loc.u32(avatarId_);
    loc.u32(shardId_);
    loc.u32(zone);
    if (!finish(loc)) return false;
    DatagramWriter interest = beginMessage(msg::CLIENT_ADD_INTEREST_MULTIPLE);
    zoneCtx_ = nextContext_++;
    zoneDone_ = false;
    interest.u32(zoneCtx_);
    interest.u16(kZoneInterest);
    interest.u32(shardId_);
    interest.u16(static_cast<uint16_t>(count));
    for (int i = 0; i < count; ++i) interest.u32(zones[i]);
    if (!finish(interest)) return false;
    cfg_.zoneId = zone;
    return true;
}
```

This sends CLIENT_OBJECT_LOCATION then CLIENT_ADD_INTEREST_MULTIPLE (existing
msg_types.h constant 201), replacing the zone set on one handle. Callback arrays
already contain current cell, branch and visible cells with duplicates removed.
No DC IDs or field permissions change. Preserve the existing asynchronous interest
completion handling; test rapid crossings, reconnect and the stock server before
shipping. During an initial street transition, set the ClientConfig zone to the
first actual floor cell before connecting (2100 is a branch, not a floor cell).

### Exact tunnel/door trigger query snippet

```cpp
int hits[16];
const int total = zone.triggersAt(feet, tt::world::kToonRadius, hits, 16, true);
for (int i = 0; i < total && i < 16; ++i) {
    const auto* trigger = zone.trigger(hits[i]);
    // Process on entry only; retain the previous hit set to debounce.
    // trigger->owner/name retain e.g. linktunnel_tt_2000_DNARoot or
    // linktunnel_dg_5138_DNARoot; queue a loading-screen transition here.
    queueTunnelApproach(trigger->name, trigger->owner);
}
// For doors/other triggers pass tunnelsOnly=false and inspect trigger->kind.
```

Trigger notifications are conservative sphere/AABB overlaps. Exact arbitrary trigger
geometry remains available through `zone.collision().triggersAt`; the embedded TTC
retains all masks and original solids. Synthetic six-unit tunnel-anchor approach
boxes are also provided where DNA anchors exist; they can overlap native tunnel
triggers. Deduplicate by owner and debounce. Notifications do not automatically
teleport, open doors, implement interiors or send distributed-building fields.
The DG tunnel target requires a separate destination bake before it can be entered.

### Optional Model loader improvement

The unchanged `Model::load(path)` and `CollisionWorld::load(path)` APIs cannot read
an embedded blob directly. zone.cpp currently copies each selected TTM and the
initial TTC through an 8 KiB buffer to the reserved SD scratch file, invokes the
existing loader, then removes it. This is functional and bounded but causes SD writes
and transition stalls. No asynchronous streaming/frame-time claim is made.

The recommended next change is a Model overload
`bool load(FILE* file, uint32_t baseOffset, uint32_t blobSize)`, using an internal
bounded reader that adds baseOffset to every header/table/vertex/index/texture
read. Texture offsets are relative to each embedded TTM, so loadTexture must use
the same base-aware reader (or translate a copied TexEntry's dataOffset after
checking addition). Keep file ownership with Zone and retain the old path wrapper.
Add an equivalent bounded-file/memory overload to CollisionWorld. Replace
materialize/load/remove in zone.cpp with those overloads. Keep allocator guard,
GPU sync, validation, eviction-before-allocation and error handling intact.

## Test record and known limits

2026-10-23, host macOS, branch streets:

- Real bakes of 2100 and 2000 pass the strict format and PVS budget validator.
- Host C++ tests compile the real zone.cpp and collision.cpp with `-Wall -Wextra
  -Werror`, AddressSanitizer and UndefinedBehaviorSanitizer. They visit **54 street
  floor cells** and **13 playground floor cells**, check zone ids, collision floors,
  successful PVS transitions, callback deduplication, budget accounting, no-floor
  retention and every trigger's query at its centre. Two street chunks and three
  playground chunks have no standable floor and remain PVS-only geometry cells.
- Eight corrupt files per zone are rejected: short header, wrong version, bad PVS,
  floor owner, memory metric, model offset, model magic and trigger kind. Missing
  paths and a scratch path equal to the input are rejected; the world stays unloaded.
- Atlas unit tests verify repeat and clamp clipping, conservation of signed triangle
  area, valid atlas coordinates and disabled atlas mips.
- Independent DNA street-point fixtures verify (-130,-155) => 2101,
  (-225,-140) => 2103, (-290,-115) => 2105 and (-315,-140) => 2107.
- Two complete bakes produce byte-identical files, including when encoder worker
  count changes from four to three:
  `2100.tz` SHA-256 `00b5bb0a78cff5e234450887632cfb2d5fe7a48e6dbd827a69337d01cdde3910`;
  `2000.tz` SHA-256 `fe08b688042433adc4c0a768675c01f19138431c3ec3f5ca2ab4b231d2dd1097`.
- The required devkitARM C++17 syntax check reports zero warnings.
- Street previews decode **embedded TTM bytes**, including quantized vertices and
  ETC textures, through Panda's TinyOffscreenGraphicsPipe. Ground and overhead
  images were visually checked for geometry, texture orientation and assembly.

Preview paths (gitignored, relative to this worktree):

- `sdmc-out/previews/streets/2100_top.png`
- `sdmc-out/previews/streets/2100_cell_2101_0.png`
- `sdmc-out/previews/streets/2100_cell_2114_13.png`
- `sdmc-out/previews/streets/2100_cell_2124_23.png`

Known limits: original assembler sign text remains omitted; playground's twelve
unresolved mailbox/hydrant/trashcan placements remain reported by the existing
assembler. Street unresolved/skipped features are recorded verbatim in 2100.json.
Atlased textures are deliberately lower resolution and mipless, and mixed cull
state merges can increase overdraw. Peak uncullled triangles need tuning on Old
3DS. All floor scans are linear in the regular-heap floor table (781/795 triangles
here); a larger zone may need an acceleration grid. Streaming performs synchronous
SD I/O and can stall cell transitions. Shared atlas residency and an offset loader
would reduce those costs. Real network transitions, allocator-lock integration,
Old 3DS frame time/fps, process heap/linear/VRAM, stereo and HOME/sleep remain
unmeasured. **No Azahar, hardware test, root build, push or merge was performed**,
per the task's explicit restrictions. The main agent must record those device
measurements in docs/test-log.md when integrating.
