# Classic characters: baked assets and replay specification

This task creates only the character pipeline, preview tool and this handoff. No
runtime, generated DC header, existing pipeline module, AGENTS.md or test log was
changed. Assets are generated under `sdmc-out/`, ignored by Git. No Azahar or
`make` was run, as required by AGENT_TASK.md. Device behavior and performance
remain untested; the verification below is an offline pipeline test.

## Reproduce

Run from the chars worktree using its existing stock-wheel environment:

```sh
.venv/bin/python -I tools/pipeline/bake_char.py
.venv/bin/python -I tools/dev/preview_char.py
.venv/bin/python -I tools/pipeline/char_validate.py --source-check
# Optional independent repeat and byte comparison:
.venv/bin/python -I tools/pipeline/bake_char.py --out-dir /tmp/chars-repeat
.venv/bin/python -I tools/pipeline/char_validate.py --repeat-dir /tmp/chars-repeat
```

The scripts use one numerical-library thread, no worker pool, and disable Python
bytecode writes before importing shared reference dependencies. Nothing is written
inside the shared `ref/` or `.venv` symlinks. `--characters mk mn` supports focused
bakes; its manifest intentionally describes only that invocation. Run the default
command again to restore the complete manifest. Validation expects the full set.

Models, clips, 24 path files and `manifest.json` go to
`sdmc-out/3ds/toontown3ds/data/chars/`. Thirty per-model contact sheets, covering
start/middle/last of every clip, and `overview.png` go to
`sdmc-out/previews/chars/`. The preview reads back actual quantized vertices and
PICA textures through the existing `preview_ttm.py`; it does not render the
original BAM geometry. Each clip is framed by its full animated bounds.

## Source mapping and animation

`char_source.py` reads the three literal tables from the pinned
`ref/open-toontown/toontown/char/Char.py:8-113` without importing the game client.
Names and codes are extracted from `toontown/char/CharDNA.py:9-27,90-127` by evaluating only its name lookup method, without importing the game package. Mid and lowest LODs
are both baked; single-LOD costume models use `default`, Donald at the wheel uses
1000. Filenames preserve CharDNA names, including `donald-wheel` and
`sockHop_daisy`.

Every AnimDict clip is included, even pointing/dance clips unused by normal walking.
There is no separate wave clip referenced by the classic-character chatter code:
`classicchars/DistributedCCharBase.py:152-206` turns with `walk`, then `neutral`.
Clip names, source paths, frame counts, source rates and actual sizes are in the
manifest. Animation path resolution deliberately matches Char.py:202-204's string
splice: a phase_3.5 prefix keeps its `.5` when an entry says phase 3.

All clips use exactly 12 Hz. Source positions at `i * source_fps / 12` are linearly
interpolated between adjacent source frames. There are `ceil(source_duration*12)`
frames, preserving duration to less than one bake tick; the final source frame is
held if necessary. Root transforms/translation in the source are retained. TTM1
and TTA1 are unchanged; no attach tracks are needed. At every sampled source
frame we assert the same batch states, vertex counts, index ordering, UVs and
colors. Shared BAM arrays contain many unused vertex rows; each batch is compacted
in sorted source-row order, with the identical mapping at every frame. The local
extractor skins in float64 by summing joint matrices in name order, then passes
geometry to the unchanged shared extractor. Panda's native float32 blend sums use
pointer order, which otherwise causes process-dependent rounding. Independent
comparisons against Panda's native skinner cover every clip. TTM output must retain the vertex count with no UV clamping. Negative
validation cases prove UV and index drift are rejected.

Open eyes are selected and closed-eye meshes hidden. Eye textures with separate
alpha are loaded; pupil batches use layer 2, eyes layer 1. The character-specific
extractor corrects Panda F_alpha masks locally without changing extract.py.
Mickey's pupil offset and the upstream ear-joint attachment fixes are applied
(Char.py:216-251,324-328). Ear meshes are baked in a fixed reference orientation;
upstream camera-dependent billboard ears and runtime blink/eye tracking are not
encoded in a vertex clip. Optional live eye/blink/billboard behavior needs separate
runtime work. M_dual becomes alpha test, per the existing extractor convention.

