# Building Interiors (All Neighborhoods & Streets, Phase 4+)

This document specifies the offline assembly, baking pipeline, hardware budget verification,
deterministic seed rules, and runtime sidecar format for **all enterable building interiors**
across all 7 neighborhoods and 17 street branches in Toontown.

Paths are relative to `ref/open-toontown/`, unless they start with `phase_`, which
means `ref/resources/`.

---

## 1. Stats and Hardware Budget Verification

All 536 canonical building interiors have been assembled and baked using `tools/pipeline/bake_interior.py`.
In addition, 265 non-conflicting street aliases were generated to support `place.cpp` line 200.

### Hardware Budget Envelope (Old 3DS)
- **Triangles**: <= 15,000 tris per interior
- **Batches / Draw Calls**: <= 150 batches per interior
- **Textures**: ETC1/ETC1A4 encoded, clamped to max 128x128
- **Linear Heap Budget**: <= 6 MB for active interior geometry + textures

### Measured Summary Across All 536 Interiors
| Metric | Measured Peak | Budget Limit | Peak Building |
|---|---|---|---|
| **Triangles** | **7,917** | 15,000 (52.8%) | Zone 2513 (Toon Hall, TTC) |
| **Draw Calls / Batches** | **36** | 150 (24.0%) | Zone 2513 (Toon Hall, TTC) |
| **Geometry + Textures (TTM)** | **591 KB** | 6,144 KB (9.6%) | Zone 2513 (Toon Hall, TTC) |
| **Collision Mesh (TTC)** | **14.2 KB** | Regular Heap | Zone 2513 (Toon Hall, TTC) |

Every single one of the 536 interiors comfortably fits well within Old 3DS limits.

---

## 2. Safezone (Playground) Building Interiors (31 Buildings)

### Donald's Dock (DD 1000)
| Zone | Block | Building Name | Interior Type | Room Code | Tris | Batches | Textures | TTM Size | TTC Size | Triggers | Anchors |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **1506** | 6 | Gag Shop | `gagshop` | `gagshop` | 3,090 | 27 | 21 | 321 KB | 5.1 KB | 4 | 3 |
| **1507** | 7 | Toon Headquarters | `hq` | `hq` | 2,486 | 23 | 19 | 387 KB | 8.3 KB | 2 | 6 |
| **1508** | 8 | Clothing Shop | `clotheshop` | `TI_room_L` | 2,256 | 20 | 18 | 223 KB | 3.7 KB | 5 | 2 |
| **1510** | 10 | Pet Shop | `petshop` | `petshop` | 1,685 | 25 | 23 | 381 KB | 2.4 KB | 1 | 4 |

### Toontown Central (TTC 2000)
| Zone | Block | Building Name | Interior Type | Room Code | Tris | Batches | Textures | TTM Size | TTC Size | Triggers | Anchors |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **2513** | 13 | Toon Hall | `toonhall` | `toonhall` | 7,917 | 36 | 22 | 591 KB | 14.2 KB | 1 | 5 |
| **2514** | 14 | Toontown Bank | `bank` | `TI_room` | 2,824 | 21 | 19 | 260 KB | 3.5 KB | 5 | 2 |
| **2516** | 16 | Toontown School House | `school` | `TI_room_L` | 2,256 | 20 | 18 | 225 KB | 3.7 KB | 5 | 2 |
| **2518** | 18 | Toontown Library | `library` | `TI_room_T` | 2,816 | 24 | 21 | 273 KB | 4.8 KB | 5 | 2 |
| **2519** | 19 | Gag Shop | `gagshop` | `gagshop` | 3,090 | 27 | 21 | 321 KB | 5.1 KB | 4 | 3 |
| **2520** | 20 | Toon HQ | `hq` | `hq` | 2,486 | 23 | 19 | 387 KB | 8.3 KB | 2 | 6 |
| **2521** | 21 | Clothing Shop | `clotheshop` | `TI_room_L` | 2,256 | 20 | 18 | 223 KB | 3.7 KB | 5 | 2 |
| **2522** | 22 | Pet Shop | `petshop` | `petshop` | 1,685 | 25 | 23 | 381 KB | 2.4 KB | 1 | 4 |

