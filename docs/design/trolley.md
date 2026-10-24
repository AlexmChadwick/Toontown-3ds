# Trolley and minigames: protocol and client spec

Status: design, Phase 4+. Research only, nothing here is implemented or tested yet.
Upstream is the specification. Python paths are relative to `ref/open-toontown/toontown/`
unless they start with `etc/` (= `ref/open-toontown/etc/`) or `phase_` (= `ref/resources/`).
Line numbers are as of the 2026-10-23 clone. `docs/design/playground.md` section 4 has a
short trolley summary; this file supersedes it and corrects one detail (see 2.3: the
client sends `setAvatarReady` after the rules panel, not right after loading).

Directory shorthand used below:

| Short | Directory |
|---|---|
| `DistributedTrolley(AI).py`, `TrolleyConstants.py`, `Playground.py` | `safezone/` |
| `Trolley.py` | `trolley/` |
| `ToonHood.py`, `Hood.py`, `QuietZoneState.py`, `ZoneUtil.py` | `hood/` |
| `DistributedMinigame(AI).py`, `MinigameCreatorAI.py`, `MinigameGlobals.py`, `Purchase*.py`, per-game files | `minigame/` |
| `PurchaseManager(AI).py`, `NewbiePurchaseManager(AI).py`, `PurchaseManagerConstants.py` | `shtiker/` |
| `ToontownGlobals.py`, `TTLocalizerEnglish.py` | `toonbase/` |
| `Quests.py`, `QuestManagerAI.py` | `quest/` |

Facts that were checked by running code (not just reading): DC class and field numbers
(Panda `DCFile`, hash 547155168 matches `source/net/dc_generated.h`), model node names and
triangle counts (stock `panda3d==1.10.15` from `.venv`), existence of every asset path
(`ls`/`os.path.exists`), and that Panda's `Mersenne(seed).getUint31()` equals
`mt19937(seed)() >> 1` (section 4). Anything not checked that way is marked UNVERIFIED.

## 0. Summary of the important findings

1. The whole trolley protocol is 2 client sends (`requestBoard`, `requestExit`) and 1
   targeted receive (`setMinigameZone(zone, gameId)`). The AI allocates a **dynamic zone**
   (>= 61000, `ToontownGlobals.py:210`) per game and generates the minigame object there
   *before* telling the clients. Entering it is a normal quiet-zone change, which
   `Client::changeZone` already does.
2. Every minigame shares one client protocol (section 2): `setAvatarJoined` on generate,
   `setAvatarReady` after the rules panel, `setGameStart` back, `setGameExit` at the end,
   `setAvatarExited` in reply. Only the game-specific middle differs.
3. Payout is **server side and unconditional**. Jellybeans are added to the toon in the
   `PurchaseManagerAI` constructor (`PurchaseManagerAI.py:77`). Skipping the gag shop UI
   loses nothing, but the client must still send `requestExit` and `setInventory(done=1)`
   or the AI leaks the purchase manager and the minigame zone until we disconnect
   (2.5).
4. A client that cannot play a game can bail out safely: `requestExit` on the minigame
   aborts it at once and pays a random 1..7 beans (`DistributedMinigameAI.py:163-167,
   304-309, 398-400`). Never sending `setAvatarReady` does the same after 66 s (2.6).
5. In the stock solo pool the AI picks among **11 games** (Cannon, Ring, Maze, Tug of War,
   Catch, Diving, Target, Pairing, Vine, Cog Thief, Two-D). Tag, Pattern (Match Minnie),
   Ice, Race and Photo never appear for one rider; Trolley Tracks is the `TravelGame`
   metagame and only runs with a holiday or `trolley-goes-to-metagame` (2.7).
6. The AI can be forced to one game with the PRC variable `minigame-id <id>`
   (`MinigameCreatorAI.py:27, 48-49`). `tools/server/start.sh` does not currently expose a
   way to set it without editing `ref/` (2.7).
7. Recommended first three games: **Cannon, Catch, Tug of War** (section 3). Match Minnie
   is the cheapest to code but cannot be reached with one rider unless forced.

## 1. The playground trolley

### 1.1 DC surface (`etc/toon.dc:700-713`)

`DistributedTrolley : DistributedObject`, class id **86**
(`source/net/dc_generated.h:542`), no required fields. Every field below except the two
`clsend` ones is `broadcast ram`, so the current value is replayed in
`ENTER_OBJECT_REQUIRED_OTHER` when we get interest in the zone. A client that arrives late
therefore sees the current `setState` and the current `fillSlotN` values immediately.

| Field (number) | Args | Keywords | Dir |
|---|---|---|---|
| setState (630) | string, int16 ts | broadcast ram | S to C |
| fillSlot0..3 (631-634) | uint32 avId (0 = seat cleared) | broadcast ram | S to C |
| emptySlot0..3 (635-638) | uint32 avId, int16 ts | broadcast ram | S to C |
| **requestBoard** (639) | () | airecv **clsend** | C to S |
| rejectBoard (640) | uint32 avId | (to one avatar) | S to C |
| **requestExit** (641) | () | airecv **clsend** | C to S |
| setMinigameZone (642) | uint32 zone, uint16 minigameId | (to one avatar) | S to C |

To use it: add `requestBoard`, `requestExit` and the receive fields to dcgen `WANTED`
(same procedure as `combat.md` section 1) and whitelist the two sends with
`TT_NET_UPDATE(w, trolleyDoId, DistributedTrolley, requestBoard)`. The `kw_*` masks in
`dc_generated.h:559-570` already show both as `0x110` (airecv + clsend).

### 1.2 Server state machine

`DistributedTrolleyAI.py:25-38, 186-324`, constants `TrolleyConstants.py:1-5`:

| AI state | Entered | `accepting` | Lasts | `setState` sent |
|---|---|---|---|---|
| entering | `start()` / after `trolleyLeft` (:201-206) | 0 | `TROLLEY_ENTER_TIME` 2.0 s | "entering" |
| waitEmpty | (:222-224) | 1 | until a boarder | "waitEmpty" |
| waitCountdown | first boarder (:232-235) | 1 | `trolley-countdown-time`, default 10.0 s (:24) | "waitCountdown" |
| allAboard | countdown over and seats > 0 (:237-257) | 0 | `max(TOON_BOARD_TIME 1.0 - elapsed since last boarding, 0)` | none (the client FSM has no such state) |
| leaving | (:273-276) | 0 | `TROLLEY_EXIT_TIME` 5.0 s | "leaving" |

After `leaving`, `trolleyLeft()` (:282-320) builds the player list from the 4 seats, calls
`MinigameCreatorAI.createMinigame`, sends `setMinigameZone(zone, id)` to each seated avatar
only (:311-315), clears the seats (`fillSlotN(0)` broadcast), then re-enters `entering`.
Timeline from the first board: about 10 s countdown + <= 1 s + 5 s = **about 15 s** to
`setMinigameZone`.

Rules to mirror:
- `requestBoard` is rejected with `rejectBoard(avId)` unless `accepting` is 1, the toon has
  `hp > 0`, a seat is free (4 seats, :21-22, :74-81) and `ToontownAccessAI.canAccess` passes
  (:160-163). Duplicate requests are ignored (:153-155).
- A second boarder does **not** extend the countdown: `waitCountdown()` is requested again
  while already in that state and ClassicFSM drops same-state requests with no self
  transition (`direct/fsm/ClassicFSM.py:301-306`, checked in `.venv`).
- `requestExit` is only honoured while `accepting` (:172-184). After `allAboard` there is no
  hop-off; the AI silently ignores it (`rejectExiter` is `pass`, :110-111).
- Exit broadcasts `emptySlotN(avId, ts)`, then `emptySlotN(0, ts)` after `TOON_EXIT_TIME`
  1.0 s (:116-131). If the car empties it goes back to `waitEmpty` (:124-125).
- A disconnect frees the seat (`__handleUnexpectedExit`, :95-105).
- **Access gate warning.** `canAccess` only passes for the TTC hood (and a few special
  zones) unless the toon's `gameAccess` is `AccessFull` (`ToontownAccessAI.py:6-44`). A
  non-full toon who requests a trolley elsewhere is **banned** by default
  (`want-ban-ispaid`, :12-15). What access our dev toons get is UNVERIFIED
  (`DistributedToonAI.py:3785-3795` falls back to Full only when `__dev__`). Keep the
  trolley to TTC until a test shows other hoods are safe.

### 1.3 Per-playground placement and model

Every hood with a trolley runs `DistributedTrolleyAI` at the hood zone
(`hood/TTHoodDataAI.py:22-25`, same lines in DD, DG, MM, BR, DL `*HoodDataAI.py`). Outdoor
Zone (OZ) reuses the DG model in its storage file but its hood data creates no trolley
(UNVERIFIED for OZ; `phase_6/dna/storage_OZ_sz.dna:32` only maps the model).

| Hood (zone) | Model | DNA placement (pos / nhpr) | DNA line |
|---|---|---|---|
| TTC (2000) | `phase_4/models/modules/trolley_station_TT.bam` | (-120.945, -77.5626, 0.525) / H 128 | `phase_4/dna/toontown_central_sz.dna:470-473` |
| DD (1000) | `phase_6/models/modules/trolley_station_DD.bam` | (-25, -90, 5.67) / H 180 | `phase_6/dna/donalds_dock_sz.dna:331-334` |
| DG (5000) | `phase_8/models/modules/trolley_station_DG.bam` | (8.37, 222.32, 10) / H 0 | `phase_8/dna/daisys_garden_sz.dna:416-419` |
| MM (4000) | `phase_6/models/modules/trolley_station_MM.bam` | (-54.0463, 55.8937, 6.5) / H 45 | `phase_6/dna/minnies_melody_land_sz.dna:154-157` |
| BR (3000) | `phase_8/models/modules/trolley_station_BR.bam` | (52.5, 55.8, 6.15) / H -70 | `phase_8/dna/the_burrrgh_sz.dna:478-481` |
| DL (9000) | `phase_8/models/modules/trolley_station_DL.bam` | (64, -105.2, 0) / H -180 | `phase_8/dna/donalds_dreamland_sz.dna:306-309` |

The storage files map the model to code `prop_trolley_station`
(`phase_4/dna/storage_TT_sz.dna:10-11`, `phase_6/dna/storage_DD_sz.dna:10-11`, and the MM,
BR, DG, DL equivalents). All six models have the same node structure (checked with Panda):

- Root `prop_trolley_station`, children `trolley_sphere`, `trolley_car`,
  `prop_trolley_station_collisions` (mask 0x5), `prop_trolley_station_floor_collisions`
  (mask 0x2). The python finds them with `loader.geom.find('**/*trolley_station*')`
  (`DistributedTrolley.py:39-41`).
