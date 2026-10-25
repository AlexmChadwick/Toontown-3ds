# Cogdominiums in sv1.0.47.38

This client reimplements the pinned `toontown/cogdominium` server/client contract.
It does not embed upstream Python. All model/spec processing happens offline.

## What this version actually runs

`DistributedBuildingAI.cogdoTakeOver` uses `FieldOfficeNumFloors`, not the incoming
building height. `CogdoLayout(n)` has n game floors and, for n>=1, one final battle
floor. Departments are not filtered by `SuitPlannerCogdoInteriorAI`; all four
tracks use the same Mover & Shaker exterior in `DistributedBuilding.FO_DICT`.

The interior registers maze, flying, crane and boardroom constructors, but its
`IntGames` exclusion set contains **boardroom, crane and flying**. Normal random
selection therefore runs **maze only**. `cogdo-game` or per-toon GameRequests can
force the other prototypes. Boardroom is a 60-second room followed by a 10-second
finish timer; it has no gameplay-specific DC fields. Crane assigns participants
their cranes automatically and times out after 180 seconds. Its base hitBoss is
a no-op; the money-bag override contains old CFO helmet logic, so clients must not
invent a boss-health objective for it. Flying's `tt_m_ara_cfg_*` BAMs are absent
from the pinned resource repository. Flying is unsupported: no invented fallback art or level is supplied. Forcing it
shows a missing-assets message and permits returning with B. Crane and boardroom
are opt-in forced prototypes only, never the stock random selection.

`cogdo-want-barrel-room` defaults to 0. The barrel room is an optional server flow,
not a mandatory stage between normal maze and the suite. Stock collection lasts
30 seconds and grants 2..4 laff per valid barrel per toon. A setGrab broadcast
records a toon grab; it does not globally consume the barrel.

## Server-driven flow and duties

Street building `cogdo` -> DistributedCogdoElevatorExt (inherits stock building
boarding/seat/trip fields) -> building's actual interior zone ->
`WaitForAllToonsInside` / setAvatarJoined -> `Elevator` / elevatorDone -> `Game`.
Game load has Visible -> Intro -> setAvatarReady -> timed setGameStart ->
setGameFinish. The intro barrier has a very long timeout: ready is required.

Maze is seeded by the game's doId with Panda RandomNumGen/Mersenne. Its factory
clones the RNG by drawing a seed in 0..65536, shuffles barrier indices and nine
quadrant names, then chooses 0/90/180/270 rotations. Clients must reproduce that
sequence and the 52×52 cell map, not bake one guessed layout for every game.
Maze walls, water coolers, suit spawn/walk logic and real gag collisions are local;
AI owns suit HP, memo grants, laff and the exit gate. Killing all boss suits opens
the door. requestAction(EnterDoor=0) is legal only after OpenDoor=2; RevealDoor=1
is distinct. The game can end early when all toons enter the open door. Timeout
with a closed door makes the office fail; clients must not force victory.

Successful game -> `Resting` -> next `Elevator`. Failed game -> `Failed` -> return.
Optional room flow: BarrelRoomIntro / toonBarrelRoomIntroDone -> CollectBarrels ->
BarrelRoomReward / toonBarrelRoomRewardDone -> battle. The final suite uses
BattleIntro -> Battle -> ReservesJoining / reserveJoinDone as suits join. Combat
is DistributedCogdoBattleBldg, inherited DistributedBattleBldg, through the public
native Battle API, preserving existing camera/movies. Reward grants the SOS card
on the AI, then the building enters waitForVictorsFromCogdo. Return to the stored
exterior zone; no client grant, fake phase change or guessed dynamic zone.

## Protocol and bounds

`net/cogdo_state.*` stores fixed, transactional snapshots. Interior/game limits:
4 participants, 32 active/reserve battle suits, 64 maze suits, 256 pickup flags.
The object pool additionally admits 10 barrels, 4 cranes and 8 money bags. All
required and OTHER field streams are parsed before publishing. Truncated or
malformed data cannot partially replace an existing object. Every outgoing field
uses TT_NET_UPDATE with a compile-time clsend assertion and membership/phase/
owner guards. Numeric IDs come only from dcgen; hash stays 547155168.

