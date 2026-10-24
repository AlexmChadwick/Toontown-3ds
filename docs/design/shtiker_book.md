# Shtiker Book Design & Architecture

The Shtiker Book is Toontown's primary player information and settings interface. On the Nintendo 3DS, it operates on the bottom touch screen (320x240) using citro2d, providing full implementations of all upstream Shtiker pages with real server data.

---

## 1. Overview & Entry Points

The book is managed by `tt::ui::Book` (`source/ui/book.{h,cpp}`), integrated into the core bottom-screen `Ui` (`source/render/ui.{h,cpp}`).

### Entry Points:
1. **START Button**: Pressing `KEY_START` in the game world toggles the Shtiker Book open/closed.
2. **Toon Tab**: A button labeled `[ Open Shtiker Book (START) ]` on `kTabToon` opens the book directly on touch.
3. **Autotest / Direct Navigation**: Autotest step triggers (`"book: gags"`, `"book: tasks"`, etc.) open specific pages via `ui.openBookPage(...)`.

### Navigation:
- **L / R Bumpers**: Page turning (`KEY_L` for previous page, `KEY_R` for next page).
- **D-Pad Left / Right**: `KEY_DLEFT` / `KEY_DRIGHT` for previous/next page.
- **Top Header Buttons**: `< Prev` (top-left), `Next >` (top-right), and `[X] Close` (top-far-right).
- **Bottom Tab Bar**: 9 touchable tabs across the bottom edge (height 28px, width ~35.5px each).

---

## 2. Book Pages & Features

The book implements 9 pages conforming to `tt::ui::BookPage` (`source/ui/book_pages/book_page.h`):

| Page | Class | Tab Name | Contents & Server Data Sources |
|---|---|---|---|
| **1. Gags** | `PageInventory` | Gags | Inventory carry counts, max carry, experience bars with next-gag thresholds (`kLevels`), track access, and 16-frame track training filmstrip (`setTrackProgress`). Uses `GagAtlas` for real gag icons. |
| **2. ToonTasks** | `PageQuests` | Tasks | Up to 4 active task cards using `quest_text.h` / `quest_data.h`. Displays full generated headlines, objectives, defeat counts, recovery items, destinations/NPCs, reward badges, and completion banners. |
| **3. Cog Gallery** | `PageCogs` | Cogs | 4 Cog departments x 8 cogs. 32 real rendered cog head portraits from `suit_heads.ttm` (`SuitHeadAtlas`). Displays seen/defeated counts, quotas, department tabs, and building/cog radar status (`setCogRadar`, `setBuildingRadar`). |
| **4. Disguise** | `PageDisguise` | Suits | 4 Cog suit departments (Bossbot, Lawbot, Cashbot, Sellbot). Displays suit parts collected bitmask/count (`setCogParts`), merit progress bars (`setCogMerits` vs `getNeededMerits`), cog level, and "READY FOR PROMOTION!" banner. |
| **5. Fish Album** | `PageFish` | Fish | 70 species across 14 genera from `setFishCollection`. Shows genus pagination (`<` and `>`), caught weights in lb/oz, record badges, and 7 fishing trophies from `setFishingTrophies`. |
| **6. Trophies** | `PageTrophies` | Trophy | Go-Karting trophies (33), tickets count, and karting history (`setKartingTrophies`, `setTickets`, `setKartingHistory`). Mini-Golf trophies (30), 3 cups, and history calculated deterministically via `calcGolfTrophies` and `calcGolfCups`. |
| **7. SOS Cards** | `PageSos` | SOS | SOS cards grid from `setNPCFriendsDict`. Displays 1..5 star ratings, track type badge (Toon-Up, Sound, Drop, Lure, Restock, etc.), NPC names from `sos_tables.h`, and remaining call counts. |
| **8. Options** | `PageOptions` | Option | Stereo 3D toggle, Debug HUD overlay toggle, LOD bias toggle, Music volume controls, Sound volume controls, stress toons adjustment, Quit Game button, and live District / Shard browser from `net.districts()`. |
| **9. Map** | `PageMap` | Map | Full Toontown map with player orientation arrow, zone label, "Home" estate button, and 8 playground teleport pads (`kTeleportHoods`). Maintains 100% exact touch coordinate compatibility with existing autotest scripts. |