Clarabelle is an explicit exception: Char.py:68 references the missing
`phase_5.5/models/estate/Clara_pose2-`. The shipped actor and `listen` animation
used by `toontown/catalog/CatalogScreen.py:727-738` are baked instead, with a
`neutral` alias. It is catalog content, has no classic-character DC class or walk
graph, and is not a playground placement. Its original root-space scale is kept;
no catalog camera transform or switchboard prop is baked. No winter-specific
classic-character DNA/model/class exists in the pinned tables. Winter flags select
chatter only (`DistributedCCharBase.py:233-248`); reuse the normal visual set.

State selection:

| Character | Neutral | Walking / special |
|---|---|---|
| Mickey, Minnie, Goofy, Daisy, Chip | loop `neutral` | loop `walk`; `run` exists for Mickey/Minnie/Goofy but is not selected by their walk FSM |
| Vampire Mickey | loop `neutral` | `walk` aliases sneak; additional sneak/into_sneak/into_idle/chat/run clips retained |
| Witch Minnie, Super Goofy, Franken Donald, Sock Hop Daisy, Police Chip, Jailbird Dale | loop `neutral` | loop `walk`, no fabricated run/transition clip |
| Donald in Dreamland | play `trans`, then `neutral` at 0.5 rate | play `trans-back`, then loop `walk` |
| Donald at wheel | loop `neutral` (same source as `wheel`) | boat attachment, no autonomous walking |
| Pluto, Western Pluto | play `sit`, then loop `neutral` | play `stand`, then loop `walk` |
| Dale / Jailbird Dale | loop `neutral` | loop `walk` while following Chip's orbit track |
| Clarabelle | loop `listen` / `neutral` alias | catalog only |

Sources: `CharStateDatas.py:20-29,64-93`, `DistributedDonald.py:56-78`,
`DistributedFrankenDonald.py:36-59`, `DistributedPluto.py:57-85`,
`DistributedWesternPluto.py:12-26`, `DistributedDonaldDock.py:43-60` (all under
`ref/open-toontown/toontown/classicchars/`).

## DC classes, nametags and interaction

All leaf classes inherit DistributedCCharBase's fields below. IDs here are read
from `source/net/dc_generated.h:1265-1275,2123-2141`; runtime code must use the
generator's tables/constants rather than hardcoding these documentary values.
There is no transmitted ToonDNA, name, XYZ or smoothed movement field in these
classic-character classes. A client's class-to-asset mapping chooses the character;
`DistributedCCharBase.py:22-35` creates the DNA and name locally.

Every character has root/avatar scale **1** and nametag scale **1**
(`ref/open-toontown/otp/avatar/Avatar.py:59-60,190-206`). Heights below are the
explicit Char.py:151-185 values, not mesh bounds. Nametag origin is
`(0,0,height+0.5)` (Avatar.py:205-206). Costumes retain the familiar character's
displayed name (`DistributedVampireMickey.py:22-23` and analogous costume classes).

| Asset / DNA | DC class (ID) | Home zone(s) | Height | Nametag Z |
|---|---|---|---:|---:|
| mickey / mk | DistributedMickey (67) | 2000 | 3 | 3.5 |
| vampire_mickey / vmk | DistributedVampireMickey (68) | 2000 | 3 | 3.5 |
| minnie / mn | DistributedMinnie (70) | 4000 | 3 | 3.5 |
| witch_minnie / wmn | DistributedWitchMinnie (69) | 4000 | 3 | 3.5 |
| goofy / g | DistributedGoofy (71), DistributedGoofySpeedway (83) | 5000 / 8000 | 4.8 | 5.3 |
| super_goofy / sg | DistributedSuperGoofy (84) | 8000 | 4.8 | 5.3 |
| donald / d | DistributedDonald (78) | 9000 | 4.5 | 5 |
| franken_donald / fd | DistributedFrankenDonald (79) | 9000 | 4.5 | 5 |
| donald-wheel / dw | DistributedDonaldDock (80) | 1000, boat local | 4.5 | 5 |
| pluto / p | DistributedPluto (81) | 3000 | 3 | 3.5 |
| western_pluto / wp | DistributedWesternPluto (82) | 3000 | 4.5 | 5 |
| daisy / dd | DistributedDaisy (72) | 5000 | 4.5 | 5 |
| sockHop_daisy / shdd | DistributedSockHopDaisy (73) | 5000 | 4.5 | 5 |
| chip / ch | DistributedChip (74) | 6000 | 2 | 2.5 |
| police_chip / pch | DistributedPoliceChip (75) | 6000 | 2 | 2.5 |
| dale / da | DistributedDale (76) | 6000 | 2 | 2.5 |
| jailbird_dale / jda | DistributedJailbirdDale (77) | 6000 | 2 | 2.5 |
| clarabelle / cl | none (catalog) | none | 3 (legacy table) | 3.5 (legacy) |

