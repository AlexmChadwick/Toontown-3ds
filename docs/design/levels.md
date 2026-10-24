# Facility levels (factories now; mints, DA offices and country clubs next)

Upstream facilities are `otp/level` levels: a spec (a Python dict of entities) plus a model
whose `zone<N>` nodes are the spec's zone entities. The AI builds the distributed entities
(doors, buttons, stompers, lifts, barrels, goons, Cogs, battle blockers); the client builds
the local ones (static models, gears, platforms, conveyor belts, prop spinners, paths, zones)
and runs visibility. This client does the same with no Python at runtime:

| Piece | Offline (tools/pipeline) | Runtime |
|---|---|---|
| geometry | `bake_level.py`: the level model as **TZ v2**, one chunk per spec zone, vis lists from the zone entities, static `model` entities merged, lift guards and `Prop_*` spinner nodes split out | `world/zone.*` (one `Zone` per level) |
| spec | `bake_level.py` + `tlv.py`: every entity with its world transform and attributes, synthetic `cogSpec` / `reserveCogSpec` / `battleCell` / `spinProp`, cog paths in world space, path points | `world/level_spec.*` (`LevelSpec`) |
| props | `bake_level_props.py`: door, button, stomper (2 styles), lift, barrels + labels, gears, paint mixer, conveyor tread, goons (per hat, 3 clips), radar cone, sounds | `world/level.*` |
| network | `tools/dcgen`: `DistributedFactorySuit` (+Mint/Stage), `BattleBlocker`, door/button/stomper/lift/barrel/goon fields | `net/client_level.cpp` |

All files live in `sdmc:/3ds/toontown3ds/data/level/` (`<level>.tz`, `<level>.tlv`,
`props/*`); sounds in `data/audio/`.

## Zones and visibility (otp/level/DistributedLevel)

- **Zone ids.** `setZoneIds` lists the AI's zone ids in sorted spec zone-number order
  (`LevelMgr.privAssignZoneIds`). TZ chunk/floor zones are the zone number + 1000
  (`ZONE_BASE`); the branch is 999.
- **Visibility.** The zone under the toon (floor reach 200, like the unbounded camera ray
  onto the zone floors) gives the visible set. `Client::setLevelVisibility` keeps the
  location at the level zone and sets the interest to `[levelZone, uberZone, visible…]`
  (`setVisibility` / `sendSetZoneMsg`).
- **Visibility extenders.** A door that is not closed extends its zone with its extenders'
  `newZones` (`Zone::setExtraVisible`).
- **Battles.** A battle locks its zone into view (`lockLevelViz`).

## Entities

| Entity | Upstream | Here |
|---|---|---|
| entrance | `EntrancePoint.placeToon` (radius, theta, toon index) | `Level::spawnPose` |
| Cogs | `DistributedFactorySuit`: path walk (velocity 3, 1 s turns), chase on `setConfrontToon` (4/s, re-aim each 1 s, return after 3 s), `setAlert` (r 15, dZ < 8), `setStrayed` (> 10), `requestBattle` in parent space | `Level::update`; drawn by `SuitActors::setExternal` (skelecogs: robot bodies, ties) |
| battle blockers | `BattleBlocker.startBattle` (sphere `radius`) | start or join the cell's battle |
| level battles | `DistributedLevelBattle.setBattleCellId` (cell spec pos, parent heading) | `Client::setLevelBattlePlacer` |
| doors | `DistributedDoorEntity` (FourState 5/1/6 s, outer then inner halves ±8, three lock slides) | parts drawn, `requestOpen` on contact, solid while closed |
| buttons | `DistributedButton` (r 0.5 trigger, sinks 0.2 and turns green) | `requestInteract` / `requestExit`, floor top |
| stompers | `DistributedStomper` (six motions, `checkSquashedToon`) | solid head, squish → `setOuch` + `Toon.enterSquish` |
| lifts | `DistributedLift` (easeInOut to the arrival time, board sides, guards) | carries riders, `setAvatarEnter` / `Leave` |
| barrels | `DistributedGag/Bean/HealBarrel` (tube, r 3.2 sphere, labels) | `requestGrab`; label via `PyRandom` = `BarrelBase.getRng` |
| gears | `GearEntity` | disc (vertical) / box solids, spin |
| platforms | `PlatformEntity` / `PaintMixer` | carry riders |
| conveyor | `ConveyorBelt` | treads, carries at `speed` |
| props | `PropSpinner` | spin `Prop_*` nodes |
| goons | `DistributedGoon` (path, 3 s turns, radar cone, head stun, movies) | zap → `setOuch(strength)`, stun on landing |
| the end | `setDefeated` → `FactoryInterior.handleFactoryWinEvent` | teleport to the hood (11500 → 11000) |

**Dynamic solids.** Doors, buttons, stompers, lifts, barrels, gears, platforms, belts and
goons are boxes (or discs) in an entity's space, rebuilt each frame *before* the toon moves.
`Player::setDynamic` runs them in three steps:

1. A push out of walls, which then slides against the static world.
2. Floors under the feet, standing on one setting Z directly like `CollisionHandlerFloor`.
3. Carry from the motion of the solid stood on.

## Not done yet

- crates (`DistributedCrate` / grids)
- crusher cells
- cut scenes
- the foreman confrontation message
- ambient sounds
- Mint / stage / country-club floors

Since `MintLayout` / `StageLayout` / `CountryClubLayout` are deterministic from
`random.Random(id * floor)`, the plan is to assemble each floor offline (as `gotAllRooms`
does) into one TZ + TLV, with room-tagged entities and per-room zone-id tables.

## Verify in Azahar (`AI_DEV=1`; `AI_DEV_STRONG=1` for the foreman)

`AUTOTEST_ONLY="go factory,factory: …"` runs the factory steps:

- board
- button
- stomper
- lift
- barrel
- gear
- mixer
- conveyor
- door
- goon zap
- goon stun
- entities
- battle
- foreman

See the "factory" entries in `docs/test-log.md`.
