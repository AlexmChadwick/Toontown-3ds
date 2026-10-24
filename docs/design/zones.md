# Zones: ids, assets, costs and an Old 3DS streaming plan

Status: design spec (research only, no code). Upstream paths are relative to
`ref/open-toontown/toontown/` unless they start with `otp/`. Asset paths are relative to
`ref/resources/`, and every path listed here was checked to exist on 2026-10-23.
Costs were measured by assembling each DNA file with `tools/pipeline/dna.py` and
`assemble.py`, then counting geoms, triangles and textures. The scratch script that did
this is not in the tree; section 6 explains how to reproduce it.

## 1. Zone id scheme

- **Hood ids** (`toonbase/ToontownGlobals.py:146-154`): DD 1000, TTC 2000, BR 3000,
  MM 4000, DG 5000, OZ (Chip 'n Dale's Acorn Acres) 6000, FunnyFarm 7000 (unused), GS 8000,
  DL 9000.
- **Street (branch) ids** (`:155-171`): the hood id + 100/200/300.
- **Hood-to-street map:** `HoodHierarchy` (`:173-179`). GS has no streets. DL has only
  9100 and 9200.
- **Cog HQs** (`:181-202`), as exterior / lobby / sub-zones:
  - Bossbot: 10000 / 10100 / 10500-10700.
  - Sellbot: 11000 / 11100 / factory exterior 11200 / factory interior 11500.
  - Cashbot: 12000 / 12100 / mints 12500-12700.
  - Lawbot: 13000 / 13100 / 13200 / 13300-13600.
- **Other zones:**
  - Tutorial 15000, MyEstate 16000, GolfZone 17000, PartyHood 18000 (`:203-206`).
  - Welcome Valley 22000-61000 (`:208-209`).
  - Dynamic zones start at 61000 (`:210`). Estates, minigames, battles and interiors that
    are allocated at runtime live there.
- `HQToSafezone` (`:217`) maps each Cog HQ to the playground the toon returns to.
- **Math** (`hood/ZoneUtil.py`):
  - `getHoodId = z - z%1000` (`:166-171`).
  - `isPlayground` means `z%1000==0` (`:63-68`).
  - `getBranchZone = z - z%100`, minus 500 if `z%1000 >= 500` (not in Cog HQs) (`:122-130`).
  - `isInterior` means `z%1000 >= 500` (`:189-197`).
  - Building interior zone = `exteriorZone - exteriorZone%100 + 500 + block`
    (`building/DistributedBuildingAI.py:197-204`). For example, block 12 on street 2100
    is interior 2612.
- **Street cells:** each street visgroup is its own **zone id** (for example `visgroup
  "2101"`, `phase_5/dna/toontown_central_2100.dna:276`). Visgroup zones are what the
  client sets its location to and takes interest in (section 4).
- **Welcome Valley** remaps zones through `getCanonicalZoneId` / `getTrueZoneId`
  (`:141-163`). A 3DS client should never request Welcome Valley, so it uses canonical
  ids only.

## 2. Asset table (hoods and streets)

Common storage files:
- Every place loads `phase_4/dna/storage.dna` (`distributed/PlayGame.py:151`).
- Then the hood storage (`Hood.py:88-89`).
- Then the safe-zone storage (`safezone/SafeZoneLoader.py:72-75`) or, for streets,
  `phase_5/dna/storage_town.dna` plus the town storage (`town/TownLoader.py:187-193`).

Street DNA is `phase_N/dna/<dnaMap name>_<branch>.dna` (`TTTownLoader.py:17` and its
siblings). Holiday storage files (`winter_*`, `halloween_props_*`) are optional and
should be skipped for v1. Every street adds the battle music
`phase_3.5/audio/bgm/encntr_general_bg.ogg` (`TownLoader.py:56`).