`toon.dc:624-691` is authoritative for inheritance: Witch Minnie inherits Mickey
in the DC schema, while Jailbird Dale inherits Dale and its extra fields.

| Field (generated ID) | Wire arguments | Flags / direction |
|---|---|---|
| setChat (614) | uint32 category, uint32 message index, uint32 avatar doId | broadcast, server to clients |
| fadeAway (615) | none | broadcast |
| setWalk (616) | string srcNode, string destNode, int16 timestamp | required broadcast ram |
| avatarEnter (617), avatarExit (618) | none | airecv clsend |
| setNearbyAvatarChat (619) | char[0-1024], length-prefixed string | airecv clsend |
| setNearbyAvatarSC (620) | uint16 phrase | airecv clsend |
| setNearbyAvatarSCCustom (621) | uint16 custom phrase | airecv clsend |
| setNearbyAvatarSCToontask (622) | uint32 taskId, uint32 toNpcId, uint32 progress, uint8 message | airecv clsend |
| setFollowChip (623), Dale/Jailbird only | string src, string dst, int16 timestamp, int16/100 offsetX, int16/100 offsetY | broadcast ram |
| setChipId (624), Dale/Jailbird only | uint32 Chip doId | required broadcast ram |

Only the `clsend` interaction fields may be sent by the client, addressed to the
character doId, never the locally controlled Toon. Register these explicitly in
the outgoing whitelist if interaction is integrated. `avatarEnter/Exit` track an
intangible radius-8 sphere and subscribe/unsubscribe nearby chat
(`DistributedCCharBase.py:42-63,118-150`). `setChat` indexes localized CCharChatter,
substitutes `%` with the target name, and turns toward that avatar except GOODBYE.
Chat turns take the shortest heading at 180 degrees/sec; turns longer than .2 sec
loop walk then neutral. Pluto/Western Pluto/Donald generally use thought bubbles;
DonaldDock uses speech and hides its nametag. See DistributedCCharBase.py:152-206
for the April Fools exception and missing-avatar/chatter guards.

Generation fades alpha 0→1 over .5 sec. `fadeAway` fades 1→.5 over .5 sec,
loops neutral and enters costume transition with a dust cloud; it is not an object
delete (`DistributedCCharBase.py:34-36,270-290`). Object leaving still releases the
asset normally. Dust clouds/audio/chatter text are not part of these model files.

## Path file contract and exact replay

Each `<char>_<zone>.paths` is UTF-8 JSON text, with `magic: CCHAR_PATHS`, `version: 1`.
Reject other versions. Positions are Panda/Toontown world units, Z up, with heading
0 facing +Y. Store/convert coordinate axes consistently with the existing renderer.
Top-level fields are character, DNA code, zone, source selector/location, speed,
start (`a`), nodes, and edges. Nodes contain position and **directed** adjacency;
never add reverse edges. Edges contain endpoints, ordered points including both
endpoints, per-segment 3D distances, total distance, raycast flag, and nominal
seconds. All decimals retain the original Panda float32 point precision.

`char_source.path_namespace` evaluates only CCharPaths.py's data/helper AST with
imports removed and explicit Panda point/copy/localizer stubs. This preserves the
upstream helper behavior and avoids importing the game client. The exporter checks
every duration against upstream `getWalkDuration`; the validator independently
checks distance, adjacency and binary structures.