The exact fields are generated in the marked `_COGDO_FIELDS` block of dcgen.py.
Interior fields: zones/building/floor count/NPC/SOS/type; toon and suit lists;
(string, i16 timestamp) state; six phase-specific completion callbacks; barrel
reward (u32[],u8[]). Game common fields: interior/exterior/difficulty overrides,
visible/intro/start/finish and setAvatarReady. Maze has action(u8,u32), fixed
u8[3] suit counts, gag/use/hit, pickup and water-cooler fields. Flying has its
separate u8/u8 requestAction and u16/u8 requestPickUp forms. Do not send its
GotoWinState=2 broadcast as a request. Crane cable is
(u8,i16/100,u16%360/100,3 LinkPositions,i16); LinkPosition coordinates are i16/10.
Crane control is assigned by AI; there is no requestControl field in this class.

## Native implementation and controls

`CogdoRuntime` runs before the existing boss frame hook while our toon is a
member of the interior. The street and floor groups are mutually exclusive;
GPU synchronization precedes scene retirement. `CogdoAssets` preflights the
whole group, queues one bounded loader ticket at a time, and reports missing,
malformed or over-budget data on the bottom screen. B returns from a load error.
There is one native simulation/render pair and both displays are cleared.
Stereo is disabled inside offices to preserve the Old 3DS rendering envelope.

Maze: Circle Pad uses the ordinary tank controller with forward input capped to
94 (under the upstream 11.2 units/s). A jumps, B throws the equipped balloon.
Walk close to a water cooler to request one. Cooler transforms and numbering
are sorted like Panda and the request sends the current avatar pose first.
The 18-unit, half-second balloon uses swept local collisions; a hit sends the
specific suit type/serial and waits for the AI's HP broadcast. Boss scale is
2.5, fast minion 1.3, slow minion 1.33. All 64 possible Cogs simulate, with only
the nearest six booked into the existing actor pool. Cabinet drops consume the
same per-Cog RNG draws every third boss think; DropDamage is zero in this pin.
Network death order assigns memo IDs; the cosmetic memo spread is simplified.
Hit animation lasts 2.25 seconds plus the source two-second invulnerability
cooldown. No message grants client-side laff, credit, a memo or a reward.

The 52x52 map uses CGM1 collision tables. MazeBase's z=0 gameplay plane is baked
explicitly: the source frame BAM contains elevated trim and cannot provide the
ground. Four-way pathfinding is diagnostic-only and moves through the ordinary
controller and cell collision; it never teleports within a game. Exact seeded
quadrants, cells, spawn coordinates, direction choices, tick ordering, cabinet
RNG consumption and cooler ordering have independent upstream/Panda oracles.
The upstream unchecked reverse-direction fallback is preserved, including its
occasional walk into a reserved cell.

Battle floors and the suite use the existing public `Battle` API, choices,
movies, effects, laff and reward handling. ReservesJoining is acknowledged once
per timestamp after 3.5 seconds. The suite's real shop-owner DNA is loaded from
BND1 records through ToonCache at low LOD. Reward lowers the cage over .75 s,
opens its hinged door over .5 s, and lets the owner walk out over 1.5 s. The
full upstream dialogue/camera/particle movie is condensed to this presentation
and the existing battle movie renderer. A returns after the reward presentation;
the diagnostic does this automatically. The exterior handshake sends
`DistributedBuilding.setVictorReady` only after its actual victor list contains
us. Manual boarding caches the street door before the interest handoff. Defeat
returns to the toon's home playground; reconnect uses the stored exterior,
never the abandoned dynamic interior.

The optional barrel room shows ten original barrel positions, the 30-second
collection timer, per-toon collection, and AI rewards. Stompers retain their baked
pose rather than the full procedural animation. The pinned AI's `_gameDone`
branches to Resting/Failed/BattleIntro; enabling `cogdo-want-barrel-room` alone
**does not route into BarrelRoomIntro** in this revision. These incoming states
and completion messages are implemented and host-tested, but the stock maze
run does not visit them. Do not claim an ordinary maze run tests barrel play.

Crane and boardroom are forced prototypes. Their EndVault, crane and bag models
exist in the pinned resources and are baked. Boardroom preserves its 60+10 second
timer without inventing an objective. Crane preserves AI-assigned control,
bounded three-link cable simulation, owner-only cable/held/dropped bag updates,
magnet grabs/releases and floor/free requests. Circle Pad moves the crane arm;
B toggles the magnet. It has the stock 180+10 second timer and no implemented
boss-health objective. The native cable constraints approximate Panda physics;
there is no unsupported hitBoss shortcut. These prototypes need emulator checks
before any playability claim. Flying remains unsupported and has no baked level,
no native substitute, and no opt-in dev switch here.

## Assets, formats and reproducibility

Run from this checkout only. Python must always use `-I`; `-B` prevents imported
upstream modules writing bytecode through the read-only symlink. Install the
stock simplifier wheel into worktree-local ignored scratch (never `.venv`):