| Hood | Zone | Hood storage | SZ DNA + SZ storage | SZ music / activity | Streets (DNA) | Town storage | Street music | Sky |
|---|---|---|---|---|---|---|---|---|
| TTC | 2000 | `phase_4/dna/storage_TT.dna` (TTHood.py:18) | `phase_4/dna/toontown_central_sz.dna`, `storage_TT_sz.dna` (TTSafeZoneLoader.py:14-15) | `phase_4/audio/bgm/TC_nbrhood.ogg` / `phase_3.5/audio/bgm/TC_SZ_activity.ogg` (:12-13) | 2100 Silly St, 2200 Loopy Ln, 2300 Punchline Pl: `phase_5/dna/toontown_central_2x00.dna` | `phase_5/dna/storage_TT_town.dna` (TTTownLoader.py:12) | `phase_3.5/audio/bgm/TC_SZ.ogg` (:10) | `phase_3.5/models/props/TT_sky` (TTHood.py:23) |
| DD | 1000 | `phase_6/dna/storage_DD.dna` (DDHood.py:14) | `phase_6/dna/donalds_dock_sz.dna`, `storage_DD_sz.dna` (DDSafeZoneLoader.py:16-17) | `phase_6/audio/bgm/DD_nbrhood.ogg` / `DD_SZ_activity.ogg` (:14-15) | 1100, 1200, 1300: `phase_6/dna/donalds_dock_1x00.dna` | `phase_6/dna/storage_DD_town.dna` | `phase_6/audio/bgm/DD_SZ.ogg` | `BR_sky` (DDHood.py:19) |
| MM | 4000 | `phase_6/dna/storage_MM.dna` (MMHood.py:14) | `minnies_melody_land_sz.dna`, `storage_MM_sz.dna` (MMSafeZoneLoader.py:13-14) | `phase_6/audio/bgm/MM_nbrhood.ogg` / `MM_SZ_activity.ogg` | 4100, 4200, 4300: `phase_6/dna/minnies_melody_land_4x00.dna` | `phase_6/dna/storage_MM_town.dna` | `phase_6/audio/bgm/MM_SZ.ogg` | `phase_6/models/props/MM_sky` (MMHood.py:19) |
| DG | 5000 | `phase_8/dna/storage_DG.dna` (DGHood.py:15) | `daisys_garden_sz.dna`, `storage_DG_sz.dna` (DGSafeZoneLoader.py:12-13) | `phase_8/audio/bgm/DG_nbrhood.ogg` / `DG_SZ.ogg` | 5100, 5200, 5300: `phase_8/dna/daisys_garden_5x00.dna` | `phase_8/dna/storage_DG_town.dna` | `phase_8/audio/bgm/DG_SZ.ogg` | `TT_sky` (DGHood.py:20) |
| BR | 3000 | `phase_8/dna/storage_BR.dna` (BRHood.py:14) | `the_burrrgh_sz.dna`, `storage_BR_sz.dna` (BRSafeZoneLoader.py:15-16) | `phase_8/audio/bgm/TB_nbrhood.ogg` / `TB_SZ_activity.ogg` | 3100, 3200, 3300: `phase_8/dna/the_burrrgh_3x00.dna` | `phase_8/dna/storage_BR_town.dna` | `phase_8/audio/bgm/TB_SZ.ogg` | `BR_sky` (BRHood.py:19) |
| DL | 9000 | `phase_8/dna/storage_DL.dna` (DLHood.py:14) | `donalds_dreamland_sz.dna`, `storage_DL_sz.dna` (DLSafeZoneLoader.py:12-13) | `phase_8/audio/bgm/DL_nbrhood.ogg` / `DL_SZ_activity.ogg` | 9100, 9200: `phase_8/dna/donalds_dreamland_9x00.dna` | `phase_8/dna/storage_DL_town.dna` | `phase_8/audio/bgm/DL_SZ.ogg` | `phase_8/models/props/DL_sky` (DLHood.py:19) |
| GS | 8000 | `phase_6/dna/storage_GS.dna` (GSHood.py:14) | `goofy_speedway_sz.dna`, `storage_GS_sz.dna` (GSSafeZoneLoader.py:21-22) | `phase_6/audio/bgm/GS_SZ.ogg` / `GS_KartShop.ogg` | none | none | none | `TT_sky` |
| OZ | 6000 | `phase_6/dna/storage_OZ.dna` (OZHood.py:14) | `outdoor_zone_sz.dna`, `storage_OZ_sz.dna` (OZSafeZoneLoader.py:28-29) | `phase_6/audio/bgm/OZ_SZ.ogg` / `GS_KartShop.ogg` | none | none | none | `TT_sky` |
| GZ | 17000 | `phase_6/dna/storage_GZ.dna` (GZHood.py:14) | `golf_zone_sz.dna`, `storage_GZ_sz.dna` (GZSafeZoneLoader.py:22-23) | `phase_6/audio/bgm/GZ_SZ.ogg` / `GS_KartShop.ogg` | none | none | none | `TT_sky` |
| Tutorial | 15000 | none (TutorialHood.py:14) | `phase_3.5/dna/tutorial_street.dna` (TutorialTownLoader.py:17) | none | none | none | none | `TT_sky` |
| Estate | 16000 | `phase_5.5/dna/storage_estate.dna` (EstateHood.py:29) | `phase_5.5/dna/estate_1.dna` (estate/EstateLoader.py:29) | `TC_nbrhood.ogg` (:27) | none | none | none | `TT_sky` |

