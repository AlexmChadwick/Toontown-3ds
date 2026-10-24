# Cog buildings

This client treats a Cog building as a server-owned state machine layered over the
street's existing DNA.  The wire declarations are in
`ref/open-toontown/etc/toon.dc:1597-1634` and the generated ids are emitted in
`source/net/dc_generated.h:1014-1162`.

## Building takeover

`DistributedBuilding` sends `setBlock(uint16,uint32)`, `setSuitData(int8,int8,int8)`,
`setVictorList(uint32[])`, and `setState(string,int16)` (`toon`, `suit`,
`becomingSuit`, or `becomingToon`).  The client retains the block, department/level
data, victor ids, and the timestamp in `net::CogBuilding`; `world::CogBuildings`
uses the block as the stable anchor and draws the department shell only in `suit`
state.  The normal landmark remains in the street `.tz`; the shell is intentionally
drawn at the same anchor and is the current non-destructive occlusion strategy.

## Elevator and hand-off

`DistributedElevator` declares `setBldgDoId`, `setState`, eight `fillSlot` and
`emptySlot` pairs, `requestBoard`, `requestExit`, and `setElevatorTripId`
(`toon.dc:1906-1930`).  `CogBuildings::update` applies the same sphere-style
contact rule as the trolley and sends `requestBoard` once per contact.  The server
owns the countdown and slot assignment.  When the interior object is generated,
the client acknowledges `setAvatarJoined`; `elevatorDone` is sent after the
interior elevator state, and `reserveJoinDone` after reserve joining.

## Interior floors

`DistributedSuitInterior` supplies `setZoneId`, `setExtZoneId`, `setDistBldgDoId`,
`setNumFloors`, `setToons`, `setSuits`, and `setState`
(`toon.dc:1623-1637`).  The client stores the lists and state in
`net::CogInterior`, loads the reusable phase-7 floor module, and treats the
server-provided zone as dynamic: no `Place` file is required.  The authoritative
sequence is `WaitForAllToonsInside -> Elevator -> Battle -> ReservesJoining ->
BattleDone -> Resting -> Reward -> Off`, matching
`ref/open-toontown/toontown/building/DistributedSuitInteriorAI.py:76-188`.

## Battles and endings

`DistributedBattleBldg` inherits `DistributedBattleBase` without extra DC fields
(`toon.dc:911-918`).  It therefore reuses `world::Battle`'s member, movie,
experience, and reward protocol; the building-specific camera and floor reward
presentation remain a follow-up visual layer.  After the final floor, the AI sends
the reward state and later removes the interior/elevator objects.  The client then
returns control to the normal street and the server places the toon beside the
building door.  `DistributedBuildingAI` schedules the return transition in
`ref/open-toontown/toontown/building/DistributedBuildingAI.py:380-404`.

## Assets

`tools/pipeline/bake_cogbldg.py` reads the stock phase-7 suit interior modules and
phase-11 Lawbot elevator, writing `sdmc-out/3ds/toontown3ds/data/cogbldg/`.
The baker emits `shell_{c,l,m,s}.ttm`, `elevator.ttm`, `floor_0.ttm` through
`floor_2.ttm`, and `manifest.json`; it fails if a file exceeds the 15,000-triangle
or 150-batch envelope from `AGENTS.md:130-134`.

## Playable interior path

Each generated floor also has a TTC collision file. On `DistributedSuitInterior`
creation the client acknowledges the avatar, places the toon at the elevator
entrance, and swaps the player to the floor collision while preserving the normal
follow camera. Floor state changes reload the room; the first server-provided suit
is offered to the shared battle state machine, and elevator/reserve acknowledgements
remain one-shot. The server owns the floor reward and street exit transition.

Autotest labels are `cog_floor`, `cog_battle`, and `cog_exit`.