```sh
UV_CACHE_DIR="$PWD/sdmc-out/cogdo-tools/cache" TMPDIR="$PWD/sdmc-out/cogdo-tools/tmp" \
  uv pip install --python .venv/bin/python --target sdmc-out/cogdo-tools \
  --no-cache --no-compile-bytecode --no-deps fast-simplification==0.2.0
.venv/bin/python -I -B tools/pipeline/bake_cogdo.py
.venv/bin/python -I -B tools/cogdo_test/assets.py --rebake
```

Create the scratch cache/tmp directories first on a fresh checkout. The baker
uses one BLAS/OpenMP thread and at most three ETC1 workers. Textures are at most
128x128, ETC1/ETC1A4 or the existing one/two-channel formats. Source construction
geometry outside the playable barrel corridor is excluded. Large architectural
faces are retained; smaller furniture detail uses deterministic quadric
simplification and stable area selection. Native TTM decoding was compared
visually with the source models at matching cameras. Collision retains source
precision, apart from the explicitly correct flat maze plane.

The asset kit is `sdmc-out/3ds/toontown3ds/data/cogdo/`, with descriptive names:
`shell`; `elevator`; `maze_frame`, `maze_quadrant_1` through `_9`,
`maze_divider`, `exit_door`, `balloon`, `memo`, `cabinet`; `barrel_room`,
`barrel`; `battle_room`; `suite`, `suite_cage`, `suite_door`;
`prototype_room`, `crane_arm`, `crane_magnet`, `crane_controls`, `crane_stick`,
`money_bag`. Models use TTM1 v1; five collision meshes use TTC1 v1.
CGM1 v1 stores 9*4*16*16 one-byte collision cells; CGW1 v1 stores a counted list
of `(u32 quadrant, f32 x,y,z,heading)` cooler poses. CGP1 v1 has five Vec3 points:
exterior door, maze entrance, maze exit, cage, cage-door hinge. BND1 v1
`shop_owners.bin` has counted `(u32 npcId, 15-byte ToonDNA)` records. All headers
are little endian and independently validated. Complete budgets/hashes are in
`cogdo_assets.json`; `cogdo_reproducibility.json` records fresh-process equality.


## Old 3DS budget report

These are the unique resident baked models per group, including mip chains.
The maze row includes all nine quadrants; actors use the existing bounded pools.

| Group | Geometry bytes | Texture bytes | Triangles | Batches |
|---|---:|---:|---:|---:|
| Maze | 350,748 | 739,136 | 10,650 | 92 |
| Battle floor | 47,840 | 65,632 | 1,736 | 7 |
| Barrels | 99,080 | 234,560 | 4,908 | 10 |
| Executive suite | 99,678 | 261,888 | 5,269 | 20 |
| Forced crane | 20,636 | 75,872 | 698 | 15 |
| Forced boardroom | 16,048 | 62,336 | 504 | 10 |

Hard model-group caps: 6 MiB geometry, 8 MiB textures, 15,000 triangles and 150
batches. Repeated barrels/cranes/bags add draw instances, not resident copies.
Runtime actors, toon clips, effects, render targets and command buffers are
additional existing pools; the asset table is not a measured total-frame cost.
Maze keeps six render actors, 64 simulations, 256 memo slots and 16 cabinet drops.
Collision heap allocations: maze 7,548 B; suite 30,492 B; barrels 28,532 B;
battle room 2,076 B; prototype room 6,204 B. No per-frame simulation allocation. The final map totals 2,381,432 bytes
for .text + .rodata + .data + .bss (under 4 MiB); CogdoRuntime occupies
176,576 static bytes. These are link-map measurements, not live heap readings.
The final kit is 40 files including its manifest. The manifest counts 2,128,848
asset bytes excluding itself; the reproducibility report includes every file.

## Validation and Azahar handoff

Host/build validation on 2026-10-25, **no emulator or hardware run**:

- Prescribed make -j4 succeeds, with zero source warnings. The devkitARM crtn.o
  GNU-stack linker warning originates in the external toolchain.
- `bash tools/cogdo_test/run.sh`: ASan/UBSan field parser and real Client;
  77 independent Panda field vectors with every truncated prefix; nine complete
  Cogdo object streams; inherited elevator/battle/building dispatch; phase and
  membership guards; barrel ownership; cable/bag ownership; exterior victory
  callback. Six seeds compare every cell and initial Cog position, 3,600 frames
  of exact direction/tick/drop RNG, cooler coordinates/indices, collision-safe
  entrance-to-cooler-to-exit navigation, difficulty sentinels and crane bounds.