Notes on the table:
- The playground base terrain comes from the SZ storage `place_model`, for example
  `phase_4/models/neighborhoods/toontown_central`. MM and DG also have `*_ext` (a 2-4 tri
  backdrop). OZ uses `phase_6/models/golf/golf_outdoor_zone`; GS uses
  `phase_6/models/karting/GasolineAlley_TT`.
- **Cog HQs do not assemble DNA on the client.** They load one `.bam`
  (`coghq/*CogHQLoader.py`):

  | HQ | Exterior model | Lobby model |
  |---|---|---|
  | Sellbot | `phase_9/models/cogHQ/SellbotHQExterior` | `SellbotHQLobby` |
  | Cashbot | `phase_10/models/cogHQ/CashBotShippingStation` | `VaultLobby` |
  | Lawbot | `phase_11/models/lawbotHQ/LawbotPlaza` | `LB_CH_Lobby` |
  | Bossbot | `phase_12/models/bossbotHQ/CogGolfHub` | `CogGolfCourtyard` |

  - Sky: `phase_9/models/cogHQ/cog_sky` (SellbotHQ.py:12).
  - Music: `phase_9/audio/bgm/encntr_suit_HQ_nbrhood.ogg`, `phase_11/audio/bgm/LB_courtyard.ogg`,
    and `phase_12/audio/bgm/Bossbot_Entry_v1..3.ogg`.
- **Music cost.** Each BGM `.ogg` is 0.5-1.45 MB. Streaming IMA-ADPCM needs no residency
  beyond a ring buffer. Per hood, bake 3 tracks: neighborhood, activity and street.

## 3. Measured costs

How to read the columns:
- **Geoms** is the number of draws before any merge.
- **Geom KB** is the `.ttm` layout: 16 B/vertex + 6 B/tri.
- **Tex 16bpp** clamps textures to 128 px at 16 bpp with mips. **Tex ETC1** is 4 bpp
  (8 bpp with alpha) with mips, clamped to 128 px.
- **Raw** is the source pixels.

### Base `.bam` and sky models (`inspect_bam.py`)

| Model | Geoms | Tris | Textures (raw KB) |
|---|---|---|---|
| toontown_central | 52 | 1205 | 22 (1884) |
| donalds_dock | 182 | 1966 | 22 (2250) |
| minnies_melody_land | 130 | 1188 | 12 (1168) |
| daisys_garden | 90 | 1604 | 12 (733) |
| the_burrrgh | 29 | 644 | 8 (471) |
| donalds_dreamland | 130 | 1256 | 14 (1909) |
| GasolineAlley_TT | 163 | 1208 | 39 (3966) |
| TT_sky | 14 | 42 | 3 (304) |
| BR / MM / DL sky | 7 | 18 | 2 |
| cog_sky | 18 | 76 | 4 (832) |
| SellbotHQExterior | 233 | 4684 | 23 |
| CashBotShippingStation | 1007 | 7887 | 39 |
| LawbotPlaza | 277 | 7780 | 26 |
| CogGolfHub | 469 | 5425 | 17 |

### Assembled playgrounds (whole zone; it is a single visgroup)

