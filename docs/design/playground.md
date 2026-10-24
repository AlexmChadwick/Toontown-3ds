# Playground features beyond walking (TTC, zone 2000)

Status: design spec for Phase 4+. Research only, nothing here is implemented yet.

Paths are relative to `ref/open-toontown/`, unless they start with `phase_`, which
means `ref/resources/`. `toon.dc` and `otp.dc` are `etc/toon.dc` and `etc/otp.dc`.
Server behaviour is described as it is in this repo's AI, which has stubs in places
(see §5).

## 0. What the AI puts in zone 2000

`TTHoodDataAI.startup` (`toontown/hood/TTHoodDataAI.py:20-36`) runs `HoodDataAI.startup`
(`toontown/hood/HoodDataAI.py:28-32`). Together they create the following objects.
**Every class below arrives in `ENTER_OBJECT_REQUIRED_OTHER` once we have interest in
2000.** The client must at least be able to skip each one with the generated field-skip
table.

| Class (dc line) | Count in 2000 | Source |
|---|---|---|
| `DistributedTrolley` (toon.dc:700) | 1 | TTHoodDataAI.py:22-26 |
| `DistributedMickey` : `DistributedCCharBase` (toon.dc:636, 624) | 1 | TTHoodDataAI.py:29-32 |
| `DistributedButterfly` (toon.dc:939) | 4 areas × 6 = 24 | HoodDataAI.py:126-133, `safezone/ButterflyGlobals.py:9-10,16` |
| `DistributedTTTreasure` (toon.dc:1531 → `DistributedTreasure` 1515) | ≤ 5, one spawns every 20 s | `safezone/TTTreasurePlannerAI.py:8` |
| `DistributedFishingPond` (toon.dc:959) | 1 | HoodDataAI.py:79-92, `ai/ToontownAIRepository.py:383-403` |
| `DistributedNPCFisherman` (toon.dc:1877) | 1 (npc 2012, posIndex 0) | HoodDataAI.py:93, `toon/NPCToons.py:440` |
| `DistributedNPCPartyPerson` (toon.dc:1882) | 2 (npcs 2016, 2017, posIndex 1, 2) | `toon/NPCToons.py:516,535,155-164` |
| `DistributedBuilding` (toon.dc:1597) | 5 (toon blocks 13, 14, 16, 18, 21) | `building/DistributedBuildingMgrAI.py:124-148` |
| `DistributedDoor` (toon.dc:1803), EXT side | 9: 5 toon blocks, gag shop, pet shop, 2 for the HQ | `building/DistributedBuildingAI.py:421-438`, `building/GagshopBuildingAI.py:31-38`, HQBuildingAI.py:35-38, PetshopBuildingAI.py:35 |
| `DistributedKnockKnockDoor` : `DistributedAnimatedProp` (toon.dc:1903, otp.dc:431) | 5 | `building/DistributedBuildingAI.py:431-432` |

No `DistributedSuitPlanner` is created for 2000, because the playground row has flag 0 in
`ai/ToontownAIRepository.py:261-264` (see HoodDataAI.py:115-124). Fishing spots and
party hats are **not** created: `findFishingSpots` and `findPartyHats` return `[]  # TODO`
(`ai/ToontownAIRepository.py:405-409`).

Interior zones are `branch - branch%100 + 500 + block`
(`building/DistributedBuildingMgrAI.py:199`), which gives **2513–2522** for the TTC
playground. The DNA blocks are `szNN:` in `phase_4/dna/toontown_central_sz.dna`:

| Block | Building | building_type |
|---|---|---|
| 13 | Toon Hall | standard |
| 14 | Bank | standard |
| 16 | School | standard |
| 18 | Library | standard |
| 19 | Gag Shop | `gagshop` |
| 20 | Toon HQ | `hq` |
| 21 | Clothing | `clotheshop`, built as a normal toon building |
| 22 | Pet Shop | `petshop` |

DNA lines: 530, 607, 644, 736, 794, 995, 1090, 1318.

## 1. NPC toons

