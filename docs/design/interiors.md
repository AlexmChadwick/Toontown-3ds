# Building Interiors (Toontown Central, Phase 4+)

This document specifies the offline assembly, baking pipeline, and runtime sidecar
format for all enterable building interiors in the Toontown Central playground (zone 2000).

Paths are relative to `ref/open-toontown/`, unless they start with `phase_`, which
means `ref/resources/`.

---

## 1. Stats and Budget Verification

All 8 Toontown Central playground building interiors have been assembled and baked
using `tools/pipeline/bake_interior.py`.

Budget envelope from `AGENTS.md` and `docs/design/playground.md`:
- Triangles: ≤ 15,000 tris per interior
- Batches / Draw Calls: ≤ 150 batches per interior
- Textures: ETC1/ETC1A4 encoded, max 128×128

### Measured Interior Stats

| Zone | Block | Building Name | Interior Type | Room Code | Tris | Batches | Textures | TTM Size | TTC Size | Coll Tris | Triggers | Anchors |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **2513** | 13 | Toon Hall | `toonhall` | `TI_hall` | 7,917 | 36 | 22 | 592 KB | 14.2 KB | 152 | 1 | 6 |
| **2514** | 14 | Toontown Bank | `bank` | `TI_room` | 2,824 | 21 | 19 | 261 KB | 3.5 KB | 38 | 5 | 5 |
| **2516** | 16 | Toontown School House | `school` | `TI_room_L` | 2,256 | 20 | 18 | 225 KB | 3.7 KB | 42 | 5 | 5 |
| **2518** | 18 | Toontown Library | `library` | `TI_room_T` | 2,816 | 24 | 21 | 274 KB | 4.8 KB | 60 | 5 | 5 |
| **2519** | 19 | Gag Shop | `gagshop` | `gagshop` | 3,090 | 27 | 21 | 322 KB | 5.1 KB | 68 | 4 | 6 |
| **2520** | 20 | Toon HQ | `hq` | `hq` | 2,486 | 23 | 19 | 387 KB | 8.3 KB | 94 | 2 | 8 |
| **2521** | 21 | Clothing Shop | `clotheshop` | `TI_room_L` | 2,256 | 20 | 18 | 224 KB | 3.7 KB | 42 | 5 | 5 |
| **2522** | 22 | Pet Shop | `petshop` | `petshop` | 1,685 | 25 | 23 | 382 KB | 2.4 KB | 24 | 1 | 5 |

All 8 interiors comfortably satisfy the hardware envelope:
- Maximum triangles: 7,917 (Toon Hall, 53% of 15k budget)
- Maximum draw calls: 36 (Toon Hall, 24% of 150 budget)
- Maximum geometry + texture file size: 592 KB (Toon Hall, well within the 6 MB linear heap sub-budget)
- Collision meshes: 2.4 KB to 14.2 KB (fits in regular heap)

---

## 2. Layout and Seed Rules with Citations

Interior generation on the original client is completely deterministic, seeded by the
interior zone ID. The offline pipeline in `tools/pipeline/interior_assemble.py` and
`tools/pipeline/bake_interior.py` re-implements these exact rules.

### 2.1 Zone ID and Block Numbering
- **Zone formula**: `interiorZone = branch - branch % 100 + 500 + block`
  (`toontown/building/DistributedBuildingMgrAI.py:199`).
- For playground zone 2000, `2000 - 0 + 500 + block = 2500 + block`.
- DNA block definitions in `phase_4/dna/toontown_central_sz.dna`:
  - Block 13: `sz13:toon_landmark_TT_toonhall_DNARoot` (line 794)
  - Block 14: `sz14:toon_landmark_TT_bank_DNARoot` (line 607)
  - Block 16: `sz16:toon_landmark_TT_school_house_DNARoot` (line 644)
  - Block 18: `sz18:toon_landmark_TT_library_DNARoot` (line 530)
  - Block 19: `sz19:toon_landmark_TT_gag_shop_DNARoot`, `building_type ["gagshop"]` (line 995)
  - Block 20: `sz20:toon_landmark_hqTT_DNARoot`, `building_type ["hq"]` (line 1090)
  - Block 21: `sz21:toon_landmark_TT_clothes_shop_DNARoot`, `building_type ["clotheshop"]` (line 736)
  - Block 22: `sz22:toon_landmark_TT_pet_shop_DNARoot`, `building_type ["petshop"]` (line 1318)