| SZ | Geoms | Tris | Geom KB | Textures | Raw MB | Tex 16bpp KB | Tex ETC1 KB |
|---|---|---|---|---|---|---|---|
| TTC | 843 | 13203 | 534 | 114 | 13.6 | 3434 | 1267 |
| DD | 892 | 11207 | 462 | 97 | 11.7 | 3055 | 1117 |
| MM | 778 | 10986 | 546 | 86 | 10.6 | 2730 | 1015 |
| DG | 983 | 11754 | 517 | 93 | 11.2 | 2858 | 1099 |
| BR | 756 | 9654 | 405 | 84 | 10.7 | 2594 | 973 |
| DL | 765 | 9935 | 421 | 84 | 10.6 | 2589 | 957 |
| GS | 630 | 6390 | 182 | 60 | 6.3 | 1677 | 650 |
| OZ | 393 | 5337 | 141 | 27 | 3.3 | 896 | 317 |
| GZ | 563 | 10377 | 237 | 26 | 2.9 | 811 | 259 |
| Tutorial | 612 | 3790 | 149 | 41 | 4.1 | 1209 | 454 |
| Estate (no houses) | 171 | 2337 | 58 | 21 | 3.1 | 709 | 265 |

Where the TTC triangles go: flat buildings 4464, props 1506, Toon HQ 1248, base terrain
1205, pet shop 980, party gate 900, gag shop 853, gazebo 465. One LODNode adds only 605
lower-LOD tris. 12 codes did not resolve (holiday/party props), so the counts are
slightly low.

### Assembled streets

The "vis" columns are for the set of visgroups drawn from one cell (the cell plus its
`vis` list), shown as average / max over all cells in that street.

| Street | Cells | Whole tris | Whole geom KB | Whole tex 16bpp / ETC1 KB | Vis cells | Vis geoms | Vis tris | Vis geom KB | Vis tex 16bpp KB |
|---|---|---|---|---|---|---|---|---|---|
| 2100 | 56 | 55087 | 1503 | 2715 / 993 | 13 / 19 | 839 / 1308 | 12446 / 22325 | 341 / 622 | 1763 / 2275 |
| 2200 | 36 | 42338 | 1232 | 2802 / 1050 | 10 / 15 | 1040 / 1518 | 12699 / 17913 | 374 / 546 | 1960 / 2286 |
| 2300 | 37 | 43214 | 1301 | 2771 / 1040 | 11 / 16 | 1059 / 1831 | 12172 / 20132 | 366 / 659 | 1939 / 2290 |
| 1100 | 29 | 37393 | 928 | 2019 / 734 | 10 / 13 | 738 / 1108 | 12974 / 18649 | 325 / 482 | 1579 / 1913 |
| 1200 | 35 | 42190 | 1048 | 1934 / 708 | 9 / 14 | 637 / 1036 | 11256 / 19222 | 281 / 485 | 1499 / 1731 |
| 1300 | 30 | 44671 | 1110 | 2046 / 746 | 9 / 13 | 858 / 1413 | 13189 / 22236 | 334 / 554 | 1534 / 1758 |
| 4100 | 48 | 62324 | 2073 | 1833 / 672 | 11 / 15 | 642 / 869 | 14163 / 22512 | 472 / 743 | 1305 / 1457 |
| 4200 | 40 | 50242 | 1717 | 1811 / 661 | 13 / 20 | 766 / 1269 | 15563 / 25405 | 535 / 871 | 1392 / 1577 |
| 4300 | 43 | 62464 | 2169 | 1982 / 736 | 10 / 16 | 724 / 1166 | 14471 / 24426 | 479 / 814 | 1357 / 1587 |
| 5100 | 38 | 39924 | 1289 | 1880 / 715 | 12 / 15 | 1057 / 1420 | 11914 / 15504 | 382 / 504 | 1443 / 1528 |
| 5200 | 45 | 42040 | 1435 | 1859 / 704 | 12 / 19 | 992 / 1663 | 11468 / 20552 | 387 / 676 | 1476 / 1618 |
| 5300 | 22 | 27342 | 839 | 2109 / 787 | 7 / 11 | 898 / 1399 | 10387 / 16810 | 319 / 506 | 1444 / 1840 |
| 3100 | 36 | 51280 | 1734 | 1875 / 704 | 10 / 16 | 759 / 1188 | 14216 / 24053 | 479 / 794 | 1445 / 1587 |
| 3200 | 36 | 45091 | 1491 | 1833 / 682 | 11 / 19 | 782 / 1252 | 14276 / 23495 | 476 / 826 | 1503 / 1609 |
| 3300 | 30 | 33696 | 1245 | 2054 / 732 | 9 / 15 | 769 / 1212 | 10247 / 14981 | 380 / 538 | 1488 / 1883 |
| 9100 | 53 | 50261 | 1366 | 1878 / 706 | 10 / 16 | 607 / 1011 | 9841 / 17338 | 265 / 462 | 1312 / 1494 |
| 9200 | 55 | 81067 | 2034 | 2257 / 822 | 9 / 13 | 735 / 1299 | 13785 / 21914 | 342 / 550 | 1441 / 1750 |