### The Brrrgh (BR 3000)
| Zone | Block | Building Name | Interior Type | Room Code | Tris | Batches | Textures | TTM Size | TTC Size | Triggers | Anchors |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **3507** | 7 | Gag Shop | `gagshop` | `gagshop` | 3,090 | 27 | 21 | 321 KB | 5.1 KB | 4 | 3 |
| **3508** | 8 | Toon HQ | `hq` | `hq` | 2,486 | 23 | 19 | 387 KB | 8.3 KB | 2 | 6 |
| **3509** | 9 | Clothing Shop | `clotheshop` | `TI_room_2` | 2,798 | 22 | 19 | 262 KB | 3.3 KB | 5 | 2 |
| **3511** | 11 | Pet Shop | `petshop` | `petshop` | 1,685 | 25 | 23 | 381 KB | 2.4 KB | 1 | 4 |

### Minnie's Melodyland (MM 4000)
| Zone | Block | Building Name | Interior Type | Room Code | Tris | Batches | Textures | TTM Size | TTC Size | Triggers | Anchors |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **4503** | 3 | Gag Shop | `gagshop` | `gagshop` | 3,090 | 27 | 21 | 321 KB | 5.1 KB | 4 | 3 |
| **4504** | 4 | Toon Headquarters | `hq` | `hq` | 2,486 | 23 | 19 | 387 KB | 8.3 KB | 2 | 6 |
| **4506** | 6 | Clothing Shop | `clotheshop` | `TI_room_L` | 2,256 | 22 | 20 | 228 KB | 3.7 KB | 5 | 2 |
| **4508** | 8 | Pet Shop | `petshop` | `petshop` | 1,685 | 25 | 23 | 381 KB | 2.4 KB | 1 | 4 |

### Daisy Gardens (DG 5000)
| Zone | Block | Building Name | Interior Type | Room Code | Tris | Batches | Textures | TTM Size | TTC Size | Triggers | Anchors |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **5501** | 1 | Gag Shop | `gagshop` | `gagshop` | 3,090 | 27 | 21 | 322 KB | 5.1 KB | 4 | 3 |
| **5502** | 2 | Toon HQ | `hq` | `hq` | 2,486 | 23 | 19 | 387 KB | 8.3 KB | 2 | 6 |
| **5503** | 3 | Clothing Shop | `clotheshop` | `TI_room_2` | 2,798 | 22 | 19 | 262 KB | 3.3 KB | 5 | 2 |
| **5505** | 5 | Pet Shop | `petshop` | `petshop` | 1,685 | 25 | 23 | 381 KB | 2.4 KB | 1 | 4 |

### Goofy Speedway (GS 8000)
| Zone | Block | Building Name | Interior Type | Room Code | Tris | Batches | Textures | TTM Size | TTC Size | Triggers | Anchors |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **8501** | 1 | Goofy Speedway Kart Shop | `kartshop` | `kartshop` | 2,519 | 28 | 25 | 334 KB | 6.1 KB | 2 | 6 |

### Donald's Dreamland (DL 9000)
| Zone | Block | Building Name | Interior Type | Room Code | Tris | Batches | Textures | TTM Size | TTC Size | Triggers | Anchors |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **9501** | 1 | Lullaby Library | `standard` | `TI_room` | 2,824 | 21 | 19 | 260 KB | 3.5 KB | 5 | 2 |
| **9503** | 3 | The Snooze Bar | `standard` | `TI_room` | 2,824 | 21 | 19 | 260 KB | 3.5 KB | 5 | 2 |
| **9504** | 4 | Gag Shop | `gagshop` | `gagshop` | 3,090 | 27 | 21 | 322 KB | 5.1 KB | 4 | 3 |
| **9505** | 5 | Toon HQ | `hq` | `hq` | 2,486 | 23 | 19 | 387 KB | 8.3 KB | 2 | 6 |
| **9506** | 6 | Clothing Shop | `clotheshop` | `TI_room` | 2,824 | 21 | 19 | 260 KB | 3.5 KB | 5 | 2 |
| **9508** | 8 | Pet Shop | `petshop` | `petshop` | 1,685 | 25 | 23 | 381 KB | 2.4 KB | 1 | 4 |

