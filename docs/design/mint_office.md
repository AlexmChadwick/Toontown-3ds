# Cashbot Mint and Lawbot DA Office

The client treats both Cog HQ facilities as server-owned dynamic levels. A
`DistributedMint` or `DistributedStage` supplies the interior zone, floor and
ordered room object IDs; each room supplies its room number, suit list and
boss/defeated state. The native client never evaluates Python specs at runtime:
`room_spec.bin` and the baked room props are generated offline.

## Cashbot Mint

Cashbot HQ zone `12000` owns the Coin, Dollar and Bullion elevators, whose
interior zones are `12500`, `12600` and `12700`. The stock AI selects a seeded
`MintLayout` by mint ID and floor, then generates `DistributedMintRoom`
objects in that order. Entrance room `0` leads through middle rooms `1--16`;
the final room set `17--25` contains the vault and boss encounter. Room
transitions are server-side dynamic-zone changes, so the client streams the
next baked room on the room object event.

## Lawbot DA Office

Lawbot HQ is zone `13000`. Its A--D elevators enter the DA Office stage
system. `StageLayout` chooses a deterministic seeded layout from the stage
layout index and floor, and `DistributedStageRoom` objects are generated in
server order. Room `0` is the lobby/entrance; the stock room table also
contains action, security, battle and final rooms (`1--11` and `100--105`).
Laser fields, cameras and maze entities are server objects; the client uses
their state to drive compact baked props and sends only the corresponding
interaction requests.

## Entry and testing

The stock server enforces elevator acceptance, laff minimums and Cog
disguise/access checks. Mints additionally require the appropriate Cashbot
access/disguise, while DA Office elevators require the Lawbot access/disguise
for the selected elevator. For client testing, use a server account/toon that
already satisfies those checks; the stock protocol does not provide a client
side bypass. A local server configured with `ToontownAccessAI` checks disabled
or elevator `minLaff=0` can be used to force entry.

Generated assets live under
`sdmc:/3ds/toontown3ds/data/mint/` and
`sdmc:/3ds/toontown3ds/data/stage/`, with `room_spec.bin` in each directory.