**Protocol.** `DistributedNPCToonBase : DistributedNode` (toon.dc:1831-1839):

- `setName(string)`, `setDNAString(blob)` and `setPositionIndex(uint8)` are all
  `required broadcast ram`.
- `setAnimState(string, int16/1000, int16)` is `broadcast ram`.
- `setPageNumber(int16, int8, int16)` is `broadcast ram clsend`.
- `avatarEnter()` is `airecv clsend`.
- `freeAvatar()` is AI → client.
- The `DistributedNode` fields `setX` through `setR` and `setParent` (otp.dc:132-148) can
  also arrive as `ram` fields in the OTHER generate. Ignore them, because NPC placement
  comes from `posIndex` (below).

Subclasses add their own `setMovie` signatures:

| Class (toon.dc line) | `setMovie` args | Client → AI fields |
|---|---|---|
| `DistributedNPCToon` (1841) | `(uint8 mode, uint32 npcId, uint32 avId, uint16[] quests, int16 ts)` | `setMovieDone`, `chooseQuest(uint16)`, `chooseTrack(int8)` |
| Clerk (1862) | `(uint8, uint32, uint32, int16)` | `setInventory(blob, int16, uint8)` |
| Tailor (1867) | `(uint8, uint32, uint32, int16)` | `setDNA(blob, int8, uint8)` |
| Fisherman (1877) | `(uint8, uint32, uint32, uint32[], int16)` | `completeSale(uint8)` |
| PartyPerson (1882) | `(uint8, uint32, uint32, uint32[], int16)` | `answer(uint8)` |

Mode constants are in `toon/NPCToons.py:9-36`:
- `QUEST_MOVIE_*` 0–11
- `PURCHASE_MOVIE_*`
- `SELL_MOVIE_*`

**Render.** An NPC is an ordinary toon built from the 15-byte DNA blob. The AI builds it
in `toon/NPCToons.py:126-140`: `newToonFromProperties`, then `makeNetString`, and it
sends `setAnimState('neutral', 1.0)` at line 147. **We reuse the player toon pipeline
unchanged.**

Placement:
- Standard NPCs reparent to the DNA/interior node `npc_origin_<posIndex>`
  (`toon/DistributedNPCToonBase.py:70-78`).
- The fisherman uses `npc_fisherman_origin_<posIndex>`
  (`toon/DistributedNPCFisherman.py:47-55`). In TTC that is
  `toontown_central_sz.dna:10-14`, pos (-68, -2, -1.975), h 27.
- Party people use `party_person_left` (even posIndex) or `party_person_right` (odd), found inside the party-gate model
  (`toon/DistributedNPCPartyPerson.py:55-62`).
- NPCs are not smoothed (`wantsSmoothing` returns 0, DistributedNPCToonBase.py:83).

Origins measured in the interior models (`tools/pipeline` probe):
- `gagShop_interior.bam`: npc_origin_0 at (-6, 21.5, 0) and npc_origin_1 at (6, 21.5, 0).
- `HQ_interior.bam`: npc_origin_0 through npc_origin_3 at z = -5.
- `toon_interior.bam`: npc_origin_0 at (0, 21, 0).
- `PetShopInterior.bam`: npc_origin_0 through npc_origin_2.

The baker must export these as named points in the `.tz`.

NPCs in TTC (`toon/NPCToons.py:250-592`, names at `toonbase/TTLocalizerEnglish.py:6954-6973`):

| Zone | NPCs |
|---|---|
| 2000 | 2012 Fisherman Freddy, 2016 and 2017 party planners |
| 2514 | 2002 Banker Bob |
| 2516 | 2003 Professor Pete |
| 2518 | 2005 Librarian Larry |
| 2519 (gag shop) | 2006 and 2011 clerks |
| 2520 (HQ) | 2007–2010, type `NPC_HQ` |
| 2521 (clothing) | 2004 Tammy, the tailor |
| 2522 (pet shop) | 2013–2015 pet clerks |
| 2513 (Toon Hall) | 2018–2020 scientists |