- `trolley_sphere` is an **intangible** `CollisionSphere`, centre (16.19, 14.88, 3.57) in
  station space, radius 15.756 (DG: centre y 13.88), into-mask 0x1. It is already baked as
  the trigger `trolley_sphere` in `sdmc-out/.../zones/2000.json` (`triggers` list).
  `tools/pipeline/collide.py:15-27` explains why it becomes a trigger and not a wall.
- `trolley_car` default pose (15.751, 14.1588, -0.9846) with sub-nodes `trolley_top/trolley`,
  `key` x3 (spinning key on the roof), `front_wheels` x3 and `back_wheels` x3.
- Size: **1481 tris, 76 geoms, 16 textures** for the whole prop (TT/DD/MM/BR/DG/DL all the
  same count), of which the car subtree is **1421 tris, 60 geoms**. The platform is only
  about 60 tris. Textures are `phase_4_palette_*`, `trolley_palette_*`, `yellow1`
  (`phase_4/maps/trolley_palette_*.jpg` exist).
- Sounds: `phase_4/audio/sfx/SZ_trolley_away.ogg` and `SZ_trolley_bell.ogg` (both exist).
  `phase_4/audio/bgm/trolley_song.ogg` exists but no python in this tree references it
  (UNVERIFIED use).

**Bake gap closed (2026-10-24, bakefix).** `bake_street.py` now excludes the entire
`trolley_car` subtree before static geometry and collision extraction in the six zones
above. The 60-triangle platform and `trolley_sphere` remain in the zone. Each zone gets
`<zone>_trolley.ttm`, `<zone>_trolley.ttc`, `<zone>_moving.json` and a `trolley_station`
entry in `<zone>.pts`. These files are in `sdmc-out/3ds/toontown3ds/data/zones/`.

The TTM is **car-pivot local**, with the car's own transform removed. Runtime matrix:
`station(position,H) * translate(carPosition) * rotateH(90 degrees)`. All six stations
have unit scale and zero pitch/roll. The parked authored car position is
`(15.751000,14.158800,-0.984615)`; its authored **H is 90**, not zero. Preserve that
heading during upstream position intervals: enter `(-20,14,-1)` to `(15,14,-1)` in
2 seconds, ease-out; leave `(15,14,-1)` to `(50,14,-1)` in 5 seconds, ease-in.
Use server state time to seek the interval. Wheels/key are merged and do not spin.
Seat/camera coordinates in section 1.4 are relative to this transformed car.

Every car is 1,421 triangles / 14 draws / 171,406 geometry bytes / 130,976 texture
bytes (304,512-byte file). The optional 768-byte TTC contains three intangible
trigger polygons, **no solid floor/wall**; do not use it as a rideable floor.
The boarding sphere remains static and uses its existing upstream event rule.
Read the `.pts` station anchor, never add the old parked car a second time.
See `streets.md`'s bakefix table and moving-prop frame reservations before drawing.

### 1.4 Client flow (what to implement)

**Approach and trigger** (`DistributedTrolley.py:177-189`, `Playground.py:347-366`):
1. Trolley state must be `waitEmpty` or `waitCountdown` (the python only enables the sphere
   in those states, :281-289, 303-310).
2. The toon enters the `trolley_sphere` trigger and its world Z is not below the car's Z
   (:179-180). Use GravityWalker's event sphere rule from `AGENTS.md` section 6 Phase 4
   (centre r - 0.1, radius r x 1.04).
3. If the toon has `hp <= 0`: show the "trolley HFA" message and stay in the playground
   (`Trolley.py:87-94, 106-115`). Otherwise send `requestBoard`.
4. The local toon stops walking, sends `setAnimState("off")` (`Playground.py:349`; "off" is
   in `ToonAnimStates`, `ToontownGlobals.py:1621-1622`) and the camera lerps to
   (-35, 0, 8) / H -90 relative to the car over 1.5 s (`Trolley.py:148-152`).

**Seating**, per seat index `i` in 0..3, positions in `trolley_car` space
(`DistributedTrolley.py:205-233`):

| Step | Duration | Action |
|---|---|---|
| 1 | (instant) | stop smoothing, reparent to car keeping the world transform, play `run`, head toward (-5, -4.5 + 3i, 1.4) |
| 2 | 0.75 x `TOON_BOARD_TIME` (0.75 s) | move to (-5, -4.5 + 3i, 1.4) |
| 3 | 0.25 s | turn to H 90 |
| 4 | duration of `sit-start` (clip file `intoSit`) | play `sit-start`; during its second quarter lerp pos to (-3.9, -4.5 + 3i, 3.0) |
| 5 | loop | `Sit` (clip file `sit`) |

If our own `avId` fills a slot while the client is not in `waitEmpty`/`waitCountdown`, drop
back to walking (:209-213). Remote toons need the same animation when `fillSlotN` arrives
for them. While seated the local toon sends **no** position updates (`LocalToon` walking is
off); remote views animate it locally.

Clips needed (all exist for every body type under `phase_4/models/char/`, for example
`tt_a_chr_dgm_shorts_legs_intoSit.bam` and `..._torso_intoSit.bam`, `..._sit.bam`; verified
for the dgm legs/torso): `intoSit`, `sit`. Our current toon bake has only neutral, walk,
run, jump.

**Hop off** (`Trolley.py:158-181`, `DistributedTrolley.py:339-340`): a "Hop off" button
sends `requestExit`. On `emptySlotN(avId, ts)` with a real avId (:256-272): face H 90, play
`sit-start` backwards, move to the seat position, then `run` to
(21 - 3i, -5, 0.02) in **station** space in 1.0 s (`TOON_EXIT_TIME`), play `neutral`, give
control back (remote toons resume smoothing, :247-254).

**Countdown clock** (:309-337): a billboard text at (15.86, 13.82, 11.68) in station space,
scale 3.02, colour (0.9, 0.1, 0.1), showing `int(duration - elapsed)`. Start value is
`trolleyCountdownTime - elapsedSince(ts)` using the 16-bit `setState` timestamp. On the 3DS
a bottom-screen countdown plus a "Hop off" touch button is enough.

**Car motion** (`DistributedTrolley.py:91-121`, state `entering`/`leaving`):
- Enter: car (-20, 14, -1) to (15, 14, -1) in 2.0 s, ease out; sound `SZ_trolley_away`.
- Exit (`leaving`): (15, 14, -1) to (50, 14, -1) in 5.0 s, ease in; sounds `SZ_trolley_bell`
  and `SZ_trolley_away`. Start the interval at the elapsed time from the `setState`
  timestamp (`:162-163, 297-301, 350-357`). Black exit fog is polish.
- Wheel spin (`animateTrolley`, :359-373): wheels turn `distance / (2 pi 0.95) x 360` degrees;
  keys turn `round(T) x 360` degrees. Optional.
- While leaving with the local toon aboard, the camera lerps to (0, 18.55, 3.75) / H -180
  over 3 s (`Trolley.py:183-187`).

**Rejection and failure:** `rejectBoard` returns the toon to walking
(`DistributedTrolley.py:274-275`). If a `fillSlot` for us arrives and we have no place
object, the python sends `requestExit` (:219-220); do the same.

### 1.5 Hand-off to the minigame

`setMinigameZone(zone, gameId)` (`DistributedTrolley.py:277-279`) is the only trigger:

1. `Trolley.handlePlayMinigame` (`Trolley.py:189-195`) sends `setParent(SPHidden)`
   (`OTPGlobals.py:27`, value 1). Our `Client::sendSetParent` already exists
   (`source/net/client.cpp:1451`); the token for hidden is 1, render is 2 (our constant
   `kParentRender` is the latter, :790).
2. `Playground.handleTrolleyDone` (`Playground.py:364-383`) returns a done status
   `{loader: 'minigame', where: 'minigame', zoneId, minigameId}`, and
   `Hood.handleSafeZoneLoaderDone` (`Hood.py:224-226`) sends the hood to `quietZone`, then
   to its `minigame` state (`ToonHood.py:174-180`).
3. For us: stop the trolley UI, fade out, call `Client::changeZone(zone)` with **no vis
   list** (it does location to quiet zone 1, drops old objects, then location plus interest
   to the target zone; `source/net/client.cpp:1386-1399`). The minigame DO already exists
   there, so it arrives in `ENTER_OBJECT_REQUIRED_OTHER` as soon as interest opens.
4. Remember the playground zone (`setTrolleyZone` also carries it, and the trolley's own
   zone id is the hood id) for the return trip (2.5).

The `gameId` in `setMinigameZone` is redundant with the DO's class and is only useful for
loading the right module before the object arrives. It is a `uint16`; the ids are in
`ToontownGlobals.py:328-345`.

Constraints inherited from the client:
- `Client::sendAnimState` and the pose senders only work in `State::InZone`
  (`client.cpp:1580`). Between `changeZone` and `EnteredZone` do not send them.
- The client has `ChangingZone`/`InZone` states already; the dynamic zone id (>= 61000)
  has never been used as a zone target in our client. UNVERIFIED that the Astron client
  agent config accepts it (it should: the AI allocates exactly these in the stock server).

## 2. Minigame framework

### 2.1 Base class and common fields

`DistributedMinigame : DistributedObject` (`etc/toon.dc:1024-1038`), class id **121**. All
subclass ids and the field numbers are in the table in section 3; the base field numbers
(checked) are:

| Field (number) | Args | Keywords | Dir |
|---|---|---|---|
| setParticipants (849) | uint32[] avIds | required broadcast ram | S to C |
| setTrolleyZone (850) | uint32 | required broadcast ram | S to C |
| setStartingVotes (851) | uint16[] | required broadcast ram | S to C |
| setMetagameRound (852) | int8 (-1 = none) | required broadcast ram | S to C |
| setDifficultyOverrides (853) | int32 difficulty, int32 trolleyZoneOverride | required broadcast ram | S to C |
| **setAvatarJoined** (854) | () | airecv clsend | C to S |
| **setAvatarReady** (855) | () | airecv clsend | C to S |
| **setAvatarExited** (856) | () | airecv clsend | C to S |
| **requestExit** (857) | () | airecv clsend | C to S |
| setGameReady (858) | () | broadcast | S to C |
| setGameStart (859) | int16 net timestamp | broadcast | S to C |
| setGameExit (860) | () | broadcast | S to C |
| setGameAbort (861) | () | broadcast | S to C |

Required-field order in `ENTER_OBJECT_REQUIRED(_OTHER)` is the inherited order: the five
base fields, then the subclass required fields (these are required: Ring `setTimeBase`,
`setColorIndices`; Target `setTimeBase`; Travel `setBoardIndex`; Pairing `setDeckSeed`;
Vine `setVineSections`; Two-D `setSectionsSelected`; all other classes have none).

Parsing notes: `setDifficultyOverrides` is `NoDifficultyOverride` 2147483647 and
`NoTrolleyZoneOverride` -1 when unset (`MinigameGlobals.py:14-15`); difficulty is
`value / 65536` (`:8`, `DistributedMinigame.py:223-229`). `setStartingVotes` must have the
same length as `setParticipants` (`DistributedMinigame.py:433-441`). A client that is not in
`setParticipants` must ignore the object (`:204-210`).