- `PORT=7598 bash tools/nettest/run_tests.sh`, `bash tools/boss_test/run.sh`.
- `.venv/bin/python -I -B tools/cogdo_test/regressions.py`: all 33 existing
  minigame, combat, book, fishing, facility, animation, estate, race, DNA, zone,
  shop, building, tailor and character regression suites.
- `.venv/bin/python -I -B tools/cogdo_test/preview.py`: independent TTM decoder,
  texture/mip validation and offscreen previews, compared with source geometry.
- `.venv/bin/python -I -B tools/cogdo_test/assets.py --rebake`: all 40 outputs,
  including the manifest, byte-identical across independent baker processes.

After integration, use the established main-checkout server installation and
its existing assets (full data/toon animation set, data/cogs, battle effects,
UI, zones/2100 and the default boot RomFS). This kit adds only data/cogdo;
copy its entire directory into the emulator SD data tree. Do not rebake or
replace another agent's assets. No runtime source edits remain for the main agent.

Start the dev server with:

```sh
AI_DEV=1 AI_DEV_STRONG=1 AI_DEV_WEAK_COGS=1 AI_DEV_COGDO=1 tools/server/start.sh
```

The import hook is off by default. AI_DEV_COGDO schedules one existing TTC street
building to call stock cogdoTakeOver after five seconds. Check the AI log for
`ai_dev: cogdo takeover zone ... block ...`. With an existing logged-in toon,
put `only cogdo:` in `sdmc:/3ds/toontown3ds/autotest`, then start Azahar in Old
3DS mode. The script's board step requests 2100 and stands at the actual field
office door; normal elevator boarding supplies the dynamic destination.

Check the shot series for labels `cogdo: board` (60 s), `cogdo: floor` (35 s),
`cogdo: game` (240 s), `cogdo: suite` (600 s), `cogdo: exit` (30 s). Labels are
capture windows, not assertions that server state transitions happened exactly
at their boundaries. The diagnostic follows the maze automatically and uses
the existing battle autoplay. It never clears the gate, kills a Cog or grants
an item through a debug-only network request.

Verify, in order:

1. The original Mover & Shaker shell aligns with the street building. Its
   offset elevator door boards once, carries actual riders, and enters the
   server-selected zone. Test a manual second run too, including a seat exit.
2. Logs show `cogdo: entered interior`, `floor ... WaitForAllToonsInside`,
   `Elevator`, `ready game`, then `setGameStart`. The maze has one seeded layout
   shared by two clients; coolers grant a balloon without suspicious/eject logs.
3. Pad walls, jumps and B throws work; suit scales, movement, cabinet drops,
   damage/invulnerability and memo claims are plausible. Four boss deaths result
   in the AI's `doAction` OpenDoor, then `entered open maze exit`. A closed gate
   never accepts EnterDoor. Check a failed/timeout run and laff-zero return.
4. The next elevator loads the suite, authentic shop owner/cage and real battle.
   Test choices, every reserve wave, gag movies, laff updates and reward. The
   cage lowers/opens and its owner walks out. No fake client SOS grant occurs.
5. A or the diagnostic returns beside the same street door, without boarding
   again. Logs show `return zone ...`, then `victor ready building ...`; the
   building returns to its toon state and the server's SOS inventory persists.
6. Disconnect during a game and battle: reconnect into the stored street;
   no retained dynamic zone, actors, loader ticket or old group survives. Test
   missing/corrupt CGM/CGW/CGP/model files: visible error and B return, no crash.
7. For prototypes only, restart with AI_DEV_COGDO_GAME=crane or boardroom added.
   Check `ai_dev: forcing cogdo game ...`; crane ownership, remote cable poses,
   magnet grabs/releases and bag floor/free; boardroom and crane timers finish
   through the AI. Flying is unsupported. Barrels need an explicit server-state
   fixture: the pinned default AI never routes to them even with its config flag.
   In such a fixture verify ten positions, healing 2..4, independent per-toon
   claims, Available/Used/Crushed states, the 30 s timer and reward callback.
8. Log FPS, heap, linear/VRAM free and frame time every phase in test-log.md.
   Check 30 fps, no command-buffer guard skips, no growth on repeated entries,
   and the whole-frame 15k-triangle/150-draw target. Host counts cannot establish
   hardware timing. In particular inspect six nearby maze Cogs plus furniture
   and multiplayer/battle effects before considering the frame budget proven.