**Interaction trigger.**
- The trigger is an intangible `CollisionTube` from (0, 1, 0) to (0, 1, 5), radius
  **3.25**, on `WallBitmask` (DistributedNPCToonBase.py:92-102).
- On enter, the client:
  - puts its place FSM into `quest` (or `stopped`),
  - sends `avatarEnter()` (`toon/DistributedNPCToon.py:64-67`),
  - turns both avatars to face each other (DistributedNPCToonBase.py:113-120).
- The AI answers with `setMovie` to the whole zone. For example `DistributedNPCToonAI`
  does this with a 5.5 s auto-clear or a 60 s timeout (`toon/DistributedNPCToonAI.py:136-214`),
  and party people use 30 s (`toon/DistributedNPCPartyPersonAI.py:21-47`).
- When it is done, the client sends `setMovieDone()`, returns the camera and goes back
  to `walk` (DistributedNPCToon.py:83-97).
- On `QUEST_MOVIE_TIMEOUT` or `freeAvatar`, it just frees the local toon
  (DistributedNPCToon.py:118-126, DistributedNPCToonBase.py:130-132).

**Minimal 3DS behaviour (Phase 4).**
1. Render NPCs at their origins in neutral, with nametags.
2. On touch or A inside the radius, send `avatarEnter`.
3. Show `setMovie` results as a speech bubble with fixed per-mode text.
4. Send `setMovieDone` after 3 s, or when the player presses A.

Quest choice, shopping and tailoring are out of scope here: clerks belong to
`combat.md` (inventory) and the tailor to `ui.md`. Blockers and HQ quest movies use the
same path.

## 2. Doors → interiors

**DC.** `DistributedDoor` (toon.dc:1803-1816):

| Field | Type | Keywords |
|---|---|---|
| `setZoneIdAndBlock` | `(uint32, uint32)` | required broadcast ram |
| `setSwing` | `(int8)` | required broadcast ram |
| `setDoorType` | `(uint8)` | required broadcast ram |
| `setDoorIndex` | `(uint8)` | required broadcast ram |
| `setOtherZoneIdAndDoId` | `(uint32 zone, uint32 doId)` | AI → one client |
| `requestEnter()` | | airecv clsend |
| `requestExit()` | | airecv clsend |
| `rejectEnter` | `(int8 reason)` | AI → one client |
| `avatarEnter` | `(uint32)` | broadcast |
| `avatarExit` | `(uint32)` | broadcast |
| `setState` | `(string, int16 ts)` | required broadcast ram |
| `setExitDoorState` | `(string, int16 ts)` | required broadcast ram |

Door types are in `building/DoorTypes.py:1-11`:
- `EXT_STANDARD` = 1, `INT_STANDARD` = 2
- `EXT_HQ` = 3, `INT_HQ` = 4 (HQ doors use `doorIndex` 0 and 1)

The interiors are generated in the interior zone. Each has `setZoneIdAndBlock(uint32, uint16)`:
- `DistributedToonInterior` (toon.dc:1614) also has `setSavedBy` and `setState`.
- `DistributedHQInterior` (toon.dc:1785) also has `setLeaderBoard(blob)` and
  `setTutorial(uint8)`.
- `DistributedGagshopInterior` (toon.dc:1791) and `DistributedPetshopInterior`
  (toon.dc:1795) have only `setZoneIdAndBlock`.

**Door FSMs.** The client mirrors two AI FSMs, the enter side (`fsm`) and the exit side
(`exitDoorFSM`). Each has the states off, closing, closed, opening and open
(`building/DistributedDoor.py:46-62`). The AI timings (`building/DistributedDoorAI.py:171-212`):
- `opening` lasts 1 s, then `open`.
- `open` lasts 1 s, then `closing`.
- `closing` lasts 1 s, then `closed`.

On the client:
- The right door leaf swings ±100° over 0.6 s after a 0.4 s delay. The sign of the swing
  comes from `setSwing` (DistributedDoor.py:565-585).