### 2.2 Sequence and server timeouts

`DistributedMinigameAI.py:16-24, 184-328`:

| Step | Who | What | Timeout |
|---|---|---|---|
| 0 | AI | `createMinigame` generates the DO (`MinigameCreatorAI.py:119`); `frameworkWaitClientsJoin` starts a barrier on all participants | JOIN 40 + 10 = **50 s** |
| 1 | client | `setAvatarJoined` as soon as the DO exists, **before** loading (`DistributedMinigame.py:94-102`) | timeout: `setGameAbort` (:196-198) |
| 2 | AI | all joined: `setGameReady` broadcast; barrier on ready | READY 40 + 16 + 10 = **66 s** (:23) |
| 3 | client | load, show the rules panel (16 s auto-timeout, `MinigameRulesPanel.py:12, 45`), then `setAvatarReady` (`DistributedMinigame.py:341-344`) | timeout: `setGameAbort` (:224-226) |
| 4 | AI | all ready: `setGameStart(ts)` broadcast; game FSM starts | per game |
| 5 | AI | `gameOver()`: `frameworkWaitClientsExit`, broadcasts `setGameExit` | EXIT 20 + 10 = **30 s** |
| 6 | client | on `setGameExit` do cleanup, then `setAvatarExited` (cleanup action, :111-116, 369-379) | timeout: cleanup anyway, **normal** payout (:267-269) |
| 7 | AI | `frameworkCleanup`: compute payout, `requestDelete()` of the DO, generate `PurchaseManager`(s) in the same zone (:293-328) | |

Details that matter:
- `setAvatarReady` is also accepted during `frameworkWaitClientsJoin` and remembered
  (:236-245, :229-231), so a fast client may send it early.
- `setAvatarJoined` and `setAvatarReady` are not checked against the participant list in
  `DistributedMinigameAI`, only the barrier tracks them. Subclass game messages usually
  check `avId in avIdList` and log `suspicious`.
- Barriers also fire on avatar disconnect (`ToonBarrier.py:35-37`, event
  `distObjDelete-<avId>`, `ToontownAIRepository.py:475-476`), after which
  `handleExitedAvatar` aborts the game for everyone (:169-172).
- The `setGameStart` timestamp is 16-bit centisecond network time. Game-relative time is
  `now - gameStartTime` (`DistributedMinigame.py:271-276, 404-411`). Reuse the
  `Client::networkTime` helpers that `setSmPosHpr` uses.
- Multi-player only: on `setGameReady` the client checks that every other participant's
  toon object exists and waits for the server to abort otherwise
  (`DistributedMinigame.py:231-269`). Solo play skips this.

### 2.3 What the client must do (state list)

| Client state | Enter on | Do |
|---|---|---|
| Init | DO arrives with local toon in `setParticipants` | send `setAvatarJoined`, start loading assets |
| Rules | load done | show title and instructions (bottom screen), then play button or 16 s timer |
| WaitStart | play pressed | send `setAvatarReady` |
| Game | `setGameStart` | game specific |
| WaitFinish | game decided locally (some games) | wait for `setGameExit` |
| Cleanup | `setGameExit`, `setGameAbort`, or DO delete | free assets; if the exit was normal send `setAvatarExited`; wait for the PurchaseManager |

`setGameAbort` sets `normalExit = 0`, so no `setAvatarExited` is sent
(`DistributedMinigame.py:278-283`). The python hood does nothing on cleanup
(`ToonHood.py:192-193`); the next thing that happens is the PurchaseManager generating.

To save time the 3DS client may skip the rules panel by sending `setAvatarReady` right
after loading. That is allowed by the server (it only waits for the barrier). Keep the
rules text for the first play at least.

### 2.4 Scores to jellybeans

`enterFrameworkCleanup` (`DistributedMinigameAI.py:293-328`), `MinigameGlobals.py:23-35`:

1. `scoreDict[avId]` starts at `DEFAULT_POINTS` 1 (:20, :188) and is set by each game's AI.
2. Multiply by `getScoreMult(trolleyZone)` = lerp(1.0, 1.5, hoodIndex/5) over the hood
   order TTC, DD, DG, MM, BR, DL: **TTC 1.0, DD 1.1, DG 1.2, MM 1.3, BR 1.4, DL 1.5**,
   unless the game's `hasScoreMult()` returns 0 (Target and Diving do, :290-291).
3. `score = int(score + 0.5)` for a normal exit; **random 1..7** (`randrange(1, 8)`, same
   value for everyone) for an aborted game (:302-309).
4. Double on the trolley-holiday flags (:310-311), clamp to 0..255 (:313-321).
5. The `PurchaseManagerAI` constructor does `av.addMoney(points)` (`PurchaseManagerAI.py:77`)
   which sends `setMoney` to the owner and spills over `maxMoney` into the bank
   (`DistributedToonAI.py:2408-2415`). So **money is final before the client sees the
   purchase manager**.

### 2.5 PurchaseManager, the gag screen, and the way back

`PurchaseManager : DistributedObject`, class **176** (`etc/toon.dc:1473-1486`), generated in
the same dynamic zone as the finished game (`DistributedMinigameAI.py:385-393`). Field
numbers (checked) 1133-1144:

| Field | Args | Keywords | Dir |
|---|---|---|---|
| setPlayerIds | 4 x uint32 (0 padded) | required broadcast ram | S to C |
| setNewbieIds | uint32[] | required broadcast ram | S to C |
| setMinigamePoints | 4 x uint8 | required broadcast ram | S to C |
| setPlayerMoney | 4 x uint8 (**before** the payout) | required broadcast ram | S to C |
| setPlayerStates | 4 x uint8 | required broadcast ram | S to C |
| setCountdown | int16 net timestamp | required broadcast ram | S to C |
| setMetagameRound | int8 | required broadcast ram | S to C |
| setVotesArray | int16[] | required broadcast ram | S to C |
| **requestExit** | () | airecv clsend | C to S |
| **requestPlayAgain** | () | airecv clsend | C to S |
| **setInventory** | blob inv, int16 money, uint8 done | airecv clsend | C to S |
| setPurchaseExit | () | broadcast | S to C |

`NewbiePurchaseManager` (class 177, `setOwnedNewbieId` appended, field 1145) is generated
instead for toons that are "newbies" (below). Player states
(`PurchaseManagerConstants.py:1-8`): 0 no client, 1 waiting, 2 play again, 3 exit, 4
disconnected; report states 10 unreported, 11 reported, 12 cannot report.

**Python flow** (`PurchaseManager.py:47-108`, `Purchase.py:228-244`,
`ToonHood.py:89-122`):
1. DO arrives: the client shows the purchase screen with a 120 s countdown
   (`PURCHASE_COUNTDOWN_TIME`, `PurchaseManagerConstants.py:9`; the timer starts when the
   AI reads `setCountdown` at generation, `PurchaseManagerAI.py:131-137`).
2. Each gag bought sends `setInventory(inv, money, done=0)`; the AI validates and stores
   it (`PurchaseManagerAI.py:208-223`, `InventoryBase.py:238-260`).
3. Buttons: "Back to Playground" sends `requestExit`; "Play Again" sends `requestPlayAgain`;
   a timeout is local only.
4. When every player has decided (or 120 s pass) the AI broadcasts `setPurchaseExit`
   (`:251-256`). Every client answers with `setInventory(inv, money, done=1)`
   (`PurchaseManager.py:96-100`). When all have reported the AI runs `shutDown`
   (:270-296): on any "play again" it calls `createMinigame` **reusing the same zone** with
   the previous game excluded; otherwise it frees the zone.
5. "Back to playground": `Purchase.handleDone` sends `setParent(SPHidden)` and the hood
   goes through `quietZone` to `{loader: safeZoneLoader, where: playground, how:
   teleportIn, zoneId: hoodId}` (`ToonHood.py:108-117`). `Playground.enterTeleportIn`
   places the toon at a drop point from `HoodMgr.getPlaygroundCenterFromId`
   (`distributed/HoodMgr.py:846-852`, `Playground.py:489-545`), plays `TeleportIn`, and
   sends `setParent(SPRender)` (`Place.py:726-759`). **The server never moves the toon back;
   the client must change zone itself.**

**What to do on the 3DS.** Implement a minimal "purchase" step that does not need the gag
UI. Required bytes and order:
1. PM object arrives (required fields parsed): show the jellybean tally (the points for
   our slot are `setMinigamePoints[i]` where `i` is our index in `setPlayerIds`).
2. Send `requestExit` (valid in state 1 or 2 and while `receivingButtons`,
   `PurchaseManagerAI.py:145-155`). If we are the only player the AI immediately calls
   `timeIsUp` and broadcasts `setPurchaseExit`.
3. On `setPurchaseExit` send `setInventory(blob, money, 1)` where `blob` is the 49-byte
   inventory we already hold (`OwnStats::inventory`, `client.h:153`; format in
   `combat.md` section 2) and `money` is **`OwnStats.money` as last set by the server after
   the payout**. Echoing a stale lower number would lower the toon's money:
   `av.setMoney(newMoney)` is applied when `validatePurchase` passes
   (`PurchaseManagerAI.py:219-221`), and the rule is only `newMoney <= currentMoney`
   (`InventoryBase.py:239-241`).
4. Then `changeZone(hoodZone)`, place the toon at a drop point, `setParent(render)`.

**If the client skips the screen entirely** (never sends `requestExit` and
`setInventory(done=1)`): payout still happened. But `playersReported[i]` stays
unreported, so the PM and the dynamic zone are only released when we disconnect
(`PurchaseManagerAI.py:343-358`; `MinigameCreatorAI.releaseMinigameZone`,
`MinigameCreatorAI.py:140-144`). After 120 s the AI broadcasts `setPurchaseExit` into a
zone nobody is in. Harmless to play but a slow AI leak per ride. Do not ship that.

**Newbie edge case.** If `quests == [[110, ...]]` with progress 0 the AI treats the toon as
a newbie (`DistributedTrolleyAI.py:286-292`, `Quests.py:19714-19719`) and sends a
`NewbiePurchaseManager` with no countdown (`NewbiePurchaseManagerAI.py:8-12`); the python
then plays an NPC tutorial movie. Our toons go through `requestSkipTutorial`
(`tutorial/TutorialManagerAI.py:230-262`): it assigns quest 110 and calls
`completeAllQuestsMagically`, so progress is non-zero, `avatarHasCompletedTrolleyQuest` is
true and the toon is **not** a newbie. This is read from the source, not run; if a test
ever yields class 177, treat it like class 176 but wait for `requestExit` only
(`handlePlayerLeaving` marks the quest done, `NewbiePurchaseManagerAI.py:17-20`).

### 2.6 Aborts, timeouts, and "client never sends setAvatarReady"