`CCharPaths.py:334-363` selects graphs by localized character identity and location.
Goofy location 0 uses the old Daisy's Gardens graph, location 1 uses Speedway;
Super Goofy always uses Speedway. Dale shares Chip's graph. The wheel character is
a singleton `a→a`, then attaches to the boat at `(0,-1,3.95)`
(`DistributedDonaldDock.py:43-47`). Clarabelle has no graph. The undefined
`__dalePaths` fallback in CCharPaths.py:383 is never evaluated for supported
non-singleton graphs; self edges return early and do not need waypoints.

Normal home routes plus all six April Fools swaps are exported:
Mickey→Daisy/5000, Daisy→Mickey/2000, Minnie→Pluto/3000,
Pluto→Minnie/4000, Donald→Goofy Speedway/8000, Goofy Speedway→Donald/9000.
The same actor and its own speed use the substituted graph. Sources are the
`handleHolidays` functions in DistributedMickey.py:91-96,
DistributedDaisy.py:88-93, DistributedMinnie.py:88-93,
DistributedPluto.py:107-118, DistributedDonald.py:96-108 and
DistributedGoofySpeedway.py:89-100.

Replay sequence (citations below are in `ref/open-toontown/toontown/classicchars/`):

1. On generate, place at node `a`, heading/pitch/roll 0
   (DistributedCCharBase.py:98-107). An empty destination or src==dest does not
   request a walk (`DistributedMickey.py:83-86` and analogous subclasses).
   The AI's initial required `setWalk` can be `('', '', 0)`
   (`DistributedCCharBaseAI.py:202-203`). Validate node names before lookup.
2. Build `[src] + waypoints + [dst]`. Waypoints reverse in reverse direction;
   the raycast flag is symmetric even though graph adjacency is directed
   (CCharPaths.py:395-435). Nominal server duration is **sum of 3D segment
   lengths / character speed** (CCharPaths.py:438-447). Speeds are exported per
   route and derive from `toontown/toonbase/ToontownGlobals.py:561-574`.
3. Apply old XY offset to the first point and new XY offset to the final point,
   leaving intermediate waypoints unchanged (CharStateDatas.py:64-73,141-146).
   Ordinary characters have offsets zero. Play any stand/trans-back start clip,
   loop walk, then execute the path. On completion the character FSM requests
   Neutral and plays any sit/trans start clip (CharStateDatas.py:74-94;
   DistributedPluto.py:57-102; DistributedDonald.py:56-91).
4. Seek the **whole sequence**, including start clip, to
   `globalClockDelta.localElapsedTime(timestamp)` (CharStateDatas.py:88-93).
   This is synchronized server time, signed-wrap 16-bit **centiseconds**, not
   seconds or raw client uptime. Compute `nowTicks=round((now-delta)*100)`,
   sign-extend `(timestamp-nowTicks)` to 16 bits, then
   `elapsed=max(0,-signedDifference/100)`. Wrap period is 655.36 sec, nearest
   signed range ±327.68 sec. See stock Panda's
   `direct/distributed/ClockDelta.py:20-21,236-268,307-319`
   (installed in `.venv/lib/python3.12/site-packages/`). Do not restart a late
   packet's path at time zero.
5. **Reproduce the actual interval construction, including its quirks** if stock
   client parity is required (CharStateDatas.py:95-123). For segment length d:
   `duration=d/speed`. Save actor HPR before building. Queue setPos(segment start)
   as a deferred Func; call headsUp(end) immediately at construction time.
   Because setPos is deferred, headsUp uses the original first-node position for
   every segment, rather than each intermediate start. With Toontown axes its
   heading is `degrees(atan2(-dx,dy))`; Panda headsUp also computes pitch toward
   the target, and keeps its upright convention. Reduce current/destination
   headings to ±180 and choose an equivalent destination nearest the current
   heading. The code sets `turnTime=abs(shortestDestinationHeading)/270`, using
   the **absolute destination angle**, not the turn delta. It then sets actor HPR
   to that destination while building the next segment. Restore initial HPR after
   building. If `duration-turnTime > .01`, append a Parallel of linear HPR lerp
   over turnTime and linear fluid position lerp over `duration-turnTime`.
   The segment therefore lasts `max(turnTime,duration-turnTime)`. If the condition
   fails, only the queued jump to segment start remains. A route's real duration
   can differ from the nominal server duration; exported `nominal_seconds` must
   not be mistaken for interval duration. When turn finishes first, hold target
   HPR; when position finishes first, hold end position. Do not silently replace
   this with an ideal constant-speed curve.
