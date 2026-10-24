# Cog HQ Exterior and Lobby Zones (.tz streaming, triggers, and audio)

Date: 2026-10-24. Branch: `coghq`.

Native Nintendo 3DS streamed `.tz` (TZ v2) zones for all four Cog Headquarters courtyards,
lobbies, and factory/office exteriors. Built from upstream Open Toontown `.bam` scene models,
chunked into grid cells with Moore-neighbourhood PVS, wired with tunnel and door triggers,
and accompanied by DSP-ADPCM looped background music.

No changes to `source/` were made in this worktree (runtime changes are specified below for
the main agent).

---

## 1. Reproduction and Outputs

```sh
# Bake all 10 Cog HQ zones, previews, and audio tracks:
.venv/bin/python -I tools/pipeline/bake_coghq.py --zone all --preview --audio

# Or bake individual zones:
.venv/bin/python -I tools/pipeline/bake_coghq.py --zone 11000,11100 --preview

# Run the host validation suite (C++ zone.cpp + collision.cpp with ASan/UBSan):
.venv/bin/python -I tools/zone_test/run.py --coghq
```

### Generated Asset Inventory
- **Zones (`sdmc-out/3ds/toontown3ds/data/zones/`)**:
  - `<zone>.tz`: Streaming TZ v2 package (chunk headers, PVS lists, floor triangles, triggers, TTC1 collision, TTM1 render models with ETC1 textures).
  - `<zone>.pts`: Anchor points for tunnel mouths, door origins, and spawn locators.
  - `<zone>.json`: Per-zone build and budget metadata report.
- **Audio (`sdmc-out/3ds/toontown3ds/data/audio/`)**:
  - `10000.tsnd`, `10100.tsnd`: Bossbot HQ background music (`Bossbot_Entry_v1.ogg`).
  - `11000.tsnd`, `11100.tsnd`, `11200.tsnd`: Sellbot HQ background music (`encntr_suit_HQ_nbrhood.ogg`).
  - `12000.tsnd`, `12100.tsnd`: Cashbot HQ background music (`encntr_suit_HQ_nbrhood.ogg`).
  - `13000.tsnd`, `13100.tsnd`, `13200.tsnd`: Lawbot HQ background music (`LB_courtyard.ogg`).
- **Previews (`sdmc-out/previews/coghq/`)**:
  - `<zone>_planned_top.png`: Top-down orthographic render of decoded TTM chunks.
  - `<zone>_planned_cell_<zone>_<cell>.png`: Ground-level eye views from floor cells.

---

## 2. Cog HQ Zones and Upstream Sources

All zone numbers, model paths, tunnel renames, door connections, and audio tracks derive
directly from upstream Open Toontown source files:

| Zone ID | Area Name | Model Path | Upstream Loader / Citations |
|---|---|---|---|
| **10000** | Bossbot HQ hub | `phase_12/models/bossbotHQ/CogGolfHub.bam` | `toontown/coghq/BossbotCogHQLoader.py:34,54-57` |
| **10100** | Bossbot HQ courtyard (lobby) | `phase_12/models/bossbotHQ/CogGolfCourtyard.bam` | `toontown/coghq/BossbotCogHQLoader.py:36,60-64` |
| **11000** | Sellbot HQ exterior | `phase_9/models/cogHQ/SellbotHQExterior.bam` | `toontown/coghq/SellbotCogHQLoader.py:37,53-73` |
| **11100** | Sellbot HQ lobby | `phase_9/models/cogHQ/SellbotHQLobby.bam` | `toontown/coghq/SellbotCogHQLoader.py:38` |
| **11200** | Sellbot factory exterior | `phase_9/models/cogHQ/SellbotFactoryExterior.bam` | `toontown/coghq/SellbotCogHQLoader.py:39,81-83` |
| **12000** | Cashbot HQ exterior | `phase_10/models/cogHQ/CashBotShippingStation.bam` | `toontown/coghq/CashbotCogHQLoader.py:26,41-47` |
| **12100** | Cashbot HQ lobby | `phase_10/models/cogHQ/VaultLobby.bam` | `toontown/coghq/CashbotCogHQLoader.py:27,48-52` |
| **13000** | Lawbot HQ plaza | `phase_11/models/lawbotHQ/LawbotPlaza.bam` | `toontown/coghq/LawbotCogHQLoader.py:32,53-57` |
| **13100** | Lawbot HQ lobby | `phase_11/models/lawbotHQ/LB_CH_Lobby.bam` | `toontown/coghq/LawbotCogHQLoader.py:34,63-67` |
| **13200** | Lawbot office exterior | `phase_11/models/lawbotHQ/LB_DA_Lobby.bam` | `toontown/coghq/LawbotCogHQLoader.py:33,58-62` |