### 2.2 RNG Seeding
- The RNG is Python's standard `random.Random()`, seeded with `zoneId`:
  - `toontown/building/DistributedToonInterior.py:83-84`
  - `toontown/building/DistributedHQInterior.py:149-150`
  - `toontown/building/DistributedGagshopInterior.py:71-72`
  - `toontown/building/DistributedPetshopInterior.py:72-73`
  - `toontown/building/DistributedToonHallInterior.py:65-66`

### 2.3 Room Model Selection
- **Catalog lookup**: `randomDNAItem(category, findFunc)`
  (`toontown/building/DistributedToonInterior.py:48-51`):
  `codeCount = self.dnaStore.getNumCatalogCodes(category)`
  `index = self.randomGenerator.randint(0, codeCount - 1)`
  `code = self.dnaStore.getCatalogCode(category, index)`
  `return findFunc(code)`
- Standard toon buildings (`TI_room` category in `phase_3.5/dna/storage_interior.dna:4-13`):
  Index 0: `TI_room` (`phase_3.5/models/modules/toon_interior.bam`)
  Index 1: `TI_room_2` (`phase_3.5/models/modules/toon_interior_2.bam`)
  Index 2: `TI_room_L` (`phase_3.5/models/modules/toon_interior_L.bam`)
  Index 3: `TI_room_T` (`phase_3.5/models/modules/toon_interior_T.bam`)
  - Zone 2514 (`bank`): seed 2514 picks index 0 (`TI_room`)
  - Zone 2516 (`school`): seed 2516 picks index 2 (`TI_room_L`)
  - Zone 2518 (`library`): seed 2518 picks index 3 (`TI_room_T`)
  - Zone 2521 (`clotheshop`): seed 2521 picks index 2 (`TI_room_L`)
- Toon Hall (`TI_hall` category in `storage_interior.dna:16`):
  Single catalog code: `TI_hall` (`phase_3.5/models/modules/tt_m_ara_int_toonhall.bam`)
  (`DistributedToonHallInterior.py:67`).
- Gag Shop: `phase_4/models/modules/gagShop_interior.bam` (`DistributedGagshopInterior.py:73`).
- Toon HQ: `phase_3.5/models/modules/HQ_interior.bam` (`DistributedHQInterior.py:31`).
- Pet Shop: `phase_4/models/modules/PetShopInterior.bam` (`DistributedPetshopInterior.py:74`).

### 2.4 Color Palette and Assignment
- Upstream table: `toontown/building/ToonInteriorColors.py:2-12, 23-27`.
  - For Toontown Central (`ToontownGlobals.ToontownCentral = 2000`):
    - `TI_wainscotting`: `[(0.8, 0.5, 0.3, 1), (0.699, 0.586, 0.473, 1), (0.473, 0.699, 0.488, 1)]`
    - `TI_wallpaper`: `[(1, 1, 0.7, 1), (0.8, 1, 0.7, 1), (0.4, 0.5, 0.4, 1), (0.5, 0.7, 0.6, 1)]`
    - `TI_wallpaper_border`: identical 4 options to `TI_wallpaper`
    - `TI_door`: `[(1, 1, 0.7, 1), (0.8, 0.5, 0.3, 1)]`
    - `TI_floor`: `[(0.746, 1, 0.477, 1), (1, 0.684, 0.477, 1)]`

### 2.5 Node Replacement (`replaceRandomInModel`)
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

### 2.6 Door Placement and Geometry
- **Door model**: `chooseDoor()` starts with `door_double_round_ul`, flips last character to
  `r` if not `r`, yielding `door_double_round_ur` from `phase_4/dna/storage.dna:103`
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
  - `HQ_interior.bam` has `door_origin_0` (south entrance, heading 180°) and `door_origin_1` (north entrance, heading 0°).
  - Both doors get instances of `door_double_round_ur` with triggers `door_trigger_20_0` and `door_trigger_20_1`
    (`DistributedHQInterior.py:153-170`).