- It closes over 1.0 s (DistributedDoor.py:531-554).
- SFX: `phase_3.5/audio/sfx/Door_Open_1.ogg` and `Door_Close_1.ogg` (DistributedDoor.py:44-45).

**Sequence for entering a building:**
1. Client: the local toon hits trigger `door_trigger_<block>` (or `_<block>_<doorIndex>`
   for HQ doors, DistributedDoor.py:136-140). The client checks the facing dot product
   (`isDoorHit`, :298-302), then sends **`requestEnter()`** and puts the place into
   `stopped` (:304-311).
2. AI: checks the lock and access. On failure it sends `rejectEnter(reason)` and the
   client returns to walk (:374-384). On success it broadcasts `avatarEnter(avId)`, sends
   **`setOtherZoneIdAndDoId(interiorZone, insideDoorDoId)`** to us, and moves the FSM to
   `opening` (`DistributedDoorAI.py:113-137`).
3. Client, on `avatarEnter`: lines the toon up in a queue at (1.5, -5 - 2n, 0.025)
   relative to `door_origin`, over 0.5 s (:274-289, :365-372).
4. Client, on `setState('open')`: plays the enter track. The toon walks to (1.5, 2, 0)
   over 1 s, the camera goes to (0, -8, h), and at 0.5 s the screen irises out and the
   client sends `b_setParent(SPHidden=1)` (:255-272, :590-601; `otp/otpbase/OTPGlobals.py:27-28`).
5. Client, on `setState('closing')`: if `done` is set, emits `doorDoneEvent` with
   `{loader, where:'toonInterior', how:'doorIn', zoneId: otherZoneId, doorDoId: otherDoId}`
   (:518-554). This starts the zone change in §3.
6. Client, after the new zone is set: `enterDoorIn` finds the inside door by `doorDoId`
   and calls `readyToExit()`, which sends **`requestExit()`** (`hood/Place.py:563-566`,
   DistributedDoor.py:251-253).
7. AI: broadcasts `avatarExit(avId)` and opens the exit FSM (DistributedDoorAI.py:139-150).
8. Client, on `avatarExit`: plays the exit track. The toon is placed at (-1.5, 0) facing
   179° relative to the door origin, `setParent(SPRender=2)` is sent, and the toon walks
   to (-1.5, -6) relative to the origin. After that come the iris in and `walk`
   (:441-484).

**Assets.** The interior models are measured with `inspect_bam.py`:

| Model | Tris | Geoms | Textures | Raw size |
|---|---|---|---|---|
| `phase_4/models/modules/gagShop_interior.bam` | 1083 | 85 | 13 | 1.5 MB |
| `phase_3.5/models/modules/HQ_interior.bam` | 3144 | 195 | 20 | 2.8 MB |
| `phase_3.5/models/modules/toon_interior.bam` (TI_room; also `_2`, `_L`, `_T`) | 250 | 8 | 5 | 0.8 MB |
| `phase_4/models/modules/PetShopInterior.bam` | 1519 | 167 | 21 | 3.7 MB |

- The toon interior is chosen by `random.Random(zoneId)` from `TI_room*` in
  `phase_3.5/dna/storage_interior.dna:4-13` (`building/DistributedToonInterior.py:83-101`).
- Gag shop, pet shop and HQ furniture and colours are also seeded by `zoneId`
  (DistributedGagshopInterior.py:24-90). Because the seed is just the zone id, **the
  baker can reproduce each interior offline** by running the same Python RNG, and emit
  one `.tz` per interior zone (2513–2522).
- Door origins: `door_origin` exists in every interior. `hqTT.bam` has `door_origin_0/1`
  and `door_trigger_0/1`. Standard exterior door triggers are made by DNA `door` nodes
  (DNADoor in libtoontown), so **the DNA baker must emit a trigger volume plus an origin
  per (block, doorIndex).**
- HQ_interior's 195 geoms need batching to fit the 150-draw budget.

## 3. Zone change: tunnels, doors, QuietZone, interest