---

## 3. Street Building Interiors by Branch (505 Buildings)

Every street building with a landmark door has been assembled, merged, and baked.
Each street has 1 street HQ building plus between 21 and 40 standard toon buildings.

| Street Branch | Buildings | Tris (Min / Avg / Max) | Batches (Min / Max) | Total TTM | Total TTC | Sample Preview |
|---|---|---|---|---|---|---|
| **Silly Street (TTC 2100)** | 41 | 2,256 / 2,671 / 2,824 | 19 / 25 | 10.4 MB | 161 KB | `2601_compare.png` |
| **Loopy Lane (TTC 2200)** | 29 | 2,256 / 2,667 / 2,824 | 20 / 25 | 7.4 MB | 115 KB | `2701_compare.png` |
| **Punchline Place (TTC 2300)** | 22 | 2,256 / 2,621 / 2,824 | 19 / 25 | 5.6 MB | 92 KB | `2801_compare.png` |
| **Barnacle Boulevard (DD 1100)** | 20 | 2,256 / 2,628 / 2,824 | 20 / 25 | 5.1 MB | 80 KB | `1602_compare.png` |
| **Seaweed Street (DD 1200)** | 24 | 2,256 / 2,707 / 2,824 | 20 / 25 | 6.2 MB | 101 KB | `1701_compare.png` |
| **Lighthouse Lane (DD 1300)** | 28 | 2,256 / 2,642 / 2,824 | 19 / 25 | 7.1 MB | 110 KB | `1802_compare.png` |
| **Walrus Way (BR 3100)** | 38 | 2,256 / 2,625 / 2,824 | 20 / 25 | 9.5 MB | 143 KB | `3601_compare.png` |
| **Sleet Street (BR 3200)** | 32 | 2,256 / 2,717 / 2,824 | 20 / 25 | 8.3 MB | 131 KB | `3702_compare.png` |
| **Polar Place (BR 3300)** | 25 | 2,256 / 2,737 / 2,824 | 20 / 25 | 6.4 MB | 97 KB | `3801_compare.png` |
| **Alto Avenue (MM 4100)** | 41 | 2,256 / 2,656 / 2,824 | 19 / 25 | 10.3 MB | 158 KB | `4603_compare.png` |
| **Baritone Boulevard (MM 4200)** | 32 | 2,256 / 2,649 / 2,824 | 20 / 25 | 8.1 MB | 129 KB | `4701_compare.png` |
| **Tenor Terrace (MM 4300)** | 37 | 2,256 / 2,640 / 2,824 | 20 / 25 | 9.3 MB | 145 KB | `4801_compare.png` |
| **Elm Street (DG 5100)** | 25 | 2,256 / 2,640 / 2,824 | 19 / 25 | 6.4 MB | 101 KB | `5601_compare.png` |
| **Maple Street (DG 5200)** | 25 | 2,256 / 2,641 / 2,824 | 20 / 25 | 6.3 MB | 99 KB | `5701_compare.png` |
| **Oak Street (DG 5300)** | 18 | 2,256 / 2,674 / 2,824 | 20 / 25 | 4.6 MB | 72 KB | `5802_compare.png` |
| **Lullaby Lane (DL 9100)** | 34 | 2,256 / 2,704 / 2,824 | 19 / 25 | 8.8 MB | 142 KB | `9601_compare.png` |
| **Pajama Place (DL 9200)** | 34 | 2,256 / 2,719 / 2,824 | 20 / 25 | 8.8 MB | 137 KB | `9703_compare.png` |

