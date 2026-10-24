# Donald's Dock boat (DistributedBoat) and Donald at the wheel

Upstream sources (read-only spec, `ref/open-toontown`):
`toontown/safezone/DistributedBoat.py`, `DistributedBoatAI.py`, `DDSafeZoneLoader.py`,
`DDPlayground.py`, `toontown/classicchars/DistributedDonaldDock.py`, `etc/toon.dc`,
`toontown/hood/DDHoodDataAI.py`, `ToontownGlobals.SPDonaldsBoat = 3`.

## Upstream behaviour

- The boat is the `ModelNode` `donalds_boat` inside
  `phase_6/models/neighborhoods/donalds_dock.bam` (placed by DNA as prop `donalds_dock`). At load
  `DDSafeZoneLoader` hides its `wheel` child (Donald's actor has his own wheel) and stashes the boat;
  `DistributedBoat.generate` unstashes it. In the model its transform is identity.
- `DDHoodDataAI` generates one `DistributedBoatAI` (zone 1000) and one `DistributedDonaldDockAI`.
- DC (`toon.dc`): `dclass DistributedBoat : DistributedObject { setState(string, int16) required broadcast ram; }`.
  Class id 111, `setState` field 790, kw 0x007 (see `source/net/dc_generated.h`).
  `DistributedDonaldDock : DistributedCCharBase` (already handled by `world/cchar.*`).
- The server clock: `ts` is `globalClockDelta.getRealNetworkTime()` at the transition;
  the client does `fsm.request(state, [localElapsedTime(ts)])` and starts the track at that offset.

### State machine and timing (AI: `DistributedBoatAI`)

| State | Duration | Next | Client action |
|---|---|---|---|
| `DockedEast` | 10 s | `SailingWest` | `weTrack.finish()` (boat at end of dd-w-e, east pier up) |
| `SailingWest` | 20 s | `DockedWest` | `ewTrack.start(ts)`; exit: `ewTrack.finish()` |
| `DockedWest` | 10 s | `SailingEast` | `ewTrack.finish()` (boat at end of dd-e-w, west pier up) |
| `SailingEast` | 20 s | `DockedEast` | `weTrack.start(ts)`; exit: `weTrack.finish()` |

The AI starts with `b_setState('DockedEast')`. A full cycle is 60 s. Mopath durations are
20.0004 s (dd-e-w) and 20.001 s (dd-w-e).

### Tracks (`DistributedBoat.setupTracks`)

- `ew-track` = `ParallelEndTogether(Parallel(ew-boat, e-pier-down), w-pier-up)`;
  `we-track` = `ParallelEndTogether(Parallel(we-boat, w-pier-down), e-pier-up)`.
- `ew-boat` = `ParallelEndTogether(Parallel(MopathInterval(dd-e-w, boat), SoundInterval(shipbell)), SoundInterval(foghorn))`
  (the foghorn is aligned to the end of the sail). `we-boat` is the same with dd-w-e.
- Piers (`east_pier`, `west_pier`, children of `piers`): `PIER_TIME` 5 s `LerpHpr` between pitch
  `0.25` (flat, boat docked) and `-44.2601` (tilted away, the model's default): east pier `(90, p, 0)`, west pier `(-90, p, 0)`.
  Sailing West: east pier goes flat -> -44.26 during t = 0..5 s, west pier -44.26 -> flat during t = 15..20 s
  (end-aligned). Sailing East is the mirror image.
- Sounds: `phase_6/audio/sfx/SZ_DD_dockcreak.ogg` (piers), `phase_5/audio/sfx/SZ_DD_foghorn.ogg`,
  `phase_6/audio/sfx/SZ_DD_shipbell.ogg` (both 3D at the boat), `SZ_DD_waterlap.ogg` looped while a toon
  is on the boat (`DDPlayground.enterOnBoat`).
- Boat riding: the collision `donalds_boat_floor` fires `enterdonalds_boat_floor` /
  `exitdonalds_boat_floor` -> `DDPlayground.activityFsm` `OnBoat` -> `localAvatar.b_setParent(SPDonaldsBoat)`
  (parent token 3) so the toon's position becomes boat-local.

### Mopath poses (sampled with `Mopath.goTo`, z is always -3.85, pitch/roll 0)

| Path | t = 0 | t = 10 s | t = 20 s |
|---|---|---|---|
| dd-e-w | (49.58, 26.04) h 180 | (10.22, -49.30) h 90 | (-49.72, -10.71) h 0 |
| dd-w-e | (-49.72, -10.71) h 0 | (-15.61, 62.37) h -89.7 | (49.52, 26.14) h -179.8 |

Docked East = end of dd-w-e, Docked West = end of dd-e-w. The boat goes around the south of the
playground sailing west and around the north sailing east.

### Donald at the wheel

`DistributedDonaldDock.generate`: `setPos(0, -1, 3.95)`, `reparentTo(boat)`, neutral state forever
(Char `'dw'`, the model `donald-wheel`). No nametag. Heading 0 relative to the boat.

## What is implemented

| Part | File |
|---|---|
| Bake: boat model, deck collision, path tables | `tools/pipeline/bake_boat.py` |
| Static 1000.tz without the boat | `tools/pipeline/bake_street.py --separate-boat` |
| DC: `DistributedBoat.setState` | `tools/dcgen/dcgen.py` (WANTED + TABLE_CLASSES), `source/net/dc_generated.h` (hash unchanged, 547155168) |
| Net: `Boat` struct, `BoatSpawn/BoatState/BoatGone` events, `Client::boat()` | `source/net/client.{h,cpp}` |
| Runtime: state clock, path sampling, draw | `source/world/boat.{h,cpp}`, generated `source/world/boat_paths.h` |
| Donald drawn attached to the boat at (0, -1, 3.95) | `source/world/cchar.cpp` (`setBoat`) |
| Integration (instantiate, event, update, draw) | `source/main.cpp` |

The state start time is `networkToLocal(ts)` like `CChar::walkStartMs`; the boat pose is
`path(now - start)` clamped to the Mopath duration (linear interpolation of a 0.1 s sample table,
x, y, z, heading). Donald is only drawn while a boat is active; when the boat object is missing he is
hidden (as before).

## What remains

- **Toon riding / moving collision.** `1000_boat.ttc` is baked (deck floor 9 tris, walls and camera
  planes, boat-local frame) but not loaded. A toon walking onto the deck currently stands on nothing
  (the static boat floor is gone from 1000.tz with `--separate-boat`). To do: load the .ttc as a second
  `CollisionWorld`, transform the toon into boat space when over `donalds_boat_floor` (upstream's
  `b_setParent(3)`), and send `setParent` 3 plus boat-local pos in `setSmPosHpr` (a parent token of 3
  is not whitelisted yet).
- **Piers.** `east_pier` / `west_pier` stay static (pitched -44.26 in the zone model); they should be
  baked as separate pieces with their collision and animated as above.
- **Sounds** (bell, foghorn, dock creak, water lap): not baked or played.
- `OnBoat` activity (water-lap loop, boat parent).
- The boat is not frustum culled or LODed (465 triangles, 7 batches, 5 textures, 61 KB `.ttm`).

## Baking and installing assets

Run from the repo root (the worktree's `ref/` may be empty; point `--resources` at the real checkout):

```
.venv/bin/python -I tools/pipeline/bake_boat.py --resources ref/resources
.venv/bin/python -I tools/pipeline/bake_street.py --zone 1000 --separate-boat --resources ref/resources
```

- `bake_boat.py` writes `sdmc-out/3ds/toontown3ds/data/zones/1000_boat.ttm` and `1000_boat.ttc`, and
  regenerates `source/world/boat_paths.h` (deterministic; commit it if it changes).
- `bake_street.py ... --separate-boat` stashes `donalds_boat` after DNA assembly so neither its geometry
  nor its collision enter 1000.tz. Result vs the old bake: 15,220 -> 14,753 worst-case visible triangles,
  floor triangles 469 -> 444, `1000.tz` 4.88 MB -> 4.70 MB. **The flag is off by default** so existing
  bakes are unchanged; use it for zone 1000 once the runtime ships (otherwise a static boat sits at the
  origin next to the moving one). It is safe for all other zones (no `donalds_boat` node there).
- Install: copy `1000_boat.ttm` (and `.ttc`) and the new `1000.tz`, `1000.json`, `1000.pts` to
  `sdmc:/3ds/toontown3ds/data/zones/`. Without `1000_boat.ttm` the boat is simply not drawn
  (logged: `boat: no model`) and Donald, drawn relative to it, is invisible.
- `Donald` assets (`chars/donald-wheel_1000.*`) come from `bake_char.py` (see `docs/design/chars.md`).

## Testing notes

- Built clean (`make -j4`, zero source warnings). **Not run in Azahar.** To exercise: enter zone 1000 on
  the local server, expect log lines `net: boat <id> state <n>` and the ship moving through the cycle
  (state 1 DockedEast, 2 SailingWest, 3 DockedWest, 4 SailingEast) with Donald on deck.