There is one emulated "set zone" interest handle. `sendSetZoneMsg(zoneId, visList=None)`
(`toontown/distributed/ToontownClientRepository.py:1012-1021`) does two things:
1. sends `CLIENT_OBJECT_LOCATION(140)`: `u32 avId, u32 shardId, u32 zoneId` (otp
   `OTPClientRepository.py:2419-2425`);
2. adds or alters interest on **the same handle** (:1036-1063).

Interest wire format (`direct/distributed/DoInterestManager.py:505-523`):
- `CLIENT_ADD_INTEREST(200)`: `u32 ctx, u16 handle, u32 parent, u32 zone`.
- `CLIENT_ADD_INTEREST_MULTIPLE(201)`: `…, u16 n, u32 zones[n]`.
- An alter is the same message with the same handle.
- The client waits for `CLIENT_DONE_INTEREST_RESP(204)`.

The UberZone (2) interest is separate and persists.

**The QuietZone FSM** (`hood/QuietZoneState.py:21-26`) runs these states in order:
`waitForQuietZoneResponse` → `waitForZoneRedirect` → `waitForSetZoneResponse` →
`waitForSetZoneComplete` → `waitForLocalAvatarOnShard`.

| # | Step | Detail |
|---|---|---|
| 1 | Leave the old place | Tunnel-out, door or trolley movie, then iris out. |
| 2 | `sendQuietZoneRequest()` | `sendSetZoneMsg(QuietZone=1, [])` (:1095-1097, `OTPGlobals.py:3`). While in the quiet zone, only `neverDisable` generates are processed (QuietZoneState.py:144-190). Wait for interest done. |
| 3 | `waitForZoneRedirect` | Only Welcome Valley redirects (QuietZoneState.py:235-264). We skip this and go straight to step 4. |
| 4 | `enterWaitForSetZoneResponse` | **Load the new zone's geometry first.** `Hood.handleWaitForSetZoneResponse` runs synchronously (`hood/Hood.py:171-192`). Then delete all objects not in the UberZone (`dumpAllSubShardObjects`, QuietZoneState.py:286-287). Then `sendSetZoneMsg(zoneId)` for a playground or interior, or `sendSetZoneMsg(zoneId, [branchZone] + visgroup vis list + [zoneId])` for streets (:289-307, :312-318). |
| 5 | `waitForSetZoneComplete` | Wait for `DONE_INTEREST_RESP`, then enter the place with `how`. |

Step 4 loads geometry first so that DNA-anchored objects (NPC origins, doors) find their
nodes when they generate. On 3DS, the `.tz` must be resident before the set-zone is sent.

Inside a street, crossing into another visgroup sends
`sendSetZoneMsg(newZone, [branch] + vis + [newZone])` directly, with no quiet zone
(`town/Street.py:320-362`).

`loader` and `where` are derived from the zone id (`hood/ZoneUtil.py:30-46, 77-120`):

| `zoneId % 1000` | Result |
|---|---|
| < 100 | `safeZoneLoader` / playground |
| ≥ 500 | interior |
| otherwise | `townLoader` / street |

**Tunnels.**
- The DNA prop `linktunnel_<hood>_<zone>_DNARoot` names its target.
- TTC has these tunnels:
  - `linktunnel_tt_2132` (Silly St), `toontown_central_sz.dna:152`
  - `linktunnel_tt_2226` (Punchline), :37
  - `linktunnel_tt_2301` (Loopy), :268
  - `linktunnel_gs_8000` (Goofy Speedway), :401
- The model `phase_4/models/modules/safe_zone_tunnel_TT.bam` (142 tris) contains a
  `tunnel_trigger` CollisionNode and a `tunnel_origin` (60, 95, 6.67).
- `HoodMgr.addLinkTunnelHooks` (`distributed/HoodMgr.py:877-925`) renames the trigger to
  `tunnel_trigger_<hood>_<zone>`. On enter it calls
  `handleEnterTunnel({how:'tunnelIn', zoneId, hoodId, tunnelOrigin})`. It also records
  placeholder `toph_<hood>_<zone>` at the origin's world transform. That placeholder is
  the arrival point when you come *from* that zone (`makeLinkTunnelName`, :930-931;
  `hood/Place.py:588-623`).