---

## 4. Zone ID Formula & Runtime Routing

### 4.1 Upstream Canonical Zone ID Formula
In Disney's Open Toontown, every interior zone ID is computed as:
```python
interiorZone = branch - branch % 100 + 500 + block
```
- Citations:
  - `toontown/building/DistributedBuildingAI.py:197-204`
  - `toontown/hood/ZoneUtil.py:126-130`
  - `toontown/toon/NPCToons.py:zone2NpcDict`

This formula maps each branch to its own unique range of 100 interior zones:
- Safezone TTC (branch 2000): zones 2500..2599 (playground landmark blocks 13..22 are zones 2513..2522)
- Silly Street (branch 2100): zones 2600..2699 (blocks 1..71 are zones 2601..2671)
- Loopy Lane (branch 2200): zones 2700..2799 (blocks 1..48 are zones 2701..2748)
- Punchline Place (branch 2300): zones 2800..2899 (blocks 1..41 are zones 2801..2841)
- Safezone DD (branch 1000): zones 1500..1599 (blocks 6..10 are zones 1506..1510)
- Barnacle Blvd (branch 1100): zones 1600..1699 (blocks 2..35 are zones 1602..1635)
- Seaweed St (branch 1200): zones 1700..1799 (blocks 1..29 are zones 1701..1729)
- Lighthouse Ln (branch 1300): zones 1800..1899 (blocks 1..31 are zones 1801..1831)
- Similar unique 100-zone ranges apply to The Brrrgh (3500..3899), Minnie's Melodyland (4500..4899), Daisy Gardens (5500..5899), Goofy Speedway (8500..8599), and Donald's Dreamland (9500..9799).

Under this canonical formula, **every single building in the entire game has a completely unique zone ID** (536 buildings, 0 collisions).

### 4.2 Runtime Bug in `source/world/place.cpp` Line 200
In `source/world/place.cpp` lines 193-204:
```cpp
// Building doors: owner "sz<block>:..." (playground) or "tb<block>:..." (street);
// the interior is hood + 500 + block (ZoneUtil.getBranchZone / DistributedDoor).
if (strstr(t.name, "door") && strstr(t.name, "trigger") &&
    (!strncmp(t.owner, "sz", 2) || !strncmp(t.owner, "tb", 2))) {
    const u32 block = static_cast<u32>(strtoul(t.owner + 2, nullptr, 10));
    if (block > 0) {
        e.kind = Exit::Door;
        e.targetZone = hoodOf(zoneId_) + 500 + block;
        snprintf(e.name, sizeof(e.name), "%s", t.owner);
        return e;
    }
}
```
Notice the comment cites `ZoneUtil.getBranchZone`, but the code executes `hoodOf(zoneId_) + 500 + block`.
- For playground doors (`sz<block>:...`), `hoodOf(2000)` is 2000, so `2500 + block` is correct.
- For street doors (`tb<block>:...`), `hoodOf(2100)` is 2000 instead of 2100, which causes streets in the same hood to collide on shared block numbers (e.g. block 1 exists on street 2100, street 2200, and street 2300).

### 4.3 Dual-Support Implementation
To support both the current runtime and the canonical architecture:
1. All 536 buildings are baked to their canonical zone ID (`branch + 500 + block`).
2. For all street buildings where `hoodOf(branch) + 500 + block` does NOT conflict with a safezone building (e.g. Silly Street block 3 -> 2503), an alias link/copy is automatically generated.
3. This allows the current `place.cpp` binary to immediately load street buildings like `2503` without crashing, while preserving canonical files for all 536 buildings.