6. When the route flag is set, enable a downward ground ray while traversing and
   disable it afterward (CharStateDatas.py:97-98,121-122). The floor handler uses
   FloorOffset, reach 10 and maxVelocity 0 (DistributedCCharBase.py:51-63).
   Use runtime collision to follow ramps rather than flattening all path Z values.

Dale and Jailbird Dale need the `follow_chip` metadata in their path files.
Resolve `setChipId` before constructing their track, retaining updates while Chip
is not generated yet (`DistributedDale.py:44-83`). Initial offset is `(3,0)`;
radius is 3 except edges b↔c and e↔f, which use 2.5
(CCharPaths.py:330-331; CharStateDatas.py:153-166). Track duration is Chip's actual
walk sequence duration, not Dale's nominal distance/speed. Let src/dest theta be
atan2 of the remembered node offsets, normalized to [0,2π). Start with
`revs=ChipRouteDistance/13`; add `(2π-srcTheta)/2π` if srcTheta>0, then add
`(destTheta - (srcTheta + (revs%1)*2π))/2π`; add whole revolutions until revs≥1.
At normalized t, subtract remembered src/dest offsets from the corresponding
already-offset endpoint positions, interpolate those centers linearly (intermediate
waypoints are not followed by the orbit center), and add
`radius*(cos(theta),sin(theta))`, where `theta=srcTheta+t*revs*2π`.
Set heading to `degrees(theta)%360`, with no tangent correction. This matches
CharStateDatas.py:167-227, including its endpoint-center calculation. `setWalk`
and `setFollowChip` feed the same Dale walk state; preserve the offset arguments
in `setFollowChip` (`DistributedDale.py:80-94`).

## Integration and budgets

The main agent must add classic-character object dispatch and generated required
field handling; use `.ttm` plus corresponding `.tta` as one whole actor, with
ordinary model world transforms and the path state machine above. No Python,
BAM/DNA parser, new dependency or skeletal skinning is needed at runtime. Decode
`.paths` offline into a fixed C++ table or a binary zone section before shipping;
JSON is a readable interchange format, not a proposal for per-frame parsing.
Keep optional interaction fields in the outgoing allowlist. Add a separate asset
pool/streaming policy before enabling characters alongside the current toon pool.

The 2,000-triangle / 12-batch target is met by all playground characters at every
exported LOD (maximum 1,746 triangles, 8 batches). Catalog Clarabelle is the only
exception: 2,065 triangles / 12 batches, 65 triangles above target; upstream ships
no lower LOD. It remains optional catalog content. Its listen clip is 1.32 MiB per alias after compaction. Both aliases are
byte-identical and can share one resident animation. The largest individual model
is 206.6 KiB. Ordinary characters can load only an active clip, or stream two
adjacent morph frames for tighter budgets. Do not preload all 15.26 MiB of
animations into the 4 MiB shared toon pool. The complete model set is 1.44 MiB on
SD, with duplicated textures between LODs; only resident characters count against
linear memory. The renderer can share textures and release unused LODs. Full-clip
loading can fit one character, but the main agent must measure combined local/
remote Toon, character and transition allocations before enabling the feature.

The following measured sizes include TTM texture payload/mips and binary alignment.
KiB means 1024 bytes. Clip cells list every output clip as `name: KiB`.