- Tunnel movies are broadcast by our own toon:
  - `setTunnelOut(int16 ts, int16/10 startX, int16/10 startY, int16/10 x, int16/10 y, int16/100 z, int32/100 h)`
  - `setTunnelIn(int16 ts, int16/10 endX, int16/10 x, int16/10 y, int16/100 z, int32/100 h)`
  - Both are `ownsend broadcast` (toon.dc:485-486), so they are safe to send.
  - The values come from `toon/DistributedToon.py:718-735, 767-785` and
    `toon/LocalToon.py:556-562`.
- Motion: pivot at (-14, -6, 0) relative to the origin, pivot angle 135°, pivot duration
  2.0 s, center offset 9.0, influence 0.6 (`DistributedToon.py:148-151, 737-753, 787-806`).
  The tunnel-in starts after `Wait(0.8)` and runs straight to y = 5. The camera sits at
  (0, 20, 12) with HPR (180, -20, 0) relative to the origin.
- **3DS minimum:** fade out and send `setTunnelOut`, do the zone change, then spawn at
  `toph_*` + (endX, 5) facing away from the tunnel and send `setTunnelIn`. The pivot
  animation is polish.

## 4. Trolley (high level)

**DC.** `DistributedTrolley` (toon.dc:700-713):

| Field | Direction / keywords |
|---|---|
| `setState(string, int16)` | broadcast ram |
| `fillSlot0..3(uint32 avId)` | broadcast ram |
| `emptySlot0..3(uint32 avId, int16 ts)` | broadcast ram |
| `requestBoard()` | clsend |
| `requestExit()` | clsend |
| `rejectBoard(uint32)` | AI → client |
| `setMinigameZone(uint32 zone, uint16 minigameId)` | AI → client |

**AI FSM** (`safezone/DistributedTrolleyAI.py:24-38, 201-322`) with constants from
`safezone/TrolleyConstants.py:1-5`:

| State | Timing | Next |
|---|---|---|
| `entering` | 2.0 s | `waitEmpty` |
| `waitEmpty` | until the first board | `waitCountdown` |
| `waitCountdown` | `TROLLEY_COUNTDOWN_TIME` 10 s; config `trolley-countdown-time` | `allAboard` |
| `allAboard` | `TOON_BOARD_TIME` 1 s | `leaving` |
| `leaving` | 5.0 s | `trolleyLeft()` |

`trolleyLeft()` calls `MinigameCreatorAI.createMinigame`, sends `setMinigameZone` to each
seated toon, then re-enters `entering`. Seats are 4. Boarding requires `hp > 0`
(:150-170). Exiting takes `TOON_EXIT_TIME` 1 s.

**Client flow.**
1. The client hits `trolley_sphere`, which is in `trolley_station_TT.bam`, only if the
   toon's Z is ≥ the car's Z (`safezone/DistributedTrolley.py:177-187`).
2. The place goes to `trolley` (`safezone/Playground.py:347-365`).
3. `trolley/Trolley.py:88-92` checks hp, then sends `requestBoard()`
   (DistributedTrolley.py:189-191).
4. The AI broadcasts `fillSlotN(avId)`. Every client runs to seat (-5, -4.5 + 3N, 1.4)
   on `trolley_car`, plays `sit-start` (file `intoSit`), then loops `Sit`
   (DistributedTrolley.py:205-235). The car moves from (-20, 14, -1) to (15, 14, -1) on
   enter and from (15, 14, -1) to (50, 14, -1) on leave (:92-120).
5. On `setMinigameZone(zone, id)`, the client sends `messenger 'playMinigame'` (:277-279).
   This is a quiet-zone change into the minigame zone.

**Minigame lifecycle.** `DistributedMinigame` (toon.dc:1024-1038):
1. Required fields: `setParticipants(uint32[])`, `setTrolleyZone`, `setStartingVotes`,
   `setMetagameRound`, `setDifficultyOverrides`.