### 4.4 Recommended 1-Line Fix for `place.cpp`
To allow all 505 street buildings to be entered without cross-street block collision, change `place.cpp` line 200:
```cpp
// Change line 200 from:
e.targetZone = hoodOf(zoneId_) + 500 + block;
// To:
e.targetZone = (zoneId_ - zoneId_ % 100) + 500 + block;
```

## 5. Layout and Seed Rules with Upstream Citations

Interior generation on the original client is completely deterministic, seeded by the
interior zone ID. The offline pipeline in `tools/pipeline/interior_assemble.py` and
`tools/pipeline/bake_interior.py` re-implements these exact rules.

### 5.1 RNG Seeding
- The RNG is Python's standard `random.Random()`, seeded with `zoneId`:
  - `toontown/building/DistributedToonInterior.py:83-84`
  - `toontown/building/DistributedHQInterior.py:149-150`
  - `toontown/building/DistributedGagshopInterior.py:71-72`
  - `toontown/building/DistributedPetshopInterior.py:72-73`
  - `toontown/building/DistributedToonHallInterior.py:65-66`

### 5.2 Room Model Selection
- Catalog lookup: `randomDNAItem(category, findFunc)` (`DistributedToonInterior.py:48-51`):
  `codeCount = self.dnaStore.getNumCatalogCodes(category)`
  `index = self.randomGenerator.randint(0, codeCount - 1)`
  `code = self.dnaStore.getCatalogCode(category, index)`
  `return findFunc(code)`
- Standard toon buildings (`TI_room` category in `phase_3.5/dna/storage_interior.dna:4-13`):
  - Index 0: `TI_room` (`phase_3.5/models/modules/toon_interior.bam`)
  - Index 1: `TI_room_2` (`phase_3.5/models/modules/toon_interior_2.bam`)
  - Index 2: `TI_room_L` (`phase_3.5/models/modules/toon_interior_L.bam`)
  - Index 3: `TI_room_T` (`phase_3.5/models/modules/toon_interior_T.bam`)
- Toon Hall: `TI_hall` (`phase_3.5/models/modules/tt_m_ara_int_toonhall.bam`) (`DistributedToonHallInterior.py:67`).
- Gag Shop: `phase_4/models/modules/gagShop_interior.bam` (`DistributedGagshopInterior.py:73`).
- Toon HQ: `phase_3.5/models/modules/HQ_interior.bam` (`DistributedHQInterior.py:31`).
- Pet Shop: `phase_4/models/modules/PetShopInterior.bam` (`DistributedPetshopInterior.py:74`).
- Kart Shop: `phase_6/models/karting/KartShop_Interior.bam` (`DistributedKartShopInterior.py:35`).

### 5.3 Color Palette and Assignment
- Upstream table: `toontown/building/ToonInteriorColors.py:2-42`.
  - Toontown Central (2000) & Tutorial (15000):
    - `TI_door`: `[(1.0, 1.0, 0.7, 1.0), (0.8, 0.5, 0.3, 1.0)]`
  - All other hoods (1000, 3000, 4000, 5000, 6000, 8000, 9000):
    - `TI_door`: `[(1.0, 1.0, 0.7, 1.0)]`
  - All hoods share identical base palettes for `TI_wainscotting`, `TI_wallpaper`, `TI_wallpaper_border`, `TI_floor`.

### 5.4 Node Replacement (`replaceRandomInModel`)
- Pattern `random_???_*` (`DistributedToonInterior.py:53-80`, `DistributedGagshopInterior.py:30-55`):
  - `key1 = name[7]`: `'m'` loads catalog node; `'t'` loads catalog texture.
  - `key2 = name[8]`: `'r'` recursively runs replacement on copied model; `'c'` applies color scale.
  - `category = name[11:]` (e.g. `TI_wainscotting`, `TI_wallpaper`, `TI_couch`).