| Model | Vertices | Triangles | Batches | Model KiB | Each clip KiB |
|---|---:|---:|---:|---:|---|
| mickey_800 | 714 | 834 | 7 | 58.4 | walk: 67.1; run: 44.8; neutral: 189.9; left-point-start: 106.1; left-point: 184.2; right-point-start: 106.1; right-point: 184.2 |
| mickey_400 | 419 | 399 | 6 | 51.0 | walk: 39.5; run: 26.4; neutral: 111.5; left-point-start: 62.4; left-point: 108.2; right-point-start: 62.4; right-point: 108.2 |
| vampire_mickey_800 | 769 | 1032 | 6 | 67.0 | walk: 192.4; run: 54.2; neutral: 192.4; sneak: 192.4; into_sneak: 72.2; chat: 54.2; into_idle: 72.2 |
| vampire_mickey_400 | 551 | 678 | 5 | 61.2 | walk: 137.9; run: 38.9; neutral: 137.9; sneak: 137.9; into_sneak: 51.9; chat: 38.9; into_idle: 51.9 |
| minnie_800 | 727 | 799 | 8 | 64.2 | walk: 91.0; run: 40.0; neutral: 261.5; left-point-start: 159.2; left-point: 159.2; right-point-start: 159.2; right-point: 159.2; up: 45.6; down: 34.2; left: 45.6; right: 34.2 |
| minnie_400 | 491 | 486 | 8 | 58.8 | walk: 61.5; run: 27.0; neutral: 176.6; left-point-start: 107.6; left-point: 107.6; right-point-start: 107.6; right-point: 107.6; up: 30.9; down: 23.2; left: 30.9; right: 23.2 |
| witch_minnie_800 | 889 | 1093 | 4 | 42.2 | walk: 173.9; neutral: 194.6 |
| witch_minnie_400 | 633 | 711 | 3 | 36.0 | walk: 123.9; neutral: 138.6 |
| goofy_1000 | 861 | 1049 | 3 | 36.1 | walk: 94.4; run: 54.0; neutral: 168.4 |
| goofy_500 | 502 | 565 | 3 | 27.6 | walk: 55.1; run: 31.5; neutral: 98.2 |
| super_goofy_800 | 896 | 1169 | 3 | 31.9 | walk: 147.1; neutral: 539.1 |
| super_goofy_400 | 545 | 627 | 2 | 23.4 | walk: 89.6; neutral: 328.0 |
| donald_500 | 517 | 564 | 4 | 65.4 | walk: 72.9; trans: 97.1; neutral: 72.9; trans-back: 72.9 |
| donald_250 | 315 | 298 | 4 | 60.6 | walk: 44.5; trans: 59.2; neutral: 44.5; trans-back: 44.5 |
| franken_donald_default | 1019 | 1412 | 2 | 35.4 | walk: 262.9; neutral: 477.9 |
| donald-wheel_1000 | 755 | 978 | 5 | 50.0 | wheel: 141.8; neutral: 141.8 |
| pluto_500 | 616 | 594 | 4 | 32.2 | walk: 57.9; sit: 72.4; neutral: 57.9; stand: 53.1 |
| pluto_300 | 415 | 391 | 4 | 28.0 | walk: 39.1; sit: 48.9; neutral: 39.1; stand: 35.9 |
| western_pluto_800 | 626 | 887 | 4 | 36.9 | walk: 98.0; sit: 107.8; neutral: 479.5; stand: 98.0 |
| western_pluto_400 | 395 | 507 | 3 | 31.0 | walk: 61.9; sit: 68.1; neutral: 302.6; stand: 61.9 |
| clarabelle_static | 1429 | 2065 | 12 | 206.6 | neutral: 1351.0; listen: 1351.0 |
| daisy_800 | 713 | 856 | 5 | 38.1 | walk: 94.9; neutral: 273.1 |
| daisy_400 | 420 | 424 | 4 | 30.8 | walk: 56.0; neutral: 161.0 |
| sockHop_daisy_default | 1136 | 1590 | 2 | 38.1 | walk: 151.0; neutral: 435.0 |
| chip_500 | 689 | 952 | 5 | 45.9 | walk: 43.2; neutral: 161.6 |
| chip_250 | 371 | 444 | 5 | 37.9 | walk: 23.4; neutral: 87.1 |
| dale_500 | 817 | 1146 | 5 | 45.0 | walk: 70.4; neutral: 191.6 |
| dale_250 | 455 | 567 | 5 | 35.9 | walk: 39.2; neutral: 106.9 |
| police_chip_default | 1106 | 1648 | 3 | 48.9 | walk: 78.0; neutral: 268.0 |
| jailbird_dale_default | 1252 | 1746 | 3 | 51.6 | walk: 107.8; neutral: 303.4 |

Path report (durations and waypoint coordinates are in each file):

