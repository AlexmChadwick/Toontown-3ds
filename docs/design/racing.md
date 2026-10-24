# Goofy Speedway kart racing

## Scope and stock AI

The client follows the `toontown/racing` DC protocol from Open Toontown. `RaceGlobals.py`
defines Practice (`0`), ToonBattle (`1`), and Circuit (`2`), four racers, three-lap
defaults, and track IDs for Speedway, Rural, and Urban genres, including separate reverse
IDs (`RT_Speedway_1_rev`, `RT_Rural_1_rev`, and `RT_Urban_1_rev`). The stock AI contains
`DistributedRacePadAI`, `DistributedStartingBlockAI`, `DistributedRaceAI`, and
`DistributedVehicleAI`: pads validate tickets/ownership, starting blocks run the countdown
and hand off to a dynamic race zone, races generate gags and report place/lap results, and
vehicles allow only the owning toon to request control and send input. The AI does not
provide client physics; this client supplies fixed-step steering and acceleration while
the server remains authoritative for race state.

Stock deployments must still generate the Speedway objects and enable `RaceManagerAI`;
some Open Toontown server configurations ship the racing code but do not create race pads
or dynamic race zones. If no `DistributedRacePad` appears in zone 8000, the protocol is
present but unavailable on that server and the client cannot manufacture a race.

## Owning a kart

`DistributedToon` stores kart DNA: body, body colour, accessory colour, engine block,
spoiler, wheel wells, rims, and decal. `KartDNA.py` supplies the model/accessory mapping;
the default body is `Kart1_Final` and a new toon has the normal starter kart fields. The
Speedway kart clerk (`DistributedNPCKartClerk`) is the authoritative shop. It uses tickets,
not jellybeans; the upstream body prices include 100 tickets for the starter body and
larger prices for upgraded bodies. The client exposes the clerk protocol and renders the
default kart even when a stock server has no shop transaction object.

## Pads, blocks, and hand-off

Walking into a `DistributedRacePad` exposes its track list and state. Each
`DistributedStartingBlock` has a pad ID, location ID, pose, and occupant. Pressing A sends
`requestEnter(paid)`, where the server validates the kart, entry fee, tickets, and whether
the block is available. The pad moves through `WaitEmpty`, `WaitCountdown`, `WaitBoarding`,
and `AllAboard`; the first valid block starts the countdown. `movieFinished()` completes
the boarding movie. The pad then allocates a dynamic race zone and the race sends
`setRaceZone` to each participant; the main hook changes interest to that zone.

## Race protocol and controls

`DistributedRace` reports track, race type, loop, racers, starting places, lap count,
`startRace(timestamp)`, `setPlace`, and circuit-place updates. The client drives with the
Circle Pad, A accelerates, B brakes, and R sends the current gag request. Pies/projectiles,
bananas, anvils, and turbo are represented by `DistributedGag`/`DistributedProjectile`
events; `heresMyT`, `requestThrow`, and `hasGag` are sent only through their declared
client-send fields. The top HUD shows speed, lap, and place. The bottom info panel shows
the active race title, lap/place text, and the track slot; the baked track/pad/kart set is
kept under the Old 3DS geometry, texture, and draw-call budgets.

The client tracks local lap/place state but never awards a place locally. The AI's
`setPlace`/`setCircuitPlace` is authoritative. On `goToSpeedway`, the client marks the race
finished and returns through the server-provided zone ID; ticket winnings are applied by
the server's toon fields.

## Assets and verification

`tools/pipeline/bake_racing.py` writes six track TTM/TTC pairs, the default kart, pad, and
gag models to `sdmc-out/3ds/toontown3ds/data/racing/`. Autotest labels are:

* `go speedway` — teleport to playground 8000.
* `speedway: pads` — face the generated race pads.
* `speedway: race` — enter a starting block, drive, use R, and wait for the server finish.
* `speedway: shop` — open the kart-clerk area and exercise the starter-kart path.

Use these only against a server that actually instantiates the racing AI objects; otherwise
the first step is still useful for checking the Speedway bake and the client HUD.