### Zone Constants
- `toontown/toonbase/ToontownGlobals.py:32-48`:
  - `BossbotHQ = 10000`, `BossbotLobby = 10100`
  - `SellbotHQ = 11000`, `SellbotLobby = 11100`, `SellbotFactoryExt = 11200`
  - `CashbotHQ = 12000`, `CashbotLobby = 12100`
  - `LawbotHQ = 13000`, `LawbotLobby = 13100`, `LawbotOfficeExt = 13200`

### Door Connections (AI Wiring)
- **Sellbot (`toontown/hood/CSHoodDataAI.py:53-73`)**:
  - In 11000: Doors 0..3 (`door_0` .. `door_3`) lead to SellbotLobby (11100).
  - In 11100: Door 0 (`door_0`) leads back to SellbotHQ (11000).
- **Cashbot (`toontown/hood/CashbotHQDataAI.py:45-62`)**:
  - In 12000: Door 0 (`door_0`) leads to CashbotLobby (12100).
  - In 12100: Door 0 (`door_0`) leads back to CashbotHQ (12000).
- **Lawbot (`toontown/hood/LawbotHQDataAI.py:53-68`)**:
  - In 13000: Door 0 (`door_0`) leads to LawbotOfficeExt (13200); Door 1 (`door_1`) leads to LawbotLobby (13100).
  - In 13100: Door 0 (`door_0`) leads back to LawbotHQ (13000).
  - In 13200: Door 0 (`door_0`) leads back to LawbotHQ (13000).
- **Bossbot (`toontown/hood/BossbotHQDataAI.py:61-75`)**:
  - In 10000: Door 0 (`door_0`) leads to BossbotLobby (10100).
  - In 10100: Door 0 (`door_0`) leads back to BossbotHQ (10000).

### Street Link Tunnels
- **Daisy Gardens street 5300 (visgroup 5316)**: `phase_8/dna/daisys_garden_5300.dna:5979` places `linktunnel_sellhq_11000_DNARoot`.
  In 11000, `Tunnel1` is renamed to `linktunnel_dg_5316_DNARoot` (`SellbotCogHQLoader.py:54-55`).
  `Tunnel2` links 11000 to 11200 (`linktunnel_sellhq_11200_DNARoot`), while 11200 links back via `tunnel_group2` (`linktunnel_sellhq_11000_DNARoot`).
- **Donald's Dreamland street 9200 (visgroup 9252)**: `phase_8/dna/donalds_dreamland_9200.dna:3766` places `linktunnel_cashhq_12000_DNARoot`.
  In 12000, `LinkTunnel1` is renamed to `linktunnel_dl_9252_DNARoot` (`CashbotCogHQLoader.py:42-44`).
- **The Brrrgh street 3300 (visgroup 3326)**: `phase_8/dna/the_burrrgh_3300.dna:3806` places `linktunnel_lawhq_13000_DNARoot`.
  In 13000, `TunnelEntrance1` is renamed to `linktunnel_br_3326_DNARoot` (`LawbotCogHQLoader.py:55-56`).
- **Chip 'n Dale's MiniGolf / Golf Zone (17000)**: `phase_6/dna/golf_zone_sz.dna:221` places `linktunnel_bosshq_10000_DNARoot`.
  In 10000, `LinkTunnel1` is renamed to `linktunnel_gz_17000_DNARoot` (`BossbotCogHQLoader.py:54-55`).
  `BossbotCogHQLoader.py:57` also sets `origin.setH(-33.33)`.

---

## 3. Pipeline Design and PVS Streaming

Unlike streets and playgrounds which assemble DNA trees, each Cog HQ zone is a single monolithic
`.bam` scene model. The offline baker (`tools/pipeline/bake_coghq.py`) processes each model into
streamed `.tz` chunks:

1. **Scene graph setup and node renaming (`build_scene`)**:
   - Replicates upstream loader tunnel renames (`linktunnel_*_DNARoot`).
   - Identifies door triggers (`door_trigger_<i>`) and renames their owning door node to
     `cogdoor_<destZone>_<i>_DNARoot`.
   - Stashes unneeded background/skybox/mirror geometry (`SkyBox1`, `SkyBox`, `skyGeo`, `underground`, `LB_Sky`, `CH_SkyBox`).
   - Hides door black hole decals (`doorFrameHole*`) until doors open at runtime.
   - For Bossbot HQ (10000), applies the `-33.33` heading fix on `tunnel_origin`.
2. **Spatial Chunking & Moore-Neighbourhood PVS**:
   - Geometry is partitioned into a uniform XY grid (`split_grid`, cell size 96 units).
   - In accordance with task requirements, each cell's potentially visible set (PVS) consists of
     **itself plus its neighbours** (`max(abs(cx - nx), abs(cy - ny)) <= 1`).
   - This bounds the visible chunk count from any cell to at most 9 chunks, preventing draw-call
     blowups in massive courtyards while providing seamless 360-degree viewing around the avatar.
3. **Collision & Trigger Extraction (`collide.py`)**:
   - Floor geometry is extracted from walkable collision polygons (`normal.z >= 0.707`) and clipped
     to grid cell boundaries so that `tz.cell_at` unambiguously maps any position to its owning cell.
   - Tunnel triggers (`tunnel_trigger`) and door triggers (`door_trigger_*`) are extracted with
     their owning DNARoot identifiers preserved.
4. **Textures & Compression**:
   - Textures are packed into power-of-two atlases and encoded as ETC1/ETC1A4 using at most 3 worker
     threads (`os.environ['TT_ETC1_THREADS'] = '3'`).

---

## 4. Per-Zone Budget Table

Budgets enforced:
- **Draws per PVS**: ≤ 150
- **Triangles per PVS**: ≤ 15,000
- **Geometry memory per PVS**: ≤ 810 KB (829,440 bytes)
- **Texture format**: ETC1/ETC1A4

| Zone ID | Area Name | Chunks | Worst Draws | Worst Tris | Worst Geom (bytes) | Worst Tex (bytes) | File Size (KB) |
|---|---|---:|---:|---:|---:|---:|---:|
| **10000** | Bossbot HQ hub | 47 | 37 | 13,586 | 733,644 | 677,120 | 7,821 |
| **10100** | Bossbot HQ courtyard | 48 | 37 | 13,504 | 729,216 | 677,120 | 7,809 |
| **11000** | Sellbot HQ exterior | 32 | 31 | 4,572 | 246,888 | 797,120 | 5,720 |
| **11100** | Sellbot HQ lobby | 6 | 23 | 5,248 | 283,392 | 414,976 | 1,392 |
| **11200** | Sellbot factory exterior | 32 | 27 | 6,748 | 364,392 | 608,864 | 4,612 |
| **12000** | Cashbot HQ exterior | 147 | 57 | 6,382 | 344,628 | 652,288 | 18,524 |
| **12100** | Cashbot HQ lobby | 50 | 29 | 1,598 | 86,292 | 620,064 | 6,948 |
| **13000** | Lawbot HQ plaza | 131 | 34 | 3,344 | 180,576 | 665,952 | 17,165 |
| **13100** | Lawbot HQ lobby | 51 | 32 | 1,301 | 70,254 | 570,560 | 7,022 |
| **13200** | Lawbot office exterior | 25 | 20 | 2,570 | 138,780 | 608,864 | 3,606 |

All 10 zones comfortably fit within all hardware envelope limits (draw calls ≤ 57 across all zones).

---

## 5. Host Validation Record

Running `tools/zone_test/run.py --coghq` host-compiles `source/world/zone.cpp` and
`source/world/collision.cpp` with `-fsanitize=address,undefined` and executes the full
integrity suite:

- **Format tests**: `test_layout`, `test_atlas_repeat_clamp`, `test_sign_layout`, `test_alpha_only_font_mask`, `test_large_uvs` passed.
- **Header & section validation**: All chunk, vis, floor, trigger, and model section offsets,
  alignments, bounds, and TTM signatures validated.
- **Floor sampling & PVS transitions**: Every floor cell in every zone was sampled with runtime
  raycasts; cell transitions and callbacks functioned without error.
- **Malformed file rejection**: Corrupt fixtures (truncated data, bad version, invalid vis offsets,
  corrupt model magic, out-of-budget sizes) were rejected by the runtime parser.
- **Result**: 10 of 10 zones passed with 0 errors.

---

## 6. Runtime Changes Needed in `source/world/place.cpp`