2. Client sends `setAvatarJoined` on generate (`minigame/DistributedMinigame.py:94-102`).
3. Client sends `setAvatarReady` after it loads (:343).
4. AI sends `setGameReady`, then `setGameStart(ts)`, then `setGameExit`.
5. AI barriers: JOIN 40 s, READY = load + rules, EXIT 20 s
   (`minigame/DistributedMinigameAI.py:22-24`).
6. Then comes a `PurchaseManager` (toon.dc:1473) for gags and jellybeans, and the toon
   returns to the trolley zone.

The AI config `minigame-id` forces a single game (`minigame/MinigameCreatorAI.py:27, 46-47`).
**For 3DS, force one simple game (for example Catch or Ring) and implement only that
game.**

Trolley model: `phase_4/models/modules/trolley_station_TT.bam` is 1481 tris, 76 geoms and
**10324 verts**, so it needs vertex welding. SFX: `phase_4/audio/sfx/SZ_trolley_away.ogg`
and `SZ_trolley_bell.ogg`.

## 5. Fishing: not supported by this server

The DC is complete:
- `DistributedFishingPond` (toon.dc:959): `hitTarget`, `setArea`.
- `DistributedFishingTarget` (toon.dc:964).
- `DistributedFishingSpot` (toon.dc:969-980):
  - `setPondDoId` and `setPosHpr` are required.
  - `requestEnter` and `requestExit`.
  - `setOccupied(uint32)`.
  - `doCast(uint8/255 power, int16/100 heading)`.
  - `sellFish`.
  - `setMovie(uint8 mode, uint8 code, uint16×3 item, uint8/100 power, int16/100 h)`.
- `DistributedPondBingoManager` (toon.dc:982).

The client side exists: `safezone/DistributedFishingSpot.py:161-185, 253-265`. Four
`fishing_spot` props are in `toontown_central_sz.dna:15-34`.

**But on the server:**
- `DistributedFishingSpotAI`, `DistributedFishingTargetAI` and
  `DistributedPondBingoManagerAI` are 5-line stubs.
- `DistributedFishingPondAI` only stores `area` (`fishing/DistributedFishingPondAI.py:1-15`).
- `findFishingSpots` returns `[]` (`ai/ToontownAIRepository.py:405-406`).

So the pond generates with no spots. **Skip fishing.** Render the pond and the
fisherman NPC, and handle the pond object with the skip table only.

## 6. Mickey (classic character)

**DC.** `DistributedCCharBase` (toon.dc:624-634):

| Field | Keywords |
|---|---|
| `setWalk(string srcNode, string destNode, int16 ts)` | required broadcast ram |
| `setChat(uint32 category, uint32 msg, uint32 avId)` | broadcast |
| `fadeAway()` | broadcast |
| `avatarEnter()`, `avatarExit()` | clsend |
| `setNearbyAvatarChat*` | clsend, optional |

**AI.** The FSM cycles `Lonely` → `Walk` → (`Chatty` if toons are nearby, else `Lonely`)
(`classicchars/DistributedMickeyAI.py:18-28, 64-88`). Timing:
- Lonely lasts `randint(3, 15)` s (`classicchars/CharStateDatasAI.py:19-20`).
- Chatty lasts `CHATTY_DURATION` 120 s (:9).
- Walk sends `setWalk(cur, dest, now)`, and `setWalk(dest, dest, now)` on arrival
  (:175-207).

**Client.**
1. Points = `[pos(src)] + waypoints(src, dest) + [pos(dest)]` (`classicchars/CCharPaths.py:432-435`).
2. Walk each segment at `MickeySpeed` **5.0** u/s (`toonbase/ToontownGlobals.py:561`).
   Turn first at 270°/s, then lerp the position. Start at
   `localElapsedTime(ts)` so late joiners are in sync (`classicchars/CharStateDatas.py:63-117`).
3. `src == dest` means neutral.
4. A proximity sphere of radius **8.0** sends `avatarEnter` and `avatarExit`
   (`classicchars/DistributedCCharBase.py:43-50, 118-135`).