### 2.7 Specific Interior Features
- **Toon HQ**:
  - Cream pie (`**/cream`) and piano (`**/crashed_piano`) are hidden (`DistributedHQInterior.py:33-34`).
  - Periscope (`**/periscope`) and speakers (`**/speakers`) are shown (`DistributedHQInterior.py:54-55`).
- **Pet Shop**:
  - Swimming fish prop `phase_4/models/props/interiorfish-zero` attached at `(0, 6, -4)` with scale 0.8
    and color scale `(0.8, 0.9, 1, 0.8)` (`DistributedPetshopInterior.py:76-80`).
- **Toon Hall**:
  - Velvet ropes `phase_4/models/modules/tt_m_ara_int_ropes.bam` attached to room (`DistributedToonHallInterior.py:178-179`).
  - Flat SillyOMeter `phase_3.5/models/modules/tt_m_ara_int_sillyMeterFlat.bam` attached to room (`:188-189, 574`).
  - Cardboard cutout scientists duck, monkey, and horse at `npc_origin_1..3` are hidden when 3D NPCs generate (`:192-205, 575-577`).

---

## 3. Sidecar Schema (`<zone>.json`)

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
    "zoneId": { "type": "integer", "description": "Canonical interior zone ID (2513..2522)" },
    "block": { "type": "integer", "description": "DNA building block number (13..22)" },
    "interiorType": { "type": "string", "enum": ["toonhall", "bank", "school", "library", "gagshop", "hq", "clotheshop", "petshop"] },
    "buildingType": { "type": "string", "enum": ["standard", "gagshop", "hq", "petshop"] },
    "name": { "type": "string", "description": "Display name of the building" },
    "roomCode": { "type": "string", "description": "Catalog code of base room model (TI_room, TI_room_L, etc.)" },
    "spawnPoint": {
      "type": "object",
      "description": "Default spawn point when entering interior (matches doorExitPoint)",
      "required": ["x", "y", "z", "h"],
      "properties": {
        "x": { "type": "number" },
        "y": { "type": "number" },
        "z": { "type": "number" },
        "h": { "type": "number" }
      }
    },
    "doorExitPoint": {
      "type": "object",
      "description": "Position where avatar ends door exit walk: (-1.5, -6.0, 0.025) relative to door_origin",
      "required": ["x", "y", "z", "h"]
    },
    "teleportPoint": {
      "type": "object",
      "description": "Avatar teleport-in arrival coordinate: (0, 0, 0.025, 45) for petshop, (2.5, 11.5, 0.025, 45) others",
      "required": ["x", "y", "z", "h"]
    },
    "doors": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["name", "origin", "exitPoint"],
        "properties": {
          "name": { "type": "string", "description": "Node name: door_origin or door_origin_N" },
          "origin": { "type": "object", "properties": { "x": {}, "y": {}, "z": {}, "h": {} } },
          "exitPoint": { "type": "object", "properties": { "x": {}, "y": {}, "z": {}, "h": {} } }
        }
      }
    },
    "npcOrigins": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["name", "x", "y", "z", "h"],
        "properties": {
          "name": { "type": "string", "description": "Anchor node name: npc_origin_N" },
          "x": { "type": "number" },
          "y": { "type": "number" },
          "z": { "type": "number" },
          "h": { "type": "number" }
        }
      }
    },
    "npcs": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["id", "name", "pos_index"],
        "properties": {
          "id": { "type": "integer" },
          "name": { "type": "string" },
          "pos_index": { "type": "integer" }
        }
      }
    },
    "stats": {
      "type": "object",
      "properties": {
        "tris": { "type": "integer" },
        "batches": { "type": "integer" },
        "textures": { "type": "integer" },
        "ttmBytes": { "type": "integer" },
        "ttcBytes": { "type": "integer" },
        "collTris": { "type": "integer" },
        "triggers": { "type": "array", "items": { "type": "string" } }
      }
    }
  }
}
```

---

## 4. Known Gaps

The baked assets represent the static visual and collision environment. The following
features are dynamic at runtime and should be implemented in future phases:

1. **Animated Props**:
   - **Pet Shop Fish**: `phase_4/models/props/interiorfish-zero` contains the static mesh.
     The swimming loop (`phase_4/models/props/interiorfish-swim` at 0.7 play rate) is an
     actor animation track.
   - **Toon HQ Periscope**: Swivels back and forth in animated state.
   - **Toon Hall SillyOMeter**: During holiday events (`SILLYMETER_HOLIDAY`), the SillyOMeter
     progresses through phases 0–15 with animated dial needles and fluid tubes
     (`tt_a_ara_ttc_sillyMeter_default.bam`). In normal non-holiday operation, it displays
     the static flat meter (`tt_m_ara_int_sillyMeterFlat.bam`).
2. **Dynamic 3D Text & UIs**:
   - **HQ Leaderboard**: In the original client, `empty_board` displays 10 rows of top toon
     scores (`DistributedHQInterior.py:61-128`). Text is rendered dynamically via TextNodes
     and updated via `setLeaderBoard(blob)` datagrams.
   - **Landmark Interior Signs**: In upstream `DistributedToonInterior.py:105-126`, signs
     are cloned from street landmark buildings. Safezone landmark signs in TTC have letter
     baselines (`Toontown Library`, `Toontown Bank`, etc.) which require font text rendering.
   - **Trophy Frames**: `trophy_origin` holds picture frames of toons who defeated cog buildings.
     Playground buildings cannot be taken over, so this node remains unused in zone 2000.
3. **NPC Interactions & UIs**:
   - Clerk inventory purchase GUI (Gag Shop).
   - Tailor DNA/clothing change interface (Clothing Shop).
   - Pet shop doodle purchase and adoption screens.
   - Bank / Toon Hall quest dialogue cinematics.
4. **Door Swinging Animation**:
   - `leftDoor` and `rightDoor` swing ±100° over 0.6 seconds when an avatar enters or exits.
     The baked `.ttm` includes the flat door frame (`door_*_flat`); the animated leaves
     can be instantiated as separate props during the door enter/exit sequence.

---

## 5. Changes Needed in Shared Modules

Per the worktree isolation rules, no shared modules in `tools/pipeline/` were modified.
The following improvements are recommended for shared modules in future updates:

1. **`tools/pipeline/dna.py`**:
   - **Catalog code indexing**: `Storage` only stores `self.categories[code] = category`
     (code-to-category reverse lookup). Upstream interior assembly relies on
     `getNumCatalogCodes(category)` and `getCatalogCode(category, index)` to deterministically
     pick rooms, textures, and furniture. `Storage` should maintain
     `self.catalog = {category: [code1, code2, ...]}` in order of appearance.
   - **`store_texture` catalog categories**: In `dna.py:210-216`, parsing of `store_texture`
     discards `vals[0]` when 3 arguments are present (`store_texture [ "TI_wallpaper" "stripeB5" "..." ]`).
     It only stores the code and texture object. Recording the catalog category is required
     for category-based texture randomizers.
   - **Sign transform lookup**: `Storage` does not implement `getSignTransformFromBlockNumber(block)`.
2. **`tools/pipeline/assemble.py`**:
   - **Interior door handling**: `assemble._setup_door_parts` is hardcoded for exterior
     doors on landmark buildings. Interior doors need slightly different logic:
     reparenting `door_*_flat` to the room root with `doorColor`, naming triggers
     `door_trigger_<block>` (or `_<block>_<index>`), and applying the specific door origin
     Z/Y offset (`-0.025` for standard, `-0.25` for Pet Shop).
3. **`tools/pipeline/ttm.py`**:
   - The returned stats dictionary uses key `'file_bytes'` rather than `'bytes'`. Standardizing
     naming across `ttm.write` and `collide.mesh.write` (which returns `'bytes'`) would make
     generic batch scripts simpler.