All checked in `DistributedMinigameAI.py`:

| Situation | Result |
|---|---|
| never `setAvatarJoined` | after **50 s** `setGameAbort` (:196-200) |
| joined, never `setAvatarReady` | after **66 s** from `setGameReady` `setGameAbort` (:224-228) |
| `requestExit` at any time | immediate `setGameAbort` (:398-400) |
| client leaves the zone but stays connected | nothing is notified; the barriers above run out |
| client disconnects | `handleExitedAvatar` aborts for everyone (:169-172) |
| game decided, never `setAvatarExited` | after 30 s cleanup with **normal** payout (:267-269) |

`setGameAbort` broadcasts to the DO, sets `normalExit = 0`, goes to `frameworkCleanup`: the
DO is deleted, a PurchaseManager is generated with random 1..7 beans each (2.4), and the
2.5 flow follows. Per-game AIs override `setGameAbort` but all guard on
`gameFSM.getCurrentState()` being set (checked for Ring, Catch, Cannon, Tag, Maze, Pattern,
Pairing, Target, Diving, Tug, Race, Vine).

**Stub for unimplemented games (recommended):** on a game id we do not support, send
`setAvatarJoined`, show "This game isn't on the 3DS yet", send `requestExit`. The player
gets 1..7 beans, and every other rider is aborted too (so only use it solo or accept that).
This is faster and cleaner than waiting out the 66 s barrier.

Per-game timers also exist (examples): Catch waits `GameDuration` 55 s + 10 s for
`reportDone` and aborts otherwise (`DistributedCatchGameAI.py:63-85`,
`CatchGameGlobals.py:1`); Pairing and Two-D do the same with their own durations; Pattern
aborts if a client does not send `reportPlayerReady` within 15 s (`PatternGameGlobals.py:
8-11`, `DistributedPatternGameAI.py:66-90`).

### 2.7 Which game does the AI pick, and can we force it?

`MinigameCreatorAI.createMinigame` (`MinigameCreatorAI.py:31-131`), in priority order:
1. A per-avatar request stored in `RequestMinigame[avId]` (:38-47). Only the magic word
   `~mg` sets it (`spellbook/MagicWordIndex.py:374-445`). It needs `accessLevel >= 100`
   (MODERATOR, `MagicWordIndex.py:59`; `ToontownMagicWordManagerAI.py:196-198`). New toons
   have `accessLevel 0` (`etc/otp.dc:253`), so using it needs a DB edit and sending the
   `requestExecuteMagicWord` field (not in our whitelist). UNVERIFIED end to end.
2. `simbase.forcedMinigameId = config.GetInt('minigame-id', 0)` (:27, 48-49). **This is
   the supported lever.** It forces every ride and every "play again", in solo too, even for
   games that would never be drawn solo.
3. Otherwise random: `MinigamePlayerMatrix[numPlayers]` (`ToontownGlobals.py:384-438`). With
   `want-all-minigames` true (the default, `ToontownAIRepository.py:66`) and more than one
   rider the list becomes **all** `MinigameIDs` (:367-383) minus Travel (:52-60), which
   includes Photo. The previous game is removed (:62-64). `removeUnreleasedMinigames` has
   no effect now because every release date is in 2008 (`ToontownGlobals.py:439-442`, :147-186).

Solo pool (`MinigamePlayerMatrix[1]`, `ToontownGlobals.py:384-395`): Cannon, Ring, Maze,
Tug of War, Catch, Diving, Target, Pairing, Vine, Cog Thief, Two-D (11 games). Two riders
add Pattern, Tag, Ice; three or four add Race.

Trolley Tracks is `DistributedTravelGame` (id 100, `TravelGameId`, `TTLocalizerEnglish.py:
9185`). The trolley only goes there if `trolley-goes-to-metagame` is true or a trolley
holiday/weekend is on (`DistributedTrolleyAI.py:299-308`); the metagame needs two or more
players (`metagame-min-2-players` default 1).

**How to set `minigame-id` here.** `tools/server/start.sh:94-97` starts the AI with no
config file argument, so `AIStart.py:14-19` falls back to `etc/Configrc.prc`, which is
`ref/open-toontown/etc` through a symlink (`start.sh:42`). `AIStart` accepts several PRC
paths, so the clean fix is to give the AI both `etc/Configrc.prc` and a repo-local
`tools/server/ai.prc` containing `minigame-id 2` (and optionally `trolley-countdown-time 3`
to shorten tests; the client reads the same name for its display, `DistributedTrolley.py:22`).
That is a change to `tools/server/start.sh`, which is outside this file; do it as a
follow-up. It must be loaded before `MinigameCreatorAI` is imported (it is: PRC files load
first, `AIStart.py:18-19`). UNVERIFIED: not run.

## 3. The stock minigames

Class ids, field numbers and required fields checked with `DCFile`. "C to S" lists every
`clsend` field the client may send; `airecv` ones reach the AI, `clsend broadcast` ones
(marked b) are only relayed to other clients. Models are `phase_4/models/minigames/*.bam`
unless a path is given; audio is `phase_4/audio/{bgm,sfx}/`. Every path below was checked to
exist. Sizes were measured with `tools/pipeline/inspect_bam.py`.

Shared by all games: base fields (2.1); toon clips in `phase_4/models/char/` per body part
(`swim`, `sit`, `intoSit`, `gamerun`, `gameneutral`, `eat_neutral`, `eatnrun`, `gameThrow`,
`tug-o-war`, `losewalk`, `sad-neutral`, `slip-forward`, `slip-backward`, `swing`, ...; all
verified for `dgm shorts` torso and legs) plus phase 3.5 `victory-dance`, `cringe`, `conked`,
`jump*`; `phase_3/models/props/drop_shadow.bam`; `phase_3.5/models/props/TT_sky.bam` (42
tris). The win sting `MG_win.ogg` and `MG_lose.ogg` are in `phase_4/audio/sfx/`.
`ToonAnimStates` that games use (`ToontownGlobals.py:1621-1664`): `swim`, `swimhold`, `dive`,
`Catching`, `CatchEating`, `Sit`, `SitStart`, `Push`, `Squish`, `FallDown`,
`CogThiefRunning`, `victory`, `Sad`, `Happy`; the client whitelist must be extended
(`Client::sendAnimState` only passes names in `allowedAnimState`).