5. `setChat` indexes `CCharChatter` strings, with `%` replaced by the target's name
   (:168-182).

The graph is in `CCharPaths.py:5-48`:
- Nodes a–i, for example a = (17, -17, 4.025) and f = (-106.15, -4, -2.5).
- Waypoint lists per edge, each with a raycast flag.
- Copy it verbatim into a C++ table.

**Assets.**
- Model: `phase_3/models/char/mickey-{1200,800,400}.bam`. mickey-400 is 399 tris but has
  **54 joints**, so it must be split into ≤ 20-bone draw groups or pre-skinned on the CPU.
- Anims: `mickey-walk`, `mickey-run`, `mickey-wait` (`char/Char.py:8-14, 57, 75`).
- Eyes: `phase_3/maps/mickey_eyes_closed.jpg`.
- Dialogue: `phase_3/audio/dial/mickey.ogg`.

## 7. Small things

- **Treasures (ice cream, heals 3 laff).**
  - `DistributedTreasure` (toon.dc:1515-1520): `setPosition(int16/10 ×3)` required,
    `requestGrab()` clsend, `setGrab(uint32 avId)`, `setReject()`.
  - The AI respawns every 20 s, up to 5, at 21 fixed points (`safezone/TTTreasurePlannerAI.py:7-31`).
  - It heals only if hp < maxHp (`safezone/DistributedSZTreasureAI.py:10-23`).
  - The client trigger is a sphere of radius 2.0, and the model is a billboard
    `phase_4/models/props/icecream.bam` (2 tris). SFX: `phase_4/audio/sfx/SZ_DD_treasure.ogg`
    (`safezone/DistributedTreasure.py:39-85`, `DistributedTTTreasure.py:7-8`).
  - **High value, tiny effort.**
- **Butterflies** (24 objects) and `DistributedKnockKnockDoor` (5): skip them with the
  skip table. Don't render either.
- **`DistributedBuilding`** (toon.dc:1597-1603): in the playground the state is always
  `toon`. Parse and ignore it. Cog takeovers only matter on streets.
- **Music:** `phase_4/audio/bgm/TC_nbrhood.ogg`, and `phase_3.5/audio/bgm/TC_SZ_activity.ogg`
  for activities (`safezone/TTSafeZoneLoader.py:12-13`). The full sky and music table is
  in `zones.md`.

## 8. Ranking for the 3DS port

Effort is in focused dev-days and assumes the Phase 3 networking and toon pipeline exist.

| Rank | Feature | Player value | Effort | Notes |
|---|---|---|---|---|
| 1 | Skip tables + ignore-handlers for all zone-2000 classes (§0) | Required | 0.5 | Without them, generates break parsing |
| 2 | NPC toons rendered at origins (fisherman, party planners, later interiors) | Med | 1.5 | Reuses the toon pipeline; DNA blob → parts; neutral loop |
| 3 | Treasures (heal) | Med-High | 1 | 3 fields; billboard sprite |
| 4 | Generic zone change (QuietZone + set-zone emulation + `.tz` load-before-interest) | **Enabler for everything else** | 3 | One code path for tunnels, doors, trolley, teleport |
| 5 | Tunnels to streets (simple fade, `toph_*` arrival, `setTunnelIn/Out`) | High | 1.5 | After 4; the street itself is in zones.md/combat.md |
| 6 | Doors → interiors (gag shop, HQ, toon interiors baked per zone id) | Med-High | 4 | 2 days baker (seeded RNG, triggers/origins), 2 days client FSM |
| 7 | Mickey walking + chat | Med (charm) | 3 | 54-joint skinning split is the cost |
| 8 | NPC interaction (avatarEnter → setMovie bubble → setMovieDone) | Low until quests/shops exist | 1 | |
| 9 | Trolley boarding + one forced minigame + PurchaseManager | High | 8–12 | Boarding 2 d; per-minigame 3–5 d; purchase screen 3 d |
| — | Fishing | n/a | — | Server-side stubs: do not build |
| — | Butterflies, knock-knock doors, party gate | Low | — | Skip |
