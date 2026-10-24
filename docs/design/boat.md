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
| Integration (instantiate, event, update, draw, setParent) | `source/main.cpp` |
| Deck collision + riding | `source/world/boat.*` (`deck()`), `source/world/player.*` (`setPlatform`) |
| Sounds | `source/world/boat.*`, `tools/pipeline/bake_audio.py --set boat` |
| Remote toons on the boat | `source/world/remote_toons.*` |

The state start time is `networkToLocal(ts)` like `CChar::walkStartMs`; the boat pose is
`path(now - start)` clamped to the Mopath duration (linear interpolation of a 0.1 s sample table,
x, y, z, heading). Donald is only drawn while a boat is active; when the boat object is missing he is
hidden (as before).

## Riding (implemented, not yet run in Azahar)

Upstream: the collision `donalds_boat_floor` fires `enter/exit` events, `DDPlayground.activityFsm`
`OnBoat` calls `localAvatar.b_setParent(SPDonaldsBoat)` (ParentMgr `wrtReparentTo`, so the world
pose is kept) and loops `SZ_DD_waterlap`; on exit `b_setParent(SPRender)`. While parented, the
toon's `setSmPosHpr` poses are boat-local, and other clients draw it under the boat node.

Ours (`Player::setPlatform`, `world/player.*`):

- **Approach: the toon is simulated in boat space** instead of transforming the 53-triangle deck
  into the world every frame. `1000_boat.ttc` loads once as a second `CollisionWorld` (4 KB, regular
  heap, `BoatActor::deck()`); `main.cpp` calls `boat.update()` then
  `player.setPlatform(boat.deck(), boat x/y/z/heading)` before `player.update()`.
- **Boarding** (`tryBoard`): each frame the world pose is inverse-transformed (one rotation) and
  tested against the deck floor: a floor at most 1.0 above the feet and at most 5.0 below (jump apex
  ~4.5) boards. The deck floor is local z 3.93 (world ~0.08 with the boat at z -3.85). The left rail
  has an opening (x -5, y 3.5..14), the right rail is solid.
- **Aboard**: the toon's slide / ground / jump run unchanged on the deck collision in local space
  (so rails and the hull walls work), then local -> world uses the boat matrix every frame: the toon
  is carried by the boat's translation and its heading delta (the camera follows the world heading
  and also ray-casts the deck's camera walls in local space).
- **Leaving**: no deck floor under the toon (walked off an edge, boat gone) -> world mode, the zone
  collision takes over with the world pose that was just computed.
- **Network** (`main.cpp`): on a board/leave transition `sendSetParent(3 / 2)` (`DistributedNode.setParent`,
  already clsend-whitelisted via `TT_NET_OWN_UPDATE`) followed immediately by a pose in the new frame
  (`Player::netPose` = boat-local while aboard) and a `setSmStop` if we are standing. While aboard
  `Client::setOwnWorldPos` keeps the world position for chat targeting. `netOnBoat` resets when not in
  a zone (entering a zone sends `setParent(render)`).
- **Remote toons**: `RemoteToons` snaps and then smooths poses in the parent's frame; toons whose
  `parentToken == kParentDonaldsBoat` are drawn (and shadowed / nametagged) through `boat.matrix()`.
- **Sounds** (`BoatActor::updateSounds`, assets baked by `bake_audio.py --set boat`, ~490 KB PCM16):
  bell at t=0 of each sail, creak of the departure pier at t=0 and of the arrival pier at
  `dur - 5 s`, foghorn at `dur - 2.19 s` (end-aligned); positional at the boat / pier docks with
  gain `min(1, 30 / d)` (silent beyond 250) and pan from the camera; sounds crossed more than 0.75 s
  ago (joined mid-sail) are skipped. `SZ_DD_waterlap` loops while the toon is aboard
  (`BoatActor::setRider`). Sounds load one file per frame after the boat spawns.
- **Async load**: the boat model loads through `world/asset_loader.*` (see below); nothing is drawn
  until it is ready.

## Background asset loader

`world/asset_loader.{h,cpp}`: a fixed pool of 24 slots and one thread (prio 0x31, core 0, 16 KB stack,
started from `main`). `requestModel/requestAnim(path)` return a ticket; the thread loads into the slot's
staging `render::Model` / `render::Anim` (their `load()` take `mem::LinearGuard` only around the
short alloc calls, never across file IO); the main thread `poll`s, then `takeModel/takeAnim` swaps the
staging object into an empty destination (`Model::swap`, new `Anim::swap`: no GPU work, no
allocation) and frees the slot. Tickets carry a generation; `cancel` discards an in-flight load on
the loader thread. Users: `CCharActors` (model + clips; the actor draws nothing, shows no nametag
and casts no shadow until ready; walk timing is a pure function of the clock so the replay is exact)
and `BoatActor`. `audio::loadSfx/freeSfx` now take the linear guard too.

## What remains

- **Piers.** `east_pier` / `west_pier` stay static (pitched -44.26 in the zone model); they should be
  baked as separate pieces with their collision and animated as above (the creak already plays).
  Until then a toon boarding from the dock walks over the static tilted pier.
- Boarding only happens over the deck floor; the hull (z 0..12, y -10..2) and rails are walls for a
  toon already aboard but not for one on the dock approaching from the side (upstream's pusher would
  block it; the zone collision has no boat walls).
- Sail sounds use fixed pier dock positions (the path ends), not the (unbaked) pier nodes.
- The boat is not frustum culled or LODed (465 triangles, 7 batches, 5 textures, 61 KB `.ttm`).
- No `docs/test-log.md` entry yet: built clean, not run in Azahar.

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
- Sounds: `.venv/bin/python -I tools/pipeline/bake_audio.py --set boat --out sdmc-out/3ds/toontown3ds/data/audio --resources ref/resources` -> `boat_{bell,foghorn,creak,lap}.tsnd` (copy to `sdmc:/3ds/toontown3ds/data/audio/`).
- Install: copy `1000_boat.ttm` (and `.ttc`) and the new `1000.tz`, `1000.json`, `1000.pts` to
  `sdmc:/3ds/toontown3ds/data/zones/`. Without `1000_boat.ttm` the boat is simply not drawn
  (logged: `boat: no model`) and Donald, drawn relative to it, is invisible.
- `Donald` assets (`chars/donald-wheel_1000.*`) come from `bake_char.py` (see `docs/design/chars.md`).

## Testing notes

- Built clean (`make -j4`, zero source warnings). **Not run in Azahar.** To exercise: enter zone 1000 on
  the local server, expect log lines `net: boat <id> state <n>` and the ship moving through the cycle
  (state 1 DockedEast, 2 SailingWest, 3 DockedWest, 4 SailingEast) with Donald on deck.