| Path file | Nodes | Directed edges | Speed (units/sec) |
|---|---:|---:|---:|
| mickey_2000.paths | 9 | 20 | 5 |
| vampire_mickey_2000.paths | 9 | 20 | 1.15 |
| minnie_4000.paths | 26 | 56 | 3.2 |
| witch_minnie_4000.paths | 26 | 56 | 1.8 |
| goofy_5000.paths | 17 | 34 | 5.2 |
| goofy_8000.paths | 11 | 22 | 5.2 |
| super_goofy_8000.paths | 11 | 22 | 1.6 |
| donald_9000.paths | 13 | 24 | 3.68 |
| franken_donald_9000.paths | 13 | 24 | 0.9 |
| donald-wheel_1000.paths | 1 | 1 | 0 |
| pluto_3000.paths | 5 | 11 | 5.5 |
| western_pluto_3000.paths | 5 | 11 | 3.2 |
| daisy_5000.paths | 17 | 34 | 2.3 |
| sockHop_daisy_5000.paths | 17 | 34 | 2.3 |
| chip_6000.paths | 11 | 27 | 3 |
| police_chip_6000.paths | 11 | 27 | 3 |
| dale_6000.paths | 11 | 27 | 3.5 |
| jailbird_dale_6000.paths | 11 | 27 | 3.5 |
| mickey_5000.paths | 17 | 34 | 5 |
| minnie_3000.paths | 5 | 11 | 3.2 |
| daisy_2000.paths | 9 | 20 | 2.3 |
| donald_8000.paths | 11 | 22 | 3.68 |
| goofy_9000.paths | 13 | 24 | 5.2 |
| pluto_4000.paths | 26 | 56 | 5.5 |

## Offline test record — 2026-10-23

- Full bake: 18 CharDNA types, 30 models, 112 exact-12-Hz clips, 24 path files,
  644 exported directed edges. All supported AnimDict entries are present.
- Independent repeat: SHA-256 equality for all 167 files (models, clips, paths,
  manifest), including across separate Python processes.
- Source reference: every clip sampled at source frame 7 (or its last frame if
  shorter), all 30 models covered. Deterministic skinning/compaction differs from
  Panda's standard CPU skinner by at most 5.96046448e-7 world units. The baker also
  checks batch state, UV/color, index order and vertex count at every sampled frame.
- Structural validation: every TTM index is in its batch's vertex range; sections,
  texture/mip sizes, versions, TTA frame counts/strides, finite quantization scales
  and exact 12 Hz checked. Negative index/UV drift cases are rejected.
- Paths: node membership, directed adjacency, endpoints, 3D distances, nominal
  durations and raycast flags checked. Export checks upstream helper durations;
  independent validation recomputes distances from serialized coordinates.
- Preview: 30 contact sheets, 336 clip-frame views, plus overview, rendered through
  TinyOffscreenGraphicsPipe from final quantized SD assets. Visually reviewed the
  overview and representative Mickey, Pluto and Clarabelle sheets. Full-animation
  bounds prevent sitting/standing and pointing poses from being clipped.
- Memory reduction: unused source rows removed without deleting triangles; model
  files total 1.4415 MiB and animations total 15.2582 MiB. Earlier uncompact data
  needed 2.4407 / 85.3284 MiB respectively.
- No emulator/hardware run (task explicitly prohibits Azahar/make). FPS, heap,
  linear memory and VRAM measurements are **not available**. This record does not
  claim runtime integration, motion/network parity testing or hardware budgets.

## Handoff

Only the six new source/documentation files should be cherry-picked. Generated
assets and previews stay in this worktree's ignored `sdmc-out/`; rerun the commands
above in another checkout to reproduce them. Existing `ref/` symlink/gitlink status
and `.venv` were not staged. Upstream assets remain outside the MIT license under
the repository's existing THIRD_PARTY_NOTICES.md; no upstream source files are
vendored by this change.

## Runtime chatter

### Implementation summary
1. **Chatter table generator (`tools/gen_cchar_chatter.py`)**:
   - Parses `toontown/classicchars/CCharChatter.py`, `toontown/toonbase/TTLocalizerEnglish.py`, and `toontown/toonbase/ToontownGlobals.py` purely with Python `ast` (no client package imports).
   - Emits `source/ui/cchar_chatter_data.h` and `source/ui/cchar_chatter_data.cpp` with 78 unique chatter sets and 547 phrases across 17 character groups and 16 holidays.
   - Tables map character DC class ID and active holiday ID to `GREETING (0)`, `COMMENT (1)`, and `GOODBYE (2)` phrase lists.
   - Runtime substitution replaces `%` with the target avatar's name into a caller-supplied fixed buffer without heap allocation.
