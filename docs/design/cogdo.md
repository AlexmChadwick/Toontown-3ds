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
from the pinned resource repository. The native kit provides a clearly named
fallback course for this forced prototype; it is not a reproduction of missing art.

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
