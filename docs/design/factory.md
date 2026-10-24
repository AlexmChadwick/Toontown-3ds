# Sellbot Factory

The client treats the Sellbot Factory as a dynamic level, not as a normal
interior. The exterior elevator is in Sellbot HQ factory zone 11200; the
server's `DistributedFactoryElevatorExt` publishes the destination with
`setFactoryInteriorZone` (`etc/toon.dc:1983-1987`). Boarding the elevator
therefore hands the toon to the dynamic factory zone rather than a baked
`Place`.

## Level and offline spec

`DistributedFactory` inherits `DistributedLevel` and supplies the factory id,
active suit ids, foreman-confronted id, and defeated flag
(`etc/toon.dc:2185-2190`). `DistributedLevel` supplies the level zone, player
ids, entrance, zone-id list, start timestamp, and the level-spec request/deny
fields (`etc/toon.dc:1566-1577`). Upstream obtains the spec in
`toontown/coghq/DistributedFactory.py:68-96` and constructs a `LevelSpec` from
`FactorySpecs.getFactorySpecModule` rather than reading a model at runtime.

`SellbotLegFactorySpec.py:3-40` defines the global level manager and the first
factory zones; its later records define doors, buttons, crates, goons, and
stomper cells (for example buttons at lines 1067-1129, crates at 1209-1236,
doors at 1279-1320, and goons at 2320-2360). The baker freezes the required
entity subset into `level_spec.bin`: `FCT1`, version `uint16`, count `uint16`,
then records `{entityId:uint32, zone:uint16, type:uint8, reserved:uint8,
x/y/z:float32}`. Python is used only during baking; the native client consumes
the generated binary/assets.

The source visibility graph is the `visibility` list on each zone record
(`SellbotLegFactorySpec.py:25-40`). The client keeps the factory model and
entity table in a dynamic manager; the per-zone `cell_*.ttm` files preserve a
stable cell contract for future streaming and are generated from the same
authored phase-9 scene.

## Entities and playthrough

The relevant entity families are registered by `FactoryEntityCreator`
(`FactoryEntityCreator.py:10-49`): doors, lifts, buttons, crates, goons,
stompers, active/crusher cells, battle blockers, and suit groups. Network
state comes from `DistributedEntity`/`DistributedInteractiveEntity`
(`etc/toon.dc:1579-1591`) plus the concrete protocols:

* `DistributedLift` reports state transitions and accepts avatar enter/leave
  (`etc/toon.dc:2252-2256`).
* `DistributedDoorEntity` reports locks/door state and accepts `requestOpen`
  (`etc/toon.dc:2258-2262`).
* `DistributedCrate` reports push/move/crushed state and accepts `requestPush`
  (`etc/toon.dc:2321-2327`).
* `DistributedGoon` reports patrol/movie state and accepts battle/stun/resync
  requests (`etc/toon.dc:2334-2340`).
* `DistributedStomper` reports movie timing and actor ids
  (`etc/toon.dc:2284-2286`).

The runtime draws the baked factory and entity props, applies network positions
and state transitions, requests nearby interactions, and preserves the entity
ids/zones needed for visibility and battle-cell handoff. Factory suit groups
are supplied by `DistributedFactory.setSuits`; their individual suit objects
use the existing Cog/battle renderer.

## Battles and finish

Factory battle objects are `DistributedBattleFactory`, a subclass of
`DistributedLevelBattle` (`etc/toon.dc:920-922`). The client reuses the
`world::Battle` protocol and recognizes subclasses through inherited battle
fields. Upstream adds a `FactoryReward` state and sends the factory reward
completion through `DistributedBattleFactory.py:19-48`; the foreman encounter
is announced by `DistributedFactory.setForemanConfronted`, and completion by
`setDefeated` (`DistributedFactory.py:48-55`). The factory elevator's
interior extension also exposes the normal building-exit methods
(`etc/toon.dc:1983-1987`), so the client returns to the exterior when the
server sends the exit/zone transition.

## Baked assets

`tools/pipeline/bake_factory.py` writes:

* `sdmc:/3ds/toontown3ds/data/factory/factory.ttm`
* `sdmc:/3ds/toontown3ds/data/factory/cell_003.ttm` through
  `cell_024.ttm` for the authored visibility zones
* `sdmc:/3ds/toontown3ds/data/factory/lift.ttm`,
  `door.ttm`, `button.ttm`, `crate.ttm`, `goon.ttm`, and `stomper.ttm`
* `sdmc:/3ds/toontown3ds/data/factory/level_spec.bin`
* `sdmc:/3ds/toontown3ds/data/factory/manifest.json`