- **Wallpaper sync rule**: When `category` is `'TI_wallpaper'` or `'TI_wallpaper_border'`, the
  RNG is re-seeded with `self.zoneId` (`DistributedToonInterior.py:76`, `DistributedGagshopInterior.py:51`).
  This ensures that wallpaper and border pick the identical color choice index.
- **Collision stashing**: When a model is inserted at a `random_m*` node, any collision node
  named `'collision'` is stashed (`DistributedToonInterior.py:66-67`), so toons do not collide
  with interior prop collision geometry.

### 5.5 Door Placement and Geometry
- **Door model**: `door_double_round_ur` from `phase_4/dna/storage.dna:103`
  (`DistributedHQInterior.py:138-146`, `DistributedGagshopInterior.py:60-67`, `DistributedToonInterior.py:90-95`).
- **Origin scale and offset**:
  - `door_origin` is scaled by `(0.8, 0.8, 0.8)`.
  - Position offset: `(0, -0.025, 0)` for standard toon buildings, Gag Shop, HQ, and Toon Hall.
  - Pet Shop offset: `(0, -0.25, 0)` (`DistributedPetshopInterior.py:90`).
- **Parts setup** (C++ `DNADoor.setupDoor` / `tools/pipeline/assemble.py:228-256`):
  - `door_*_flat` (the door frame) is reparented to the room root and tinted with `doorColor`.
  - `door_*_hole_left` and `door_*_hole_right` are hidden.
  - `door_*_left` and `door_*_right` (3D swinging panels) are hidden until animated.
  - `door_*_trigger` is renamed to `door_trigger_<block>` (or `door_trigger_<block>_<index>` for HQ)
    and reparented to the room root with into-collide-mask `WallBitmask` (0x1) as an intangible trigger.
- **Toon HQ double doors**:
  - `HQ_interior.bam` has `door_origin_0` (south entrance, heading 180 degrees) and `door_origin_1` (north entrance, heading 0 degrees).
  - Both doors get instances of `door_double_round_ur` with triggers `door_trigger_<block>_0` and `door_trigger_<block>_1`
    (`DistributedHQInterior.py:153-170`).

## 6. Sidecar Schema (`<zone>.json`)