---

## 3. Data Sources & Network Integration

### Astron Protocol Fields
Owner fields added to `tools/dcgen/dcgen.py` and regenerated in `source/net/dc_generated.h` (DC hash remains **547155168**):
- `setCogStatus`, `setCogCount`, `setCogRadar`, `setBuildingRadar`
- `setCogParts`, `setCogMerits`, `setCogTypes`, `setCogLevels`
- `setFishCollection`, `setFishingTrophies`
- `setKartingTrophies`, `setTickets`, `setKartingHistory`
- `setGolfHistory`, `setPackedGolfHoleBest`, `setGolfCourseBest`
- `setNPCFriendsDict`
- `setTrackProgress`

Unpacking logic is isolated in `source/net/book_net.h` and `source/net/client_book.cpp` via `parseOwnBookField()`, updating `OwnBookStats` on `Client::ownBook()`.

---

## 4. Assets & Offline Generators

1. **Suit Heads Atlas (`tools/pipeline/bake_suit_heads_atlas.py`)**:
   - Renders all 32 cog suit heads offscreen in Panda3D rotated 180 degrees to face the camera.
   - Bakes a 256x128 ETC1A4 atlas to `romfs/baked/suit_heads.ttm` (33,280 bytes).
   - Singleton loader: `tt::ui::SuitHeadAtlas` (`source/ui/suit_heads_atlas.{h,cpp}`).
2. **SOS Tables Generator (`tools/gen_sos_tables.py`)**:
   - Parses upstream `NPCToons.py` and `TTLocalizerEnglish.py`.
   - Generates `source/ui/sos_tables.h` containing all 41 SOS cards with names, tracks, and star tiers.
3. **Disguise Tables Generator (`tools/gen_disguise_tables.py`)**:
   - Extracts `kPartsPerSuit`, `kPartsBitmasks`, `kCogBaseLevel`, and `kAllMerits` from `CogDisguiseGlobals.py` and `SuitBattleGlobals.py`.
   - Generates `source/ui/disguise_tables.h`.

---

## 5. Performance & Hardware Budgets

- **Linear Memory**: The suit heads atlas requires 33 KB of linear memory, well within the 2 MB UI sub-budget.
- **CPU & Text Rendering**: Citro2d text rendering uses cached `C2D_TextBuf` instances. Text is parsed only when data versions or settings values change (`lastStatsVer_`, `lastBookVer_`, etc.), preventing per-frame CPU parsing penalties.
- **No Per-Frame Allocations**: All page drawing code uses stack buffers and fixed arrays.

---

## 6. Azahar Verification Guide

Run the scripted autotest pass by placing the trigger file `autotest` on the virtual SD card:
```bash
touch ~/.local/share/azahar-emu/sdmc/3ds/toontown3ds/autotest
```
The test run will produce the following screenshots in `sdmc:/3ds/toontown3ds/`:
- `shot_book_gags_bottom.bin` (Gags inventory, exp bars, track training)
- `shot_book_tasks_bottom.bin` (ToonTasks cards)
- `shot_book_cogs_bottom.bin` (Cog gallery with suit head portraits)
- `shot_book_disguise_bottom.bin` (Cog suits, parts bitmask, merits)
- `shot_book_fish_bottom.bin` (Fish album species & weight records)
- `shot_book_trophies_bottom.bin` (Karting & Golf trophies)
- `shot_book_sos_bottom.bin` (SOS friend cards with star ratings)
- `shot_book_options_bottom.bin` (Options & District selector)

Convert binary screenshots using the project's decoder:
```bash
python3 tools/dev/decode_shot.py sdmc-out/3ds/toontown3ds/shot_book_cogs_bottom.bin shot_cogs.png
```

Verify in Azahar:
1. Press **START** in-game: the red Shtiker Book opens immediately.
2. Press **L** / **R**: pages cycle smoothly.
3. Tap bottom tabs: direct navigation to each page.
4. Open Cog Gallery: verify all 32 head icons render cleanly with correct lighting.
5. Open Options: test toggling Stereo 3D, Debug overlay, and adjusting volume controls.
6. Open Map: tap a playground button or Home button; verify teleport request fires.