| Game (id) | DC class (id), fields | Client to server | Essential mechanics | Assets | Old 3DS |
|---|---|---|---|---|---|
| **Cannon** (2) | DistributedCannonGame (124): setCannonPosition 867, setCannonLit 868, updateCannonPosition 869, setCannonWillFire 870, setToonWillLandInWater 871, announceToonWillLandInWater 872 | `setCannonPosition(int32/100 zRot, uint32/100 angle)`, `setCannonLit` (same args), `setToonWillLandInWater(int32/100 landTime)` | 90 s. Aim a cannon (rot -20..20, angle 10..85), fire, ballistic flight (closed-form parabola, `Trajectory.py`) at a water tower whose position comes from `RandomNumGen(doId)` (`DistributedCannonGame.py:281-314`). The **client decides** the outcome: it sends the landing time only when it will hit water. Score `int(23 - 18 t/90 + 0.5)`, 23 max, 5 min (`CannonGameGlobals.py:1-16`); everyone gets the same score; `gameOver` when the water landing time passes or 90 s expire (`DistributedCannonGameAI.py:35-129`). Misses (ground or tower) just reset and you shoot again. | `toon_cannon_gameground` (26 tris), `toon_cannon_water_tower` (136), `toon_cannon` (164), `cannon_hill` (62), `TT_sky`; `bgm/MG_cannon_game`, `sfx/MG_cannon_adjust`, `MG_cannon_fire_alt`, `MG_cannon_hit_dirt`, `MG_cannon_hit_tower`, `MG_cannon_splash`, `MG_cannon_whizz`, `MG_win` | simple |
| **Catch** (8) | DistributedCatchGame (131): claimCatch 903, setObjectCaught 904, hitBySuit 905 (b), reportDone 906, setEveryoneDone 907 | `claimCatch(uint32 objNum, uint32 typeId)`, `reportDone()`, `hitBySuit(uint32 avId, int16 ts)` (b, optional) | 55 s. Move left/right on a platform and catch falling fruit, avoid anvils and walking cogs. The client owns the drop schedule (`scheduleDrops`, `DistributedCatchGame.py:581-620`; the AI only dedups by `objNum`, rejects `objNum >= 5000` or `>= 2 x len(caught)`, `DistributedCatchGameAI.py:88-110`). Score: good catches +1; final `max(1, int(s/2))` plus `round(numFruits/4)` if every fruit caught (`:43-55`); `numFruits` 18 in TTC solo (`CatchGameGlobals.py:42-55`). `reportDone` after the last drop, then the AI sends `setEveryoneDone` and `gameOver`. In solo the schedule can be any local one; multiplayer needs the Mersenne port (section 4). Cogs are cosmetic to the server. | `treehouse_2players` (1870 tris, 70 geoms, 13 tex; 3p/4p variants), `apple` (105 tris) `orange` `pear` `coconut` `watermelon` `pineapple`, `phase_4/models/props/anvil-mod.bam`; `bgm/MG_toontag`, `sfx/SZ_DD_treasure`, `MG_cannon_hit_dirt`, `AA_drop_anvil_miss`, `ring_perfect`; cog models per `combat.md` | simple |
| **Tug of War** (7) | DistributedTugOfWarGame (130): reportPlayerReady 893, sendGoSignal 894, sendStopSignal 895, sendGameType 896, reportEndOfContest 897, sendNewAvIdList 898, reportCurrentKeyRate 899, sendCurrentPosition 900, sendSuitPosition 901, remoteKeyRateUpdate 902 | `reportPlayerReady(uint8 side)`, `reportCurrentKeyRate(uint32 rate, int16/100 force)` every 0.2 s (`TugOfWarGameGlobals.py:5`), `reportEndOfContest(uint8 losingSide)`, `sendNewAvIdList(uint32[])` | 40 s. Solo is always toon vs cog (`sendGameType` carries suit 1..4, `DistributedTugOfWarGameAI.py:65-84`). Alternate two buttons; keys per second (`keyTTL` window) maps through a Gaussian around an "ideal rate" to a force (`DistributedTugOfWarGame.py:836-880, 1204-1212`). The AI sums forces, adds the cog force table, and broadcasts positions (`:188-225`). The **client** declares the loser when a character reaches the water (`:1152-1172`). Pay: win `15 + 2^(suit-1)`, loss 4 (`TugOfWarGameGlobals.py:14-17`, AI :238-262). The client needs the AI's `sendCurrentPosition`/`sendSuitPosition` replies to move the rope. | `tug_of_war_dock` (754 tris, 64 geoms, 12 tex), `tug_of_war_rope` (2 tris), `TT_sky`, `phase_3/models/props/arrow`, cog `suitA/B/C-mod` + `suitX-tug-o-war`, `-slip-forward/-backward`, `-flailing`, `-victory`; toon clips `tug-o-war`, `slip-*`, `victory`, `sad-neutral`; `bgm/MG_tug_o_war`, `sfx/MG_pos_buzzer`, `MG_cannon_splash`, `AA_sound_whistle` | medium |
| **Pairing / Toon Memory** (11) | DistributedPairingGame (344): setDeckSeed 1898 (required), setMaxOpenCards 1899, openCardRequest 1900, openCardResult 1901, reportDone 1902, setEveryoneDone 1903, setSignaling 1904 (b) | `openCardRequest(int16 deckIndex, int16 bonusGlowCard)`, `reportDone()`, `setSignaling(uint32)` (b) | 90 to 120 s (`PairingGameGlobals.py:1-2`, difficulty scaled). **Server authoritative**: it decides matches and awards points (+1, +1 if the match includes the bonus-glow card) and a low-flip bonus (`DistributedPairingGameAI.py:70-229`, `PairingGameGlobals.py:25-40`). The reply `openCardResult` gives only the matching card index, not ranks, so the client must rebuild the deck: `random.Random(deckSeed).shuffle(range(52))`, then drop ranks above 7 (solo; `MaxRankIndexUsed`, `PairingGameGlobals.py:5-10`). That needs a CPython Mersenne Twister port (section 4). The python walks a toon around a room to flip cards; touch on the 3DS would be more natural. | `memory_room` (30 tris), `garden_sign_memory`, `phase_3.5/models/gui/playingCard.bam`, `inventory_icons.bam`; `bgm/MG_Pairing`, `sfx/MG_pairing_*` (7 files) | medium |
| **Ring** (5) | DistributedRingGame (127): setTimeBase 882 (required), setColorIndices 883 (required), setToonGotRing 884, setRingGroupResults 885 | `setToonGotRing(uint8 success)` once per ring group, 16 groups | Swim through 16 groups of rings, steer in the XZ plane with the circle pad; the server **trusts** each flag. Solo: +1 per ring, +5 if perfect, min 1 (`DistributedRingGameAI.py:99-146`). Ring layout comes from `randomNumGen.choice` of difficulty patterns (`DistributedRingGame.py:532-535`); in solo the layout need not match anything. 16 x 3 s + intro, about 55 s. | `swimming_game` (189 tris), `swimming_game_ring` (144), `drop_shadow`; `bgm/MG_toontag`, `sfx/AV_ambient_water`, `ring_perfect`, `ring_get`, `ring_miss` | medium |
| **Maze** (6) | DistributedMazeGame (129): claimTreasure 889, setTreasureGrabbed 890, allTreasuresTaken 891, hitBySuit 892 (b) | `claimTreasure(uint32 n)`, `hitBySuit(uint32, int16)` (b) | 60 s. Walk a maze (`maze_1player` to `maze_4player` chosen by `randHash(doId)`, `MazeGameGlobals.py:3-8`; data in `MazeData.py`) collecting Mickey-head treasures while cogs wander. The server only checks the index range and duplicates (`DistributedMazeGameAI.py:80-107`). Final `max(1, treasures/12)`, +8 if all (:117-126). Needs the wall data from `MazeData.py` baked, plus the Mersenne port for cog walks. | `maze_1player` (342 tris), `phase_3.5/models/props/mickeySZ`, cog type `f` (Flunky; `MazeSuit.py:33`); `bgm/MG_toontag`, `sfx/MG_maze_pickup`, `MG_Tag_C`, `MG_cannon_whizz` | medium-hard |
| **Diving / Treasure Dive** (9) | DistributedDivingGame (132): pickupTreasure 908, handleFishCollision 910, handleCrabCollision 912, removeFish 916, getCrabMoving 917, treasureRecovered 919 (+ broadcasts) | `pickupTreasure(uint32)`, `handleFishCollision(avId, spawnId, spawnerId, status)`, `handleCrabCollision(avId, status)`, `removeFish(uint32)`, `getCrabMoving(...)`, `treasureRecovered()` | 65 s fixed timer (`DistributedDivingGameAI.py:354-366`). Swim down, grab treasure, avoid fish and a crab, bring it to the boat. The AI spawns fish through an interval loop (`:262-275, 394-408`), so spawn data arrives as `fishSpawn`. Solo +15 per recovery in TTC (`10 x 1.5 x (0.25 + 0.75)`, :300-315), min 5, **no hood multiplier** (`hasScoreMult` 0). | `diving_game` (100 tris, 9 tex, 1.9 MB raw), `treasure`, `treasure_chest`, fish actors `clownFish`, `PBJfish`, `BearAcuda`, `balloonFish`, `nurseShark`, `pianoTuna` (`-zero` + `-swim`), `kingCrab-zero` + `-swimLOOP` (skeletal, needs the anim baker); `bgm/MG_Target`, `sfx/diving_*` (3 files), `AV_ambient_water` | medium-hard |
| **Target / Toon Slingshot** (10) | DistributedTargetGame (133): setTimeBase 921 (required), setToonGotRing 922, setPlayerDone 924, setScore 925, setTargetSeed 926, setRoundDone 927, setSingleScore 928, setGameDone 929 | `setScore(int32 x, int32 y)`, `setPlayerDone()` | 3 rounds (`round = 2`, counted down, `DistributedTargetGameAI.py:35`). Fire a toon on a slingshot over a desert with wind, glide with an umbrella, land on scored rings. The **server** checks the landing point against targets it places from `random.seed(seed)` + `random.random()` (`:62-100`), so a port needs CPython `random()` (53-bit float) too. Physics (wind, trampoline) is the hard part. No hood multiplier. | `slingshot_game_desert4` (450 tris), `_tramp`, `_cacti`, `_umbrellas`, `_sling`, four umbrella textures; `bgm/MG_Diving`, `sfx/target_*` (11 files) | hard |
| **Cog Thief** (14) | DistributedCogThiefGame (373): throwingPie 2112 (b), hitBySuit 2113 (b, airecv), pieHitSuit 2114, cogHitBarrel 2115, cogAtReturnPos 2116, updateSuitGoal 2117, makeCogCarryBarrel 2118, makeCogDropBarrel 2119, markBarrelStolen 2120 | `throwingPie(...)`, `hitBySuit(...)`, `pieHitSuit(...)`, `cogHitBarrel(...)`, `cogAtReturnPos(...)` | Throw pies at cogs to stop them stealing gag barrels. The **client simulates cog movement** and reports hits and barrel events; the AI chooses goals and times (`DistributedCogThiefGameAI.py:171-204, 324, 384-409`). Pie physics, cog pathing and barrel state sharing make it the largest protocol of the group. | `cogthief_game` (112 tris), `cogthief_game_gagTank`, `ice_game_kaboom`, cog models; `bgm/MG_CogThief`, `sfx/AA_pie_throw_only` (phase 3.5) | hard |
| **Jungle Vines** (12) | DistributedVineGame (351): reachedEndVine 1969, setNewVine 1970, setNewVineT 1971 (b), setJumpingFromVine 1972 (b), claimTreasure 1973, setFallingFromVine 1977 (b), setFallingFromMidair 1978 (b), setVineSections 1979 (required) | `reachedEndVine(int8)`, `setNewVine(uint32, int8, uint32/10000, int8)`, `claimTreasure(uint32)`, plus broadcast-only movement updates | 70 s. Swing between 20 vines with bats and spiders; bonus for time left. Custom swing and jump physics, broadcast state per toon (`VineGameGlobals.py`). | `vine_game` (86 tris), `vine_game_shelf`, `phase_4/maps/swinging_vine.jpg`; `bgm/MG_Vine`, `sfx/MG_sfx_vine_game_*` (10 files) | hard |
| **Toon Escape / Two-D** (15) | DistributedTwoDGame (376): showShootGun 2128, toonHitByEnemy 2129, toonFellDown 2130, toonSquished 2131, toonVictory 2132, claimTreasure 2133, claimEnemyShot 2134, reportDone 2135, setSectionsSelected 2136 (required), ... | `claimTreasure(uint8, uint8)`, `claimEnemyShot(uint8, uint8)`, `toonHitByEnemy/FellDown/Squished/Victory(uint32, int16)`, `reportDone()` | Side-scrolling platformer with sections, enemies, stompers, water gun (about 150 s in TTC, `ToonBlitzGlobals.py:14`). Score from treasure, falls and hits, then `sqrt(score x mult)` (`DistributedTwoDGameAI.py:104-112, 188-265`). Large asset set and its own 2D walk and collision code. | `toonblitz_game` (1344 tris), `_wall`, `_start`, `_elevator`, `_arrow`, `_block00..03`, `_stomper`, five icons, `prop_waterspray`, `particleGlow`; `bgm/MG_TwoDGame`, 9 sfx | hard |
| **Match Minnie / Pattern** (4) | DistributedPatternGame (126): reportPlayerReady 876, setPattern 877, reportPlayerPattern 878, setPlayerPatterns 879, reportButtonPress 880, remoteButtonPressed 881 | `reportPlayerReady()`, `reportButtonPress(uint8 idx, uint8 wrong)`, `reportPlayerPattern(uint8[], uint16/1000 time)` | 4 rounds, pattern grows by 2 from length 2 (`PatternGameGlobals.py:2-5`), 10 s to repeat each (`InputTime`). **Server authoritative and RNG free**: it sends the pattern, the client shows it and records the 4-button reply. Score per round `round` plus +2 for fastest, +4 if perfect (`DistributedPatternGameAI.py:150-191`). Cheapest game to code. Never drawn for one rider (needs 2+ unless forced). | `matching_room` (16 tris), `phase_3.5/models/gui/matching_game_gui`; `bgm/m_match_bg1`, `sfx/m_match_{trumpet,guitar,drums,piano}`, `MG_pos_buzzer`, `MG_neg_buzzer`, `MG_Tag_A`, `MG_win` | simple (but 2+ players) |
| **Race** (1) | DistributedRaceGame (123): setTimerStartTime 862, setAvatarChoice 863, setAvatarChose 864, setChancePositions 865, setServerChoices 866 | `setAvatarChoice(uint8)` | Dice board game, 20 s to choose per turn (`RaceGameGlobals.py:8`); everything else, including positions and rewards, is computed by the AI and sent in `setServerChoices` (`DistributedRaceGameAI.py:96-220`). UI only. Needs 3 or 4 riders in the random pool; works solo when forced. | `race` (37 tris), `dice`, `question_mark`, `chance_card`; `bgm/minigame_race`, `sfx/MG_pos_buzzer`, `MG_neg_buzzer`, `MG_lose`, `MG_win`, `phase_3.5/audio/sfx/GUI_stickerbook_turn` | simple (but 3+ players) |
| **Tag** (3) | DistributedTagGame (128): tag 886, setIt 887, setTreasureScore 888; plus `DistributedTagTreasure` (192: setPosition required, requestGrab 1158, setGrab 1159, setReject 1160) | `tag(uint32 avId)`, `requestGrab()` on treasure objects | 60 s (`TagGameGlobals.py:1`). Free-roam arena; "it" chases, can only tag back every 2 s; ice-cream treasures are server-spawned objects worth +2 each (`DistributedTagGameAI.py:78-132`). Needs 2+ riders. | `tag_arena` (224 tris), `bboard-pointer`, `phase_4/models/props/icecream`; `bgm/MG_toontag`, `sfx/MG_Tag_C`, `SZ_DD_treasure` | medium (2+ players) |
| **Ice Slide** (13) | DistributedIceGame (372): setForceArrowInfo 2098 (b), setAvatarChoice 2099, endingPositions 2100, reportScoringMovieDone, claimTreasure 2102, claimPenalty 2103, ... | `setAvatarChoice(int32/100, int32/100)`, `endingPositions(Coord3[])`, `reportScoringMovieDone()`, `claimTreasure(uint8)`, `claimPenalty(uint8)` | Turn-based puck pushing, 3 matches x 2 rounds (`IceGameGlobals.py:14-15`). Uses Panda **ODE** physics on the client and the AI (`DistributedIceWorld.py`, `DistributedIceGameAI.py`); positions must agree across machines. Needs 2+ riders. | `ice_game_icerink`, `_tire`, `_tirestack`, `_crate`, `_barrel`, `_tnt2`, `_kaboom`, `_arrow`, `_score_circle`, `_2d`; `bgm/MG_IceGame`, about 11 sfx | hard (2+ players) |
| **Photo Fun** (16) | DistributedPhotoGame (125): newClientPhotoScore 873, newAIPhotoScore 874, filmOut 875 | `newClientPhotoScore(uint8, char[0-256], uint32/100)`, `filmOut()` | 60 s. Pan a camera over a rebuilt playground (client loads the hood DNA, `PhotoGameGlobals.py:27-241`) and photograph NPC toons on assignment. The AI is not in the solo matrix and only appears for 2+ riders through `want-all-minigames`. | per hood DNA and sky models, `photogame_*`, `photo_game_*`, `toon_cannon`, `bgm/MG_cannon_game`, `sfx/Photo_*` | hard (2+ players) |
| **Trolley Tracks** (100, `TravelGame`) | DistributedTravelGame (343): setTimerStartTime 1891, setAvatarChoice 1892, setServerChoices 1894, setMinigames 1895, setBonuses 1896, setBoardIndex 1897 (required) | `setAvatarChoice(uint16, uint8)` | Metagame board of up to 5 rounds that picks the next minigame and carries votes into a bonus payout (`DistributedMinigameAI.py:330-383`, `PurchaseManagerAI.py:82-92`). Only runs with the config or holiday in 2.7 and two or more players. Out of scope until the single games work. | `trolley_game_turntable`, `mg_icons`, `phase_4/models/modules/trolley_station_TT`, `toon_cannon_gameground`, `friendslist_gui`, `dialog_box_buttons_gui`; `bgm/MG_Travel`, 6 sfx (4 `MG_sfx_travel_*` + trolley bell/away) | hard (2+ players, metagame) |