2. **Runtime balloon presentation and turning (`source/world/cchar.*`, `source/net/client.*`)**:
   - `EventType::CCharChat` is parsed from incoming `setChat(category, msgIndex, avatarDoId)` (DC field 614) in `Client::applyCCharField` and queued via `Client::pushEvent`.
   - When received by `CCharActors::handleEvent`:
     - Looks up chatter and formats phrase with target avatar name (or empty string if avatar unknown/unnamed).
     - Displays phrase in the character's balloon via `balloons.addSpeaker` with a duration of 6.0 seconds.
     - Turns the character toward the target avatar's position at 180 °/s (shortest angular difference). In parity with upstream `DistributedCCharBase.setChat`, turns are skipped for `GOODBYE` (`category == 2`).
     - *Thought bubble note*: Pluto, Western Pluto, and Donald use thought bubbles upstream (`DistributedPluto.py`, `DistributedDonald.py`). A speech balloon is used here; visual bubble styling can be added as a future polish item.
3. **Dale follows Chip (`source/world/cchar.*`, `source/net/client.*`, `tools/dcgen/dcgen.py`)**:
   - Added `setFollowChip` (DC field 623) and `setChipId` (DC field 624) to `WANTED` for `DistributedDale` and `DistributedJailbirdDale` in `tools/dcgen/dcgen.py`.
   - Regenerated `source/net/dc_generated.h`; the DC hash remains 547155168.
   - `Client` tracks `chipId`, `offsetX`, `offsetY`, and `hasFollowChip` on classic character state.
   - `CCharActors` implements exact Dale orbit kinematics in `computeDaleOrbit`:
     - Initial node offset `(3, 0)` on spawn.
     - Node offset tracking on each leg; src/dest theta normalized to `[0, 2π)`.
     - Orbit distance 3.0 units, overridden to 2.5 units on edges `b <-> c` and `e <-> f`.
     - Revolutions calculated as `chipDistance / 13.0` plus src/dest theta phase adjustments, with `revs >= 1`.
     - Center linearly interpolated between adjusted endpoints `chipSrc = pts[0] - sOff` and `chipDst = pts[-1] - dOff`, with heading strictly `degrees(theta) % 360`.
     - Leg duration matched to Chip's walk sequence duration.

### What was tested
- **Generator reproducibility**: `tools/gen_cchar_chatter.py` produces identical deterministic outputs and regenerates cleanly.
- **Host test suite (`tools/cchar_test/run.py`)**:
  - Compiled and executed on host using `c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined`.
  - Chatter lookup: verifies exact strings for Mickey default and holiday overrides (April Fools, etc.), Pluto default (none) vs holiday overrides, Donald Dock, out-of-bounds guards, and `%` name substitution into fixed buffers.
  - Walk interval timing: verifies `buildSegments` turn time, move time, segment length, and heading angles against pure Python reference calculations across 101 directed edges from real `.paths` JSON files (`mickey_2000.paths`, `chip_6000.paths`, `daisy_2000.paths`, `goofy_5000.paths`).
  - Dale orbit: verifies `computeDaleOrbit` position (x, y, z) and heading (h) against pure Python reference calculations across 6 time samples for leg `a -> b` of `dale_6000.paths`.
- **devkitARM syntax check**: Zero warnings with `-Wall -Wextra` on all modified/generated files (`cchar_chatter_data.cpp`, `client.cpp`, `cchar.cpp`).

### What is left for the main agent
- Classic-character event processing is dispatched via `cchars.handleEvent(ev, net, nowMs)`. The main agent should ensure `cchars.spawn` / `cchars.loadGraph` are invoked when entering zones containing classic characters.
- Outgoing whitelist for interaction fields (`avatarEnter`, `avatarExit`, `setNearbyAvatarChat`, `setNearbyAvatarSC`) if interactive proximity talk is enabled in later phases.
- Optional rendering distinction between thought bubbles and speech balloons for Pluto and Donald.