The following changes should be made by the main agent in `source/world/place.cpp` to integrate
the baked Cog HQ zones:

### 1. `placeKind` and `hoodOf` (`source/world/place.cpp:43-50`)
Cog HQ zones (10000..13999) consist of courtyards, lobbies, and factory/office exteriors, each
streamed as an individual `.tz` file (`11000.tz`, `11100.tz`, etc.):
```cpp
PlaceKind placeKind(u32 zoneId) {
    if (zoneId >= 10000 && zoneId < 14000) {
        // Cog HQ exteriors and lobbies are standalone streamed zones (like playgrounds)
        return PlaceKind::Playground;
    }
    const u32 local = zoneId % 1000;
    if (local < 100) return PlaceKind::Playground;
    if (local >= 500) return PlaceKind::Interior;
    return PlaceKind::Street;
}

u32 hoodOf(u32 zoneId) {
    return zoneId - zoneId % 1000;
}
```

### 2. File Resolution (`Place::resolve` and `tzPath`, lines 52-75)
Because `placeKind` for Cog HQs returns `PlaceKind::Playground`, `fileZone` is `zoneId`, resolving
cleanly to `sdmc:/3ds/toontown3ds/data/zones/<zoneId>.tz` and `<zoneId>.pts`.

### 3. Exit Detection for Cog HQ Doors (`Place::exitAt`, lines 181-224)
Cog HQ doors use the owner convention `cogdoor_<destinationZone>_<doorIndex>_DNARoot`.
Add handling alongside the existing building doors:
```cpp
// Cog HQ doors: owner "cogdoor_<destZone>_<doorIndex>_DNARoot"
if (strstr(t.name, "door") && strstr(t.name, "trigger") &&
    !strncmp(t.owner, "cogdoor_", 8)) {
    const char* p = t.owner + 8;
    const u32 destZone = static_cast<u32>(strtoul(p, nullptr, 10));
    if (destZone > 0) {
        e.kind = Exit::Door;
        e.targetZone = destZone;
        snprintf(e.name, sizeof(e.name), "%s", t.owner);
        return e;
    }
}
```

### 4. Arrival from Street Tunnels and Cog HQ Doors (`Place::arrival`, lines 227-298)
1. **Return door matching**:
   When entering a lobby from a courtyard (or vice versa), find the return door:
   ```cpp
   } else if (!strncmp(t.owner, "cogdoor_", 8)) {
       const char* p = t.owner + 8;
       if (static_cast<u32>(strtoul(p, nullptr, 10)) == fromZone) back = &t;
   }
   ```
2. **Branch-tolerant tunnel matching**:
   When arriving from a street tunnel (e.g. from Daisy Gardens street 5300 / visgroup 5316 into
   Sellbot HQ 11000), `fromZone` is 5300 or 5316, whereas `t.owner` is `linktunnel_dg_5316_DNARoot`.
   Match by branch:
   ```cpp
   if (!strncmp(t.owner, "linktunnel_", 11)) {
       const char* zs = strchr(t.owner + 11, '_');
       if (zs) {
           const u32 target = static_cast<u32>(strtoul(zs + 1, nullptr, 10));
           if (target == fromZone || (fromZone >= 1000 && target - target % 100 == fromZone - fromZone % 100)) {
               back = &t;
           }
       }
   }
   ```
3. **HQ Drop Points**:
   When teleporting into an HQ without an exit trigger (`back == nullptr`), add default drop points
   to `kDropPoints`:
   ```cpp
   const DropPoint kDropPoints[] = {
       {1000, -28.0f, -2.5f, 5.8f, 120.0f},   {2000, -60.0f, -8.0f, 1.3f, -90.0f},
       {3000, 35.0f, -32.0f, 6.2f, 138.0f},   {4000, 86.0f, 44.0f, -13.5f, 121.1f},
       {5000, 0.0f, 0.0f, 0.0f, -10.5f},      {6000, -165.8f, 108.0f, 0.025f, 252.0f},
       {8000, -0.7f, 62.0f, 0.08f, 182.0f},   {9000, 77.0f, 91.0f, 0.0f, 124.4f},
       {10000, 0.0f, -70.0f, 0.0f, 0.0f},     {11000, 0.0f, -200.0f, 0.0f, 0.0f},
       {12000, 100.0f, -500.0f, -48.0f, 0.0f}, {13000, 0.0f, -250.0f, 0.0f, 0.0f},
   };
   ```