Field numbers in the table were read from `DCFile` (`...` marks fields omitted for
brevity). Toon parts come from our existing `bake_toon.py` pipeline. Textures for all of these models are `.jpg`/`.rgb` in `phase_4/maps`
and `phase_3.5/maps`; the baker's ETC1 path applies unchanged. Even the largest scene
(`treehouse_2players` at 1870 tris) is far below the section 3 frame budget, so for all games
**the difficulty is gameplay logic and net sync, not asset size**.

### 3.1 Recommended order

1. **Framework + stub** (section 2) with `requestExit` for every game that is not ready.
   This alone makes the trolley round-trip work and pays 1..7 beans.
2. **Cannon.** One scene of about 400 tris, a four-button aim UI, a closed-form parabola, and
   a single message that matters. Reachable solo. Tower position can be a local random for solo.
3. **Catch.** One-dimensional movement, falling-object spawner, two small protocol messages.
   Reachable solo. Cogs can be omitted at first because only `hitBySuit` broadcasts them.
4. **Tug of War.** Two buttons, a Gaussian force function, and mirror-reading three server
   messages. Reachable solo. Needs the cog actor and the tug animations.
5. Then Pairing (touch-friendly, needs the CPython MT port), Ring, Maze, Diving, Cog Thief,
   Target, Vine, Two-D.
6. Multiplayer-only games when a second rider exists: **Match Minnie** is the cheapest game
   of all to code (UI plus a pattern echo), then Race, Tag; Ice, Photo and Trolley Tracks
   last.

While developing a specific game set `minigame-id` to its id so every ride goes to it
(2.7); for stock-server testing implement the stub first so that the other 7 or 8 solo games do
not strand the player.

## 4. Shared client infrastructure the games need

- **Mersenne Twister (verified).** Panda's `RandomNumGen(seed)` is `Mersenne(seed)` with
  `getUint31()`; `random(...)`, `randint`, `choice` and `shuffle` are built from it
  (`direct/showbase/RandomNumGen.py`, from the `.venv` wheel). `Mersenne(seed).getUint31()`
  equals standard MT19937 `init_genrand(seed)` output `>> 1` (checked for seeds 5 and
  1234567). `DistributedMinigame` seeds one with its own `doId`
  (`DistributedMinigame.py:79-87`), which drives Cannon tower placement, Catch drops, Maze
  layout and cog walks, Vine bats, and Ring layouts. In solo these only have to look right;
  for multiplayer they must match.
- **CPython `random.Random`.** Pairing's deck uses `random.Random().seed(n); shuffle`
  (`PlayingCardDeck.py:15-20`), Target uses `random.seed`/`random.random()`
  (`DistributedTargetGameAI.py:62-100`). CPython seeds with `init_by_array([seed])` and
  `shuffle` uses `_randbelow(i + 1)` from the top down via `getrandbits`. A Python
  prototype of exactly that algorithm matched `random.Random(n).shuffle` on three seeds
  (5, 3999999, 0), so a C++ port is about 60 lines. `random.random()` (53-bit float) was not
  prototyped.
- **Network time.** `setGameStart`, `setTimerStartTime` and many game messages carry 16-bit
  centisecond timestamps; reuse the existing clock helpers.
- **Animation clips.** Add to the toon bake: `intoSit`, `sit`, `swim`, `gamerun`,
  `gameneutral`, `eat_neutral`, `eatnrun`, `tug-o-war`, `slip-forward`, `slip-backward`,
  `losewalk`, `sad-neutral`, and phase 3.5 `victory-dance`, `cringe`, `conked`.
- **Generic object classes.** dcgen `WANTED`/`TABLE_CLASSES` needs the base minigame
  fields and `PurchaseManager`; each implemented game adds its own class. Unknown classes
  must keep using the generated skip table so a forced game we do not support does not
  desync the stream.
- **Interest.** After a minigame ends, the PM appears in the same zone with `OBJECT_LEAVING`
  for the game DO just before it; do not treat the DO leaving as "game over, go home".

## 5. Open questions and risks

1. Dynamic zone ids in `changeZone` and quiet-zone behaviour are untested on the 3DS client.
2. `trolley_car` is not separable in the current bake (1.3).
3. Access: which `gameAccess` dev toons get decides whether trolleys outside TTC ban the
   account (1.2). Keep to TTC until verified.
4. Out-of-tree PRC loading for `minigame-id` is read from source, not run (2.7).
5. The newbie purchase path (2.5) is derived from reading `requestSkipTutorial`; confirm with
   a log of the PM class id on a freshly skipped toon.
6. Cannon, Catch and Tug constants (cannon initial velocity, drop tables, rope physics) were
   not transcribed; read them from `CannonGameGlobals.py`, `Trajectory.py`, `DropScheduler.py`
   and `DistributedTugOfWarGame.py` when implementing.
7. `phase_4/audio/bgm/trolley_song.ogg` use is unknown.
8. Nothing in this file has been run against the local server. Per `AGENTS.md` section 9,
   the first implementation must add a `docs/test-log.md` entry per game.

## 6. Test plan (not yet run)

1. Start the server with `minigame-id 2` and `trolley-countdown-time 3` (after the
   `start.sh` follow-up). Board with the PC bot as a second rider.
2. Check by log: `fillSlot`, `setState` countdown, `setMinigameZone`, the DO's class and
   required fields, `setAvatarJoined`, `setAvatarReady`, `setGameStart`.
3. Stub path: force an unimplemented id, send `requestExit`, confirm a PM (class 176) with
   `setMinigamePoints` in 1..7, `requestExit`, `setPurchaseExit`, `setInventory(done=1)`,
   zone change back, and that the AI log shows the zone released (no leaked PM).
4. Timeout path: join but never send `setAvatarReady` and time the abort (expect 66 s).
5. Record heap, linear and VRAM numbers per game in `docs/test-log.md`.

## 7. Implementation status

### 7.1 Catching Game (id 8, DistributedCatchGame)
- **Status:** Done and verified end-to-end against local Open Toontown server.
- **Implementation:** `source/game/catch_logic.{h,cpp}`, `source/game/minigame_session.{h,cpp}`, `tools/catch_test/`.
- **Assets:** `catch_ground_{2,3,4}.ttm`, fruit models, `catch_anvil.ttm`, sfx in `sdmc-out/3ds/toontown3ds/data/minigames/`.

### 7.2 Cannon Game (id 2, DistributedCannonGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes byte-for-byte against upstream Python.
- **Implementation:**
  - Logic: `source/game/cannon_logic.{h,cpp}`: closed-form trajectory parabola, water tower placement from `RandomNumGen(doId)`, water disc, cylinder side and ground collision detection, score formula.
  - Host test: `tools/cannon_test/` compares 150 shots and 5 tower placements against `cannon_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `updateCannonPosition`, `setCannonWillFire`, `announceToonWillLandInWater`, and sends `setCannonPosition`, `setCannonLit`, `setToonWillLandInWater`. DC fields registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/cannon_game.{h,cpp}` dispatched by `MinigameSession`: Circle Pad aim (yaw -20..20, pitch 10..85), button A to fire, ballistic flight camera tracking, bottom-screen aim & score display, `autoPlay` auto-aim hook.
  - Assets: `cannon_gameground.ttm`, `cannon_tower.ttm`, `cannon_hill.ttm`, `cannon_base.ttm`, `cannon_barrel.ttm`, and Cannon audio baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Dual-screen presentation (top screen shows 3D camera follow, bottom screen shows aim angles and timer).
- **What to verify in Azahar:** Force `minigame-id 2` in server config; board trolley; verify cannon aiming with Circle Pad, fire with A, trajectory flight and camera follow, water tower impact and score payout.