### What the numbers say

1. **Memory is not the constraint.**
   - The largest whole street is 2.2 MB of geometry and 2.8 MB of textures at 16 bpp.
     The largest playground is 0.55 MB geometry and 3.4 MB textures at 16 bpp (1.3 MB
     with ETC1).
   - Both fit the 6 MB geometry and 8 MB texture sub-budgets with room to spare, even
     before ETC1. That leaves room to keep the **adjacent zone's** textures resident.
2. **Draw calls and triangles are the constraint.**
   - Raw playgrounds have 400-1000 geoms and streets 600-1800 visible geoms, against
     the ≤150 draw-call envelope.
   - A street vis set averages 10-15k triangles and peaks at 25k, against the ≤15k
     envelope, and that is *before* toons and suits.
   - The baker must therefore merge per texture (atlas) **inside each visgroup**, which
     brings 50-75 textures down to about 10-20 batches per cell.
   - The runtime must frustum-cull per visgroup and per playground grid cell, plus a
     far-plane or fog cut. The vis list has no direction, so frustum culling alone should
     remove about half.
3. **Atlas sharing.** Streets reuse about 60 textures, mostly from the shared
   `storage_town.dna`. A per-street atlas set shared by all cells beats per-cell atlases,
   because there are no duplicate texture copies across cells.

## 4. Runtime zone protocol on streets (what streaming must drive)

- **Floor polygons carry the cell id.** Floor collision polys (into-mask bit 1) in each
  visgroup are renamed to the visgroup zone id (`town/TownLoader.py:202`, `:280` and
  `town/Street.py:294-303`).
- **The camera's floor ray reports the cell.** It fires `on-floor`
  (`otp/avatar/LocalAvatar.py:252-263`). `BattlePlace.enterZone` parses the zone id from
  the collided node's name (`battle/BattlePlace.py:84-94`), then
  `Street.doEnterZone` (`town/Street.py:321-360`):
  - fades groups out and in over 0.5 s (`TownLoader.py:259-261`);
  - stashes groups that are not visible (`Street.py:305-315`);
  - under Astron, sends `sendSetZoneMsg(newZone, [branchZone] + visZones + [newZone])`
    (`Street.py:353-360`).
- **What `sendSetZoneMsg` sends** (`distributed/ToontownClientRepository.py:1012-1021`):
  - `sendSetLocation(avId, shard, newZone)`, then
  - one queued **SetInterest** (an alter-interest on the single "OldSetZoneEmulator"
    handle) over the whole zone list.
- **The baked street format must therefore carry:**
  - per-cell floor collision tagged with the zone id (u16);
  - each cell's vis list as zone ids;
  - the branch zone.
- The client keeps doing the same set-location plus interest alteration on every cell
  change. Without it, other toons and suits in neighbouring cells never generate.
- Playgrounds need none of this: one zone, one interest.

## 5. Streaming plan for Old 3DS

**Units.**
- A *playground* is one `.tz` with uniform grid cells (about 40 × 40 units) for culling.
- A *street* is one `.tz` with one chunk per visgroup, plus a shared texture table.
- Interiors (`zone%1000 >= 500`) are separate small `.tz` files, specified in
  `playground.md`.

**Residency rule.** Load the **whole** current zone file at the zone transition, behind
the quiet-zone or loading screen. Upper bounds per zone:

| Content | 16 bpp | ETC1 |
|---|---|---|
| Geometry | ≤ 2.2 MB | ≤ 2.2 MB |
| Textures | ≤ 3.4 MB | ≤ 1.3 MB |

- There is no intra-street streaming. The `vis` lists are used **only** for draw
  selection and interest.
- Async prefetch of the destination's textures while the toon walks into a tunnel is
  optional (`linktunnel_*` nodes in DNA mark the tunnels).
- Free the previous zone after the new one is up. Peak with both resident is about
  2 × 5.6 MB at 16 bpp. That exceeds the 8 MB texture budget, so
  **at 16 bpp, free first then load. With ETC1, double-buffering fits.**

**Per hood, Old 3DS (16 bpp until ETC1 lands):**

| Hood | Playground resident (geom + tex) | Worst street resident | Notes |
|---|---|---|---|
| TTC 2000 | 0.5 + 3.4 MB | 2100: 1.5 + 2.7 MB | Do this first; the playground is already partly baked |
| DD 1000 | 0.5 + 3.1 | 1300: 1.1 + 2.0 | The playground has water and a boat (DistributedBoat), so stub the animation |
| MM 4000 | 0.5 + 2.7 | 4300: 2.2 + 2.0 | Densest geometry (vertex-heavy). Trim the vis set by frustum |
| DG 5000 | 0.5 + 2.9 | 5200: 1.4 + 1.9 | DistributedDGFlower can be ignored |
| BR 3000 | 0.4 + 2.6 | 3100: 1.7 + 1.9 | Snow props; 11-13 codes do not resolve yet |
| DL 9000 | 0.4 + 2.6 | 9200: 2.0 + 2.3 | 9200 has the most triangles (81k whole), so it needs LOD and a far cut |
| GS / OZ / GZ | ≤ 0.2 + 1.7 | none | Low value without karting or golf |
| Cog HQ exteriors | 7.9k tris and 1007 geoms (Cashbot) | none | Needs merging. Post-v1 |

**Draw-time policy per street cell:**
1. Take the visible set = the current cell's vis list.
2. Reject cells whose AABB is outside the frustum.
3. Reject cells whose nearest point is more than about 150 units away (fog hides the pop).
4. Draw batches, at a budget of ≤ 9k world tris. That leaves about 6k for toons and suits.
5. If the frame is over budget, drop the farthest cells first.
6. Upstream's 0.5 s fade is optional. A hard pop behind fog is acceptable.

## 6. Baker work items and effort

Reproducing the measurements:
- Load storages in order: `storage.dna`, hood storage, then SZ storage, or
  `storage_town.dna` plus town storage.
- Call `dna.parse_file`, then `assemble.assemble`.
- Group by the `dna_visgroup` tag (`assemble.py:28,110`) and union over each cell's
  `vis` list.

Missing pieces:
- **Per-visgroup chunking.** Tag floor collision with the zone id.
- **Per-texture batch merge and atlas** inside each chunk.
- **ETC1** (already a Phase 2 follow-up).
- **Suit paths** (`suit_edge`, `store_suit_point`). Not needed by the client; the AI owns
  them.
- **Battle cells** (`battle_cell`, currently ignored at `assemble.py:49`). Needed only
  for combat camera placement; see `combat.md`.

Effort estimates:

| Work | Dev-days |
|---|---|
| Street streaming, first street (2100), end to end: per-visgroup baker and `.tz` street layout (2-3), runtime cell detection from tagged floor polys plus vis-set draw selection and frustum/distance culling (2), set-location plus interest alteration on cell change (1, reuses the Phase 3 interest code), measurement and tuning to 30 fps (1-2) | 6-8 |
| Each additional street in a hood that is already baked | 0.5 (bake, fix unresolved codes, measure) |
| Each additional playground and hood: SZ bake (0.5-1), hood-specific props, water, boat or animation stubs (0.5-1), music bake (0.25), test-log entry (0.25) | 1.5-2.5 |
| Each additional hood's three streets | about 1.5 |
| Cog HQ exterior (single `.bam` path, merging needed) | about 1 each, post-v1 |