Each baked interior is accompanied by a JSON sidecar `<zone>.json`. The runtime
reads this to spawn toons, place NPCs, and link door triggers to the zone change FSM.

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "Toontown3DSInteriorSidecar",
  "type": "object",
  "required": [
    "zoneId",
    "block",
    "interiorType",
    "buildingType",
    "name",
    "spawnPoint",
    "doorExitPoint",
    "teleportPoint",
    "doors",
    "npcOrigins",
    "npcs",
    "roomCode",
    "stats"
  ],
  "properties": {
    "zoneId": { "type": "integer", "description": "Canonical interior zone ID" },
    "block": { "type": "integer", "description": "DNA building block number" },
    "interiorType": { "type": "string", "enum": ["toonhall", "bank", "school", "library", "gagshop", "hq", "clotheshop", "petshop", "kartshop", "standard"] },
    "buildingType": { "type": "string" },
    "name": { "type": "string", "description": "Display name of the building from DNA title" },
    "roomCode": { "type": "string", "description": "Catalog code of base room model" },
    "spawnPoint": { "type": "object", "properties": { "x": {}, "y": {}, "z": {}, "h": {} } },
    "doorExitPoint": { "type": "object", "properties": { "x": {}, "y": {}, "z": {}, "h": {} } },
    "teleportPoint": { "type": "object", "properties": { "x": {}, "y": {}, "z": {}, "h": {} } },
    "doors": { "type": "array" },
    "npcOrigins": { "type": "array" },
    "npcs": { "type": "array" },
    "stats": { "type": "object" }
  }
}
```

## 7. Known Gaps

The baked assets represent the static visual and collision environment. The following
features are dynamic at runtime and should be implemented in future phases:

1. **Animated Props**:
   - **Pet Shop Fish**: `phase_4/models/props/interiorfish-zero` contains the static mesh.
     The swimming loop (`phase_4/models/props/interiorfish-swim` at 0.7 play rate) is an
     actor animation track.
   - **Toon HQ Periscope**: Swivels back and forth in animated state.
   - **Toon Hall SillyOMeter**: During holiday events (`SILLYMETER_HOLIDAY`), the SillyOMeter
     progresses through phases 0-15 with animated dial needles and fluid tubes
     (`tt_a_ara_ttc_sillyMeter_default.bam`). In normal non-holiday operation, it displays
     the static flat meter (`tt_m_ara_int_sillyMeterFlat.bam`).
2. **Dynamic 3D Text & UIs**:
   - **HQ Leaderboard**: In the original client, `empty_board` displays 10 rows of top toon
     scores (`DistributedHQInterior.py:61-128`). Text is rendered dynamically via TextNodes
     and updated via `setLeaderBoard(blob)` datagrams.
   - **Landmark Interior Signs**: In upstream `DistributedToonInterior.py:105-126`, signs
     are cloned from street landmark buildings with letter baselines.
3. **NPC Interactions & UIs**:
   - Clerk inventory purchase GUI (Gag Shop).
   - Tailor DNA/clothing change interface (Clothing Shop).
   - Pet shop doodle purchase and adoption screens.
   - Kart shop kart customization and accessory purchase screens.
   - Street quest NPC dialogue cinematics.
4. **Door Swinging Animation**:
   - `leftDoor` and `rightDoor` swing +-100 degrees over 0.6 seconds when an avatar enters or exits.
     The baked `.ttm` includes the flat door frame (`door_*_flat`); the animated leaves
     can be instantiated as separate props during the door enter/exit sequence.

## 8. Changes Needed in Shared Modules

Per the worktree isolation rules, no shared modules in `tools/pipeline/` or `source/` were modified.
The following improvements are recommended for shared modules in future updates:

1. **`source/world/place.cpp`**:
   - **Street door zone calculation**: Change line 200 from `e.targetZone = hoodOf(zoneId_) + 500 + block;`
     to `e.targetZone = (zoneId_ - zoneId_ % 100) + 500 + block;` to match `ZoneUtil.getBranchZone`
     and allow all street buildings to be entered without block collisions.
   - **Exit routing**: Change line 208 from `e.targetZone = hoodOf(zoneId_);` to
     `e.targetZone = ZoneUtil.getBranchZone(zoneId_);` so street building exits return to the street.
2. **`tools/pipeline/dna.py`**:
   - **Catalog code indexing**: `Storage` only stores `self.categories[code] = category`
     (code-to-category reverse lookup). Upstream interior assembly relies on
     `getNumCatalogCodes(category)` and `getCatalogCode(category, index)` to deterministically
     pick rooms, textures, and furniture. `Storage` should maintain
     `self.catalog = {category: [code1, code2, ...]}` in order of appearance.
   - **`store_texture` catalog categories**: In `dna.py:210-216`, parsing of `store_texture`
     discards `vals[0]` when 3 arguments are present (`store_texture [ "TI_wallpaper" "stripeB5" "..." ]`).
     It only stores the code and texture object. Recording the catalog category is required
     for category-based texture randomizers.
3. **`tools/pipeline/assemble.py`**:
   - **Interior door handling**: `assemble._setup_door_parts` is hardcoded for exterior
     doors on landmark buildings. Interior doors need slightly different logic:
     reparenting `door_*_flat` to the room root with `doorColor`, naming triggers
     `door_trigger_<block>` (or `_<block>_<index>`), and applying the specific door origin
     Z/Y offset (`-0.025` for standard, `-0.25` for Pet Shop).
4. **`tools/pipeline/ttm.py`**:
   - The returned stats dictionary uses key `'file_bytes'` rather than `'bytes'`. Standardizing
     naming across `ttm.write` and `collide.mesh.write` (which returns `'bytes'`) would make
     generic batch scripts simpler.