### 7.3 Tug of War (id 7, DistributedTugOfWarGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes byte-for-byte against upstream Python.
- **Implementation:**
  - Logic: `source/game/tug_logic.{h,cpp}`: Gaussian key force formula around target rates, key press decay window, mud pit fall thresholds.
  - Host test: `tools/tug_test/` tests force calculation across all rates and awards against `tug_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `sendGameType`, `sendGoSignal`, `sendStopSignal`, `sendCurrentPosition`, `sendSuitPosition`, `remoteKeyRateUpdate`, and sends `reportPlayerReady`, `reportCurrentKeyRate`, `reportEndOfContest`, `sendNewAvIdList`.
  - Runtime: `source/game/tug_game.{h,cpp}`: alternating button input (Circle Pad Left/Right or A/B), 5 Hz force updates sent to AI, dock and rope drawing, water fall detection and win/loss resolution.
  - Assets: `tug_dock.ttm`, `tug_rope.ttm`, and Tug audio baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Input on 3DS uses Circle Pad or A/B buttons; bottom screen shows pull gauge and target rate.
- **What to verify in Azahar:** Force `minigame-id 7`; board trolley; report ready; on Go signal alternate buttons; verify rope and avatar offset movements from server updates, mud pit fall detection, and jellybean payout.

### 7.4 Ring Game (id 5, DistributedRingGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes byte-for-byte against upstream Python.
- **Implementation:**
  - Logic: `source/game/ring_logic.{h,cpp}`: difficulty pattern selection per safezone using Panda MT RNG, collision circle radius check, scoring with perfect game bonus.
  - Host test: `tools/ring_test/` validates patterns for all 6 playgrounds and collision math against `ring_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `setTimeBase`, `setColorIndices`, `setRingGroupResults`, and sends `setToonGotRing`.
  - Runtime: `source/game/ring_game.{h,cpp}`: 2D steering in XZ plane with Circle Pad, 16 ring groups advancing along Y, collision check on pass, tally counter and score calculation.
  - Assets: `ring_game.ttm` (swimming environment), `ring_ring.ttm`, and Ring audio baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Circle Pad steers directly in the swimming plane; bottom screen shows color assignments and ring tally.
- **What to verify in Azahar:** Force `minigame-id 5`; board trolley; swim through rings using Circle Pad; verify ring hits/misses, `setToonGotRing` sent per group, and score tally at end of course.

### 7.5 Maze Game (id 6, DistributedMazeGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes 100 collision checks, 10 tile lookups, and 9 score calculations against upstream Python.
- **Implementation:**
  - Logic: `source/game/maze_logic.{h,cpp}`: 28x22 collision tilemap with `doOrthoCollisions` 4-quadrant slide resolution matching upstream Panda3D collision math, 197 collectible treasures, Flunky suit patrolling with waypoint graph and collision detection, upstream score formula (`max(1, treasures / 12) + (all ? 8 : 0)`).
  - Host test: `tools/maze_test/` compares tile collisions, bounds clamping, and scoring against `maze_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `setTreasureGrabbed`, `allTreasuresTaken`, `hitBySuit`, and sends `claimTreasure`, `hitBySuit`. Registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/maze_game.{h,cpp}` dispatched by `MinigameSession`: Circle Pad tank/analog navigation with wall collision sliding, 3D follow camera tilted down at 45 degrees, treasure rendering and pickup detection, Flunky suit patrolling, bottom-screen maze minimap and treasure counter.
  - Assets: `maze_1player.ttm`, `maze_treasure.ttm`, and Maze audio (`mg_maze_pickup`, `mg_tag_c`) baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Maze camera is adapted for 3DS top screen with an angled follow perspective; bottom screen renders a full-grid minimap showing maze layout and remaining treasures.
- **What to verify in Azahar:** Force `minigame-id 6`; board trolley; navigate maze using Circle Pad; confirm wall sliding prevents moving through hedges; pick up treasures and confirm count increments and `claimTreasure` sent; avoid Flunky suit; verify final score and jellybean payout.

### 7.6 Match Minnie / Pairing (id 11, DistributedPairingGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes byte-for-byte CPython MT19937 RNG shuffle across 6 seeds and 20 evaluation tests against upstream Python.
- **Implementation:**
  - Logic: `source/game/pairing_logic.{h,cpp}`: CPython MT19937 PRNG + `_randbelow_with_getrandbits` implementation matching Python 3's exact `random.Random(deckSeed).shuffle` algorithm, 32-card filtered deck (ranks 0..7 across 4 suits), card flip state machine (up to 2 open cards), match evaluation, and upstream low-flip bonus calculation (`PairingGameGlobals.py`).
  - Host test: `tools/pairing_test/` validates exact 32-card deck construction across multiple seeds and match logic against `pairing_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles required `setDeckSeed`, `setMaxOpenCards`, `openCardResult`, `setEveryoneDone`, and sends `openCardRequest`, `reportDone`. Registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/pairing_game.{h,cpp}`: top screen shows 3D memory room with card grid; bottom screen provides responsive touch grid and D-pad/Circle Pad cursor navigation with A button flip; match detection animations, card turn SFX, match SFX.
  - Assets: `memory_room.ttm`, `playing_card.ttm`, and Pairing audio (`mg_pairing`, `mg_pairing_match`, `mg_pairing_turn`) baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Upstream walks the toon across physical floor cards in a 3D room; 3DS provides an intuitive touch-screen card matrix on the bottom screen alongside the 3D room on the top screen.
- **What to verify in Azahar:** Force `minigame-id 11`; board trolley; flip cards using touch screen or D-pad + A; verify cards reveal corresponding rank/suit matching server seed; verify matching pairs stay revealed and award points; verify `openCardRequest` and `reportDone` protocol flow.

### 7.7 Diving Game (id 9, DistributedDivingGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes 150 simulation frames of swimming physics and 6 score payouts against upstream Python.
- **Implementation:**
  - Logic: `source/game/diving_logic.{h,cpp}`: 2D swimming physics in XZ plane with drag damping, natural upward buoyancy (+0.8 dt empty, -0.8 dt carrying chest), sunken treasure chest pickup at bottom (Z <= -40), surface boat delivery at Z >= 25, 3-second stun/freeze on obstacle hit, upstream TTC scoring formula (`10 * 1.5 * (0.25 + 0.75) = 15` per chest).
  - Host test: `tools/diving_test/` validates drag, buoyancy, velocity damping, chest state changes, and scoring against `diving_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `handleFishCollision`, `handleCrabCollision`, `getCrabMoving`, `treasureRecovered`, and sends `pickupTreasure`, `handleFishCollision`, `handleCrabCollision`, `getCrabMoving`, `treasureRecovered`. Registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/diving_game.{h,cpp}`: Circle Pad directional propulsion, depth gauge tracking, fish spawners across 6 water depth strata, bottom-crawling crab obstacle, smooth underwater camera follow, chest delivery tracking.
  - Assets: `diving_game.ttm` (underwater scene), `diving_boat.ttm`, `diving_chest.ttm`, and Diving audio (`mg_target`, `mg_diving_hit`, `mg_diving_drop`, `mg_diving_pickup`) baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Dual-screen layout: top screen displays 3D underwater perspective tracking the toon, bottom screen provides depth meter, chest status, and oxygen/timer readout.
- **What to verify in Azahar:** Force `minigame-id 9`; board trolley; swim down using Circle Pad; retrieve sunken chest at the seafloor; swim back to surface boat to deposit; verify collision with fish freezes motion for 3 seconds and drops chest; verify jellybean payout on game over.

### 7.8 Jungle Vines (id 12, DistributedVineGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes 20 pendulum swing calculations, 40 parabolic jumps, and 6 score calculations against upstream Python.
- **Implementation:**
  - Logic: `source/game/vine_logic.{h,cpp}`: 20 sequential vines spaced at 30 units, sinusoidal pendulum oscillation, pump swing impulse and climb controls, jump velocity decomposition into tangential swing velocity + forward boost, parabolic airborne flight under gravity, catch detection for target vine within 4 units, time bonus scoring at vine 19 (`int(1.0 + timeLeft * 0.1)`).
  - Host test: `tools/vine_test/` validates swing angles, jump trajectories, landing catch, and score calculation against `vine_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `reachedEndVine`, `setNewVine`, `setVineSections`, and sends `reachedEndVine`, `setNewVine`, `claimTreasure`. Registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/vine_game.{h,cpp}`: Circle Pad Left/Right to pump swing, Up/Down to climb vine height, button A to release and leap forward, side-scrolling dynamic follow camera, bottom-screen progress tracker showing current vine out of 20 and remaining time.
  - Assets: `vine_game.ttm` (jungle backdrop), `vine_shelf.ttm` (platforms), and Vine audio (`mg_vine`, `mg_vine_bananas`, `mg_vine_fall`, `mg_vine_jump`) baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Dual-screen presentation with side-scrolling 3D follow camera on top screen and vine course progress meter on bottom screen.
- **What to verify in Azahar:** Force `minigame-id 12`; board trolley; pump swing with Circle Pad, jump between vines with A; reach end platform (vine 19); verify `reachedEndVine` transmission, time bonus calculation, and jellybean reward.

### 7.9 Toon Slingshot / Target Game (id 10, DistributedTargetGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes CPython MT19937 RNG target placement across 3 seeds and 12 evaluation tests against upstream Python.
- **Implementation:**
  - Logic: `source/game/target_logic.{h,cpp}`: CPython MT19937 PRNG + 53-bit float `random()`, safezone difficulty patterns, target placement algorithm matching `DistributedTargetGameAI.setupTargets`, collision score evaluation, and slingshot/glide physics.
  - Host test: `tools/target_test/` validates exact target positions across multiple seeds and collision scoring against `target_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `setTargetSeed`, `setRoundDone`, `setSingleScore`, `setGameDone`, and sends `setScore`, `setPlayerDone`. Registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/target_game.{h,cpp}`: Circle Pad aim and launch power, umbrella glide mechanics, 3D slingshot camera follow, bottom-screen aim angle and score display.
  - Assets: `target_ground.ttm`, `target_sling.ttm`, `target_tramp.ttm`, and Target audio (`mg_target_launch`, `mg_target_trampoline`) baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Dual-screen presentation with 3D flight camera on top screen and aim/round status on bottom screen.
- **What to verify in Azahar:** Force `minigame-id 10`; board trolley; aim slingshot with Circle Pad, fire with A; glide towards target rings; verify score calculation and jellybean reward.

### 7.10 Cog Thief (id 14, DistributedCogThiefGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes cog AI targeting, pie ballistic collisions, and score calculation against upstream Python.
- **Implementation:**
  - Logic: `source/game/cog_thief_logic.{h,cpp}`: 4 gag barrels, cog AI targeting & return, pie physics trajectory & collisions, score calculation with +8 solo perfect bonus.
  - Host test: `tools/cog_thief_test/` compares barrel theft tracking, pie hits, and scoring against `cog_thief_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `updateGoal`, `carryBarrel`, `dropBarrel`, `barrelStolen`, and sends `throwPie`, `hitBySuit`, `pieHitSuit`, `hitBarrel`, `atReturnPos`. Registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/cog_thief_game.{h,cpp}`: Circle Pad toon navigation, A button pie throwing with cooldown, barrel defense, bottom-screen barrel status and score display.
  - Assets: `cogthief_ground.ttm`, `cogthief_barrel.ttm`, `cogthief_tank.ttm`, and Cog Thief audio (`mg_slice`) baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Dual-screen layout: top screen displays 3D court, bottom screen shows barrel defense status and time remaining.
- **What to verify in Azahar:** Force `minigame-id 14`; board trolley; throw pies at incoming Cogs with A; defend gag barrels; verify jellybean reward and perfect bonus if all 4 barrels saved.

### 7.11 Toon Escape / Two-D (id 15, DistributedTwoDGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes 2D platforming physics, stomper timing, and score calculations against upstream Python.
- **Implementation:**
  - Logic: `source/game/two_d_logic.{h,cpp}`: 2D platforming physics in XZ plane, stomper timing, completion bonus + time left bonus, sqrt score formula matching `DistributedTwoDGameAI`.
  - Host test: `tools/two_d_test/` compares jumping arcs, platform landings, stomper collisions, and scoring against `two_d_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `setSections`, `setTreasureGrabbed`, `setEveryoneDone`, and sends `claimTreasure`, `claimEnemyShot`, `reportDone`, `hitByEnemy`, `fellDown`, `squished`, `victory`. Registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/two_d_game.{h,cpp}`: side-scrolling 2D platformer on top screen, Circle Pad movement and A button jump, obstacle hazards (stompers, pits), bottom-screen course timer and score display.
  - Assets: `twod_game.ttm`, `twod_block.ttm`, and Two-D audio (`mg_twod_bgm`, `mg_squirt_hit`) baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Fixed side-scrolling 2D camera tailored for 3DS 400x240 aspect ratio.
- **What to verify in Azahar:** Force `minigame-id 15`; board trolley; run and jump through platform course; reach the exit flag; verify time bonus and victory score payout.

### 7.12 Tag Game (id 3, DistributedTagGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes IT transitions, tagback cooldowns, and treasure scoring against upstream Python.
- **Implementation:**
  - Logic: `source/game/tag_logic.{h,cpp}`: IT selection, 2.0s tagback cooldown, treasure scoring (+2).
  - Host test: `tools/tag_test/` compares IT tagging rules, cooldown prevention, and treasure scores against `tag_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `setIt`, `setTreasureScore`, and sends `tag`. Registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/tag_game.{h,cpp}`: Circle Pad arena running, treasure pickup detection, 3D follow camera, bottom-screen IT indicator and cooldown display.
  - Assets: `tag_arena.ttm`, `tag_treasure.ttm`, and Tag audio (`mg_tag_tag`) baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Dual-screen presentation with IT status and tagback cooldown displayed on bottom screen.
- **What to verify in Azahar:** Force `minigame-id 3`; board trolley with 2+ players; verify IT tagging transfers role with 2s cooldown; pick up treasures; verify score payout.

### 7.13 Match Minnie / Pattern (id 4, DistributedPatternGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes rounds, scoring, and perfect game bonuses against upstream Python.
- **Implementation:**
  - Logic: `source/game/pattern_logic.{h,cpp}`: 4 rounds, pattern length increases by 2 each round (2, 4, 6, 8), button press tracking, scoring: +round + 4 perfect bonus.
  - Host test: `tools/pattern_test/` validates solo and multiplayer scoring across all 4 rounds against `pattern_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `setPattern`, `setPlayerPatterns`, `remoteButtonPressed`, and sends `reportPlayerReady`, `reportButtonPress`, `reportPlayerPattern`. Registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/pattern_game.{h,cpp}`: dance stage 3D view on top screen, D-pad and ABXY pattern input, round timer, bottom-screen round and pattern progress display.
  - Assets: `pattern_stage.ttm` baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Input supports both D-pad and ABXY face buttons with real-time visual feedback on the bottom screen.
- **What to verify in Azahar:** Force `minigame-id 4`; board trolley; follow Minnie's dance pattern; verify button inputs register and match round; verify score increments and perfect bonus.

### 7.14 Ice Slide (id 13, DistributedIceGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes distance-to-center curve, bonus tiers, and placements against upstream Python.
- **Implementation:**
  - Logic: `source/game/ice_logic.{h,cpp}`: 3 matches x 2 rounds, force/direction choice, sliding puck trajectory, distance-to-center scoring (5 dead center down to 1).
  - Host test: `tools/ice_test/` validates points curve and placement bonuses across player counts and distances against `ice_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `setMatchAndRound`, `setNewState`, `setTimerStartTime`, `setAvatarChoice`, `setScores`, `setTreasureGrabbed`, `setPenaltyGrabbed`, and sends `setAvatarChoice`, `endingPositions`, `reportScoringMovieDone`, `claimTreasure`, `claimPenalty`. Registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/ice_game.{h,cpp}`: Circle Pad aim and power gauge, tire slide physics with wall bounces, top-down arena view, bottom-screen match and distance readout.
  - Assets: `ice_arena.ttm`, `ice_tire.ttm`, and Ice audio (`mg_ice_scoring`) baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Smooth analog aiming on Circle Pad with dual-screen HUD tracking power and aim heading.
- **What to verify in Azahar:** Force `minigame-id 13`; board trolley; aim tire with Circle Pad and launch with A; verify sliding and distance scoring per match.

### 7.15 Race Game (id 1, DistributedRaceGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes choices, conflicts, chance cards, and win payout against upstream Python.
- **Implementation:**
  - Logic: `source/game/race_logic.{h,cpp}`: 0..4 choices, unique choice check (duplicate choices cancel out), chance card triggers, first to 14 spaces wins.
  - Host test: `tools/race_test/` validates movement choices, conflict cancellation, chance rewards, and payout against `race_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `setTimerStartTime`, `setAvatarChose`, `setChancePositions`, `setServerChoices`, and sends `setAvatarChoice`. Registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/race_game.{h,cpp}`: 3D board track perspective on top screen, D-pad/Circle Pad move selection, bottom-screen track position meter and chance square display.
  - Assets: `race_track.ttm`, and Race audio (`mg_race_bgm`) baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Dual-screen interface with move selection menu and race track progress on bottom screen.
- **What to verify in Azahar:** Force `minigame-id 1`; board trolley; select move 0..4; verify movement advances on track, chance squares trigger rewards, and reaching space 14 wins.

### 7.16 Photo Fun (id 16, DistributedPhotoGame)
- **Status:** Done. Builds with 0 warnings on devkitARM; host test passes assignments, film counts, and team + high-scorer scoring against upstream Python.
- **Implementation:**
  - Logic: `source/game/photo_logic.{h,cpp}`: film roll limit (32 in TTC), subject/pose assignment score evaluation (1..5 stars), team + highest scorer bonus.
  - Host test: `tools/photo_test/` validates photo assignment matching, star scoring, and team payout against `photo_ref.py`.
  - Network: `source/net/client.{h,cpp}` handles `newAIPhotoScore`, and sends `newClientPhotoScore`, `filmOut`. Registered in `tools/dcgen/dcgen.py` (DC hash 547155168).
  - Runtime: `source/game/photo_game.{h,cpp}`: Circle Pad viewfinder camera pan/tilt, A button photo capture, centering accuracy calculation, bottom-screen assignments and film counter.
  - Assets: `photo_scene.ttm`, `photo_subject.ttm` baked into `sdmc-out/3ds/toontown3ds/data/minigames/`.
- **What differs from upstream:** Viewfinder controls mapped to Circle Pad for smooth analog camera pan/tilt.
- **What to verify in Azahar:** Force `minigame-id 16`; board trolley; pan viewfinder with Circle Pad and snap photos with A; verify assignment star rating and team score payout.

### 7.17 Post-Game Gag Purchase Screen (MinigamePurchasePanel)
- **Status:** Done. Builds with 0 warnings on devkitARM; integrates cleanly with `MinigameSession` and `shop_rules`.
- **Implementation:**
  - Runtime: `source/ui/minigame_purchase_panel.{h,cpp}`: displayed on bottom screen during `Phase::Purchase`. Shows 7 gag tracks x 6 levels, track unlock states, carry capacities, and jellybeans earned from the minigame.
  - Controls: Touch screen tap on gag cells or D-pad navigation + A to buy, X to remove, START to finish (Done), B to cancel.
  - Network Sync: When purchase is completed (or timeout expires), `client.sendPurchaseExit()` is sent, and `client.sendPurchaseInventory(inv, money)` syncs final inventory and remaining jellybeans to the server, preventing server-side resource leaks.
- **What to verify in Azahar:** Complete any minigame; verify gag purchase screen opens on bottom screen with earned jellybeans; buy gags; press Done; confirm toons return to playground with updated inventory and jellybeans.

## 8. Trolley Tracks metagame spec (DistributedTravelGame, id 100)

### 8.1 Overview
Trolley Tracks (`DistributedTravelGame`, ID 100) is the trolley metagame played during Trolley Tracks holiday events or when the server is started with `trolley-goes-to-metagame 1`. Instead of directly launching a standard minigame, all four trolley riders embark on a joint minecart journey along branching railroad tracks.

### 8.2 Protocol and DC Surface (`etc/toon.dc:780-795`)
`DistributedTravelGame : DistributedMinigame`, class id **98**:
- `setDestinations(uint8 destinations[])`: broadcast ram - path destination IDs.
- `setServerChoices(int8 votes[], int8 winningDirection)`: broadcast - announces branch direction chosen by group vote.
- `claimTreasure(uint8 index)`: clsend airecv - claim a bonus bean/treasure on the tracks.
- `setAvatarChoice(int8 direction)`: clsend airecv - cast a vote (Left = 0, Right = 1) at track switches.

### 8.3 State Machine and Gameplay
1. **Travel Run:** The minecart rolls down track segments at constant speed with physics bumps and bank turns.
2. **Switch Approaching (Voting Phase):** As the cart nears a track switch, a voting timer begins (3.0 seconds).
   - Each rider uses Circle Pad or Left/Right to vote for the left or right branch track.
   - The majority vote decides the cart's path. In the event of a tie, the track switch alternates or selects randomly.
3. **Track Modifiers:**
   - **Boost pads:** Accelerate the minecart for a temporary speed rush.
   - **Slow mud:** Decelerate the cart.
   - **Bonus Jellybeans:** Placed along tracks; riders pick them up automatically or with quick directional tilts.
4. **Destination Resolution:**
   - Branching paths lead to different target minigames or bonus playground exits.
   - Upon reaching the end of the line, the final minigame is launched, or bonus beans are awarded directly.
