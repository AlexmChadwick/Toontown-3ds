# Cog bosses: protocol, native VP and follow-on plan

This is an implementation contract for the unmodified sv1.0.47.38 server, not a
Panda3D runtime port. References below are paths under `ref/ref/open-toontown`.
The native VP and CFO are implemented; CJ and CEO have generated boss-level receive
schemas and the plans below, but no playable native place yet. Azahar/hardware
verification remains outstanding by task instruction.

## Entry, authority and departure

| Department | Exterior | Lobby | Elevator class | Stock progression |
|---|---:|---:|---|---|
| Sellbot | 11000 | 11100 | DistributedVPElevator | Sellbot suit parts and promotion merits |
| Cashbot | 12000 | 12100 | DistributedCFOElevator | Cashbot suit and Cogbucks |
| Lawbot | 13000 | 13100 | DistributedCJElevator | Lawbot suit and jury notices |
| Bossbot | 10000 | 10100 | DistributedBBElevator | Bossbot suit and stock options |

The suit/merit system is defined by `coghq/CogDisguiseGlobals.py`,
`toon/DistributedToonAI.py::readyForPromotion` and the lobby client. The actual
`building/DistributedBossElevatorAI.py::checkBoard` checks configured `minLaff`
and `readyForPromotion(dept)`: merits must reach `getTotalMerits`. Do not invent
an additional server suit check. `suit/DistributedBossCogAI.py::isToonWearingRentalSuit`
separately classifies paid/complete versus rental suits and changes encounter
composition. The native VP currently renders the player's ordinary toon, not a
Cog-disguise costume. Stock ready/not-ready and rental/paid entry must both be
verified; they must not be conflated.

`coghq/LobbyManagerAI.py::createBossOffice` creates a dynamic boss zone when the
boss elevator closes. `DistributedBossElevator.setBossOfficeZone` normally applies
to seated riders; `setBossOfficeZoneForce` also supports a forced handoff. The
native client handles both, hides during transit, changes its Astron interest,
then follows the generated VP object. No boss zone is guessed. The lobby boarding
anchor is baked from `SellbotHQLobby/elevator_locator`, currently
`(0, -82.356003, 28.000799)`. Walk within five units and press A; B exits a seated
lobby elevator. Existing shared CogBuildings can also complete its normal handoff.

The AI owns phases, membership, attacks, damage, rewards and promotions. New boss
senders use `TT_NET_UPDATE` with compile-time `clsend` assertions and signature
checks. Existing own-avatar movement/animation/parent APIs retain their existing
whitelist. No boss phase, HP, suit ownership or merit value is sent by this work.

Victory returns to the entry HQ lobby (11100, fallback 11000), as requested by
this task. Zero-laff departure returns to Daisy Gardens (5000). This differs from
upstream `DistributedBossCog.localToonToSafeZone`, which selects the avatar's
configured default safe zone. `BossBattle::leave` restores place collision,
interest, avatar parent and battle/UI state after a GPU fence.

## Common receive and send surface

DC identities and signatures come from `etc/otp.dc` and `etc/toon.dc` through
`tools/dcgen/dcgen.py`; hash **547155168** is unchanged. No hand-written field IDs.
`net/boss_state.*` is a platform-independent, transactional decoder. It accepts
only fields belonging to the actual generated boss class. Membership and sides
are bounded at eight, barrier lists at eight, phase strings at 31 characters.
Malformed/truncated fields leave the last complete BossState unchanged.

| Receive field | Native meaning |
|---|---|
| setDNAString | bounded boss DNA |
| setToonIds(toons, sideA, sideB) | participant and battle-side membership |
| setBattleIds(number, A, B), setArenaSide | current Cog rounds and surviving side |
| setState(string) | AI phase, local transition epoch |
| setAttackCode(u8, toonId) | attack and actual target; revision restarts effects |
| setBarrierData(BarrierData[]) | nested length-delimited context/name/avatar lists |
| setBossDamage | VP/CJ/CEO: u16 damage, u8 recovery/minute, i16 network time; CFO: u16 only |
| setBattleExperience | generated descriptor skips the eight-toon reward payload safely |
| VP setCagedToonNpcId, setDooberIds, doStrafe, toonPromoted | cage NPC, promotion actors, hatch/gear sweep, promotion result |
| own toon setPieType, setNumPies | authoritative pie stock; 65535 means unlimited |

Damage recovery uses converted/wrapped network timestamps and clamps at zero.
Barrier acknowledgements echo the actual pending context only when the local
avatar is listed. Unknown barriers are never acknowledged. Leaving/resetting a
zone clears the boss snapshot and pie stock. A disconnect during the VP selects
the static Sellbot lobby for reconnect instead of the abandoned dynamic office.

The common clsend duties are `avatarEnter/Exit`, `avatarNearEnter/Exit`,
`setBarrierReady(context)` and `zapToon(posHpr, normalizedDirection, attack, time)`.
VP adds `touchCage`, `hitBossInsides`, `hitBoss(1)`, `hitToon(otherAvatarId)` and
`finalPieSplat`. Senders require current membership and appropriate phase;
`hitBoss` additionally requires `BossCogDizzyNow`. The stock AI rejects arbitrary
damage amounts: **each accepted body pie sends exactly one**. Pie hits on another
participant request server-owned healing; self-healing is rejected locally.
See `DistributedSellbotBossAI.hitBoss/hitBossInsides/hitToon/touchCage/finalPieSplat`.

`DistributedBattleFinal` adds `setBossCogId`, `setBattleNumber`, `setBattleSide` and
its own inherited `setBarrierData`. Its **ReservesJoining** barrier belongs to the
battle object, not the boss. The native controller acknowledges it after the
reserve arrival window. Battle membership gates this sender independently.
Round locations match `DistributedBossCogAI.setBattlePos` and
`ToontownGlobals.BossCogBattleAPosHpr/BPosHpr`.

## VP phase sequence and controls

Sources: `suit/DistributedBossCog{,AI}.py`,
`DistributedSellbotBoss{,AI}.py`, `BossCog.py`,
`battle/DistributedBattleFinal{,AI}.py`, and `toonbase/ToontownGlobals.py`.

| AI state | Native work / completion |
|---|---|
| WaitForToons | register arrival; acknowledge after 0.5 s even while SD assets load; stock barrier is 5 s |
| Elevator | lock movement, arena view; acknowledge after 6 s (stock timeout 30 s) |
| Introduction | condensed speech, turn and wave movie; acknowledge at 20 s (stock timeout 45 s) |
| BattleOne | assigned DistributedBattleFinal through public Battle API; ordinary gag UI/movies/rewards |
| RollToBattleTwo | 8 s roll from `(0,-35,0)` to `(0,60,18)`, correct side ramp |
| PrepareBattleTwo | cage at 63, side ramp, 4 s presentation then barrier |
| BattleTwo | second assigned Cog round through the same Battle API |
| PrepareBattleThree | cage to 25, central ramp extended, 8 s presentation |
| BattleThree | free movement, jump, pie aiming/throws, gears, stomp, swats, body contact and damage/recovery |
| NearVictory | boss at death point; a real pie collision sends finalPieSplat |
| Victory | held-last-frame fall movie; acknowledge after it has played, within stock 10 s timeout |
| Reward | AI assigns experience, SOS calls and any promotion; 4 s reward presentation |
| Epilogue | cage lowers 25→18 over 0.5 s, door opens over next 0.5 s; rescued NPC waves; A returns after movie |

Cage stops are `100,81,63,44,25,18`; one-sided Cog rounds use the extra stop.
The VP rolls along the two stock damage segments to `(0,-110,-6.5)` and
`(0,-175,-6.5)`, weighted by segment length and damage/100. The three ramps use
their original local-X 18-unit retraction, transformed into arena coordinates.
Each ramp has a separate collision world; dynamic floor queries follow the exact
rendered offset. The static arena excludes cage and ramps.

Pie controls: Circle Pad moves/turns, A jumps, B charges while held and throws on
release, D-pad up/down changes pitch; tapping the lower content area throws a
half-power pie. L/R retains the ordinary camera orbit. Touch the lowered cage for
stock. Charge oscillation comes from `LocalToon.__getPiePower`; flight comes from
`Toon.getTossPieInterval`: power 0 travels 100 units in 1 second, power 1 travels
30 units in 1.5 seconds, default gravity 32.174. Launch waits the stock 16/24 s
release offset. Fixed pools hold 16 pies and 16 gears; swept tests prevent tunneling.

`BossCog` attack identities are named wire constants. A front/rear strafe opens
its hatch and emits 4–10 gears; sweep duration falls from 5 to 1 seconds with
damage. A low hatch hit stuns; body hits while stunned push the VP. Directed gears
aim at the AI-selected toon, and area stomp checks airborne state at 1.21 s.
The native body/hatch/swat volumes are conservative simple solids, not Panda's
particle collision meshes. Validate their alignment and dodge windows in Azahar.
HP changes remain server-owned. No particle engine or boss-specific audio was
added. Intro dialogue/camera choreography is condensed, and ordinary toon costumes
are retained. Those visual differences do not change AI phase ownership.

This task requires new boss traffic to be clsend. Accordingly, flying local pies
are drawn locally; this patch does **not** add the toon ownsend broadcasts
`presentPie/tossPie/pieSplat`. Peers still receive the AI's stun/damage/heal results
and ordinary avatar movement. A future explicit extension can add those cosmetic
broadcasts through the existing own-avatar whitelist.

## Assets, formats and bounded loading

Run `.venv/bin/python -I -B tools/pipeline/bake_boss.py` in this worktree. It disables
Panda's BAM cache, uses three ETC workers and one BLAS thread, and writes only
`sdmc-out/3ds/toontown3ds/data/boss/`. All outputs there are gitignored.

- `vp_{legs,torso,head,treads}.ttm`: original attached BossCog parts.
- `vp_<part>_<clip>.tta`: 22 stock clips plus `hatch_front` / `hatch_rear`, **96 TTA1 files**, sampled at 12 fps. Legs→torso uses joint_pelvis; torso→head uses joint34; treads follow joint_axle.
- `vp_arena.ttm/.ttc`: BossRoomHQ including the stock cogHQ_elevator replacement.
- `vp_cage.ttm`, `vp_cage_door.ttm`, `vp_ramp_{a,b,c}.ttm/.ttc`.
- `vp_gear.ttm`, `vp_pie.ttm`.
- `vp_lobby_anchor.txt`: `BPA1 1` followed by three floats.
- `vp_scene.bin`: BSC1, u16 version=1/count=3, three float3 ramp offsets and float3 door hinge.
- `vp_npcs.bin`: BND1, u16 version=1/count=41, records u32 NPC ID + 15-byte stock ToonDNA. Uses existing shared toon assets/cache; it adds no duplicate toon meshes.
- `vp_manifest.json`: every output hash, clip dimensions, model costs and aggregate budgets. A checked-in copy is `boss_assets.json` beside this spec.

Sources: phase_9 BossCog/sellbotBoss parts and BossRoomHQ, phase_3.5 tart, and
`NPCToons.createLocalNPC/getRandomDNA` for the rescued toon. All textures are at
most 128², with ETC1/ETC1A4 or the existing palette formats. Model and TTA vertex
orders include the writer's UV unwelding. Weighted skin matrices use stable double-precision accumulation, checked against
Panda within 0.001 units; pose floats are canonicalized before quantization. Two
complete processes produced byte-identical results for all 115 files and the manifest.

The native runtime reuses AssetLoader's existing core-0 thread; no new thread is
created. It retains one four-part clip set and at most one pending set. All four
pending parts publish together after FrameBegin's GPU fence; old clips are freed
there. No whole animation library is resident. Preflight validates headers,
vertex counts, frames, offsets and aggregate budgets. Missing/over-budget assets
show a bottom-screen error with B to return. The lobby is unloaded before loading
the arena. Stereo is disabled for this encounter and restored by normal place UI.

| Asset allocation | Bytes | Limit |
|---|---:|---:|
| All model geometry | 114,632 | part of zone geometry |
| Largest four-part clip | 2,647,296 | 2,800,000 runtime cap |
| Geometry + active/pending largest clips | 5,409,224 | 6,291,456 |
| All model textures | 364,000 | 8,388,608 |
| Geometry + clips + textures | 5,773,224 | linear asset residency, excluding shared actors/UI/audio |
| Four collision worlds, heap incl. stamps | 31,568 | regular heap |
| NPC DNA file | 787 | regular/file data |
| Complete disk output | 26,098,096 | streamed from SD |

Unique model content totals 2,588 triangles / 42 batches. This is **not** a
per-frame stress measurement: repeated pies/gears, Cogs and toons add draws.
Toon/NPC looks remain under the existing shared 4 MiB cache cap. Attachment tracks
add at most 13,824 heap bytes for an active/pending pair. GPU/frame/whole-app peak
numbers require Azahar and then hardware measurement.

## CFO implementation (host/build verified; Azahar pending)

Sources: `suit/DistributedCashbotBoss{,AI}.py`, `suit/DistributedCashbotBossGoon{,AI}.py`,
`coghq/DistributedCashbotBoss{Crane,Object,Safe,Treasure}{,AI}.py`,
`ToontownGlobals.py`, and `etc/toon.dc` in the pinned Open Toontown reference.

The existing BossBattle frame hook now handles both VP and CFO. CFO entry uses
CashbotLobby **12100**, its BPA1 elevator anchor and `DistributedCFOElevator`.
Normal slots and forced boss destinations both enter the server's dynamic zone.
It registers/acknowledges WaitForToons while SD loading, plays condensed intro
and preparation scenes, uses the public Battle API for BattleOne and its reserve
barriers, then handles BattleThree cranes, Victory, Reward and Epilogue. Reconnect
selects static lobby 12100; successful exit returns to CashbotHQ **12000** and
zero-laff exit uses the existing sad playground behavior.

`client_cfo.cpp` stores a bounded, transactional child-object snapshot pool:
4 cranes, 9 safes, 16 goons and 32 treasures. The CFO goons are independent
`DistributedCashbotBossGoon` objects. No level entity or facility code is used.
Required and RAM fields arrive in generated DC order; truncated updates do not
publish. Each child leaves independently, and reset/disconnect clears the pool.
Every new sender uses the compile-time clsend `TT_NET_UPDATE` assertion, boss
membership/phase checks and ownership checks where applicable. Hash: 547155168.

| Object | Receive contract | Client sends |
|---|---|---|
| Crane | bossId:u32, index:u8, state:(char,u32 owner), cable:(u8 sequence,i16/100 Y,u16%360/100 H,3 fixed i16/100 XYZ links,i16 time), clearSmoothing:i8 | requestControl, requestFree, owner-only clearSmoothing(0), owner-only setCablePos at 5 Hz |
| Object / Safe | bossId:u32, objectState:(char,u32 owner,u32 crane), separate position/components and smoothed molecules; Safe adds index:u8 | requestGrab, requestDrop, hitFloor, requestFree(i16/10 XYZ,u16%360/100 H), hitBoss(u16/255 impact); Safe requestInitial |
| Goon | Object fields plus velocity:u8/10, HFov:u8, radius:u8, strength:u8, scale:u8/50, setupGoon molecule, target:(i16/10 XY,u16%360/100 H,i16 arrival), destroyGoon | requestBattle/requestStunned(i16/10 pause), object grab/drop/hit, destroyGoon after an owned thrown safe intersects it |
| Treasure | position/finalPosition:i16/10 XYZ, goonId:u32, style:u16, grab owner:u32, rejection | requestGrab only while injured and physically in range |
| CFO | Common BossCog fields, damage:u16 only, rewardId:u16 | Common entry/exit/barriers/zapToon; applyReward after explicit Epilogue confirmation |

Controls: walk/jump normally; **A near a free crane** requests control. Circle Pad
slides the arm (8–25) and rotates it (-45–45 degrees); **B toggles the magnet**,
so switching it off requests a drop; **X releases the crane**. Only the AI's
state-owner message grants ownership. The deterministic three-link cable uses
at most four 1/60-second substeps, gravity 32, length constraints and bounded
arrays. Spectators interpolate the three transmitted link positions. A dropped
object inherits 1.5 times magnet velocity and broadcasts its actual pose at 5 Hz.
A swept CFO body intersection computes normalized velocity in the crane's root
frame and packs the resulting impact as u16/255. No timer sends boss damage.

Jumping onto goon heads requests a stun; their radar cones request a battle/zap.
Walking goons can also be grabbed and wiggle free after two seconds. Stunned
goons use the collapse pose; walking/recovery clips and cone are loaded from the
**existing** `level/props/goon_pg_full*` and `goon_cone.ttm` files. Safes use I/F/G/D/s
states and server ownership; boss-owned G is the visible helmet. The AI decides
whether a safe dons/removes the helmet or damages the stunned CFO, and whether a
goon damages it. Health displays the server's 0–500 damage. Slow directed attack
11 is a physical gear projectile; electric contact also uses the guarded zap path.

The ending plays recovery, roll-out and the stock 15-car train timing. Reward
barriers precede the Resistance toon. **A confirms applyReward once; a second A
returns to HQ**. The AI owns Resistance inventory/effect updates. The NPC uses
stock seed 146392, female, `pls` head DNA; it shares the regular Toon cache.

`bake_boss.py --cfo` emits the CFO-prefixed SD set and cfo_manifest.json. MidVault
and EndVault are combined because their measured geometry plus all props is only
about 190 KiB; doors are open. Both vaults fit together in the enforced budget.
Four parts share the deterministic VP extraction/attachment convention; models
and clips retain TTM1/TTA1. Textures are ETC1/ETC1A4 or small native channels,
maximum 128 px. Metadata is BPA1/BND1. See `cfo_assets.json` for exact sizes/hashes.


| CFO asset admission | Bytes | Limit |
|---|---:|---:|
| All 16 new models, geometry | 194,132 | included below |
| Largest four-part clip | 2,004,480 | 2,004,480 |
| Active + pending clips, models, reused-goon reserve | 6,038,100 | 6,291,456 |
| New model textures | 658,688 | 8,388,608 |
| Textures including reused-goon allowance | 789,760 | 8,388,608 |
| Collision heap | 12,348 | regular heap |
| SD output, 115 files / 96 clips | 20,207,009 | streamed from SD |

The model inventory totals 4,918 unique triangles / 80 batches. These include
mutually exclusive ending props and do not describe an eight-Toon/16-goon frame;
measure instanced draw/triangle counts in the Azahar stress check.

Goons are reused, never rebaked. Their original files are absent in this checkout;
the main agent must install the existing facility goon set. Runtime preflight
rejects malformed/missing models or goon clips, with B return to HQ.

Budget admission reserves 1.5 MiB for the three resident goon clips, plus a
256 KiB goon model/cone allowance in the offline report. The runtime measures
actual model headers, caps every four-part CFO clip at 2,004,480 bytes and keeps
only active/pending sets. Publication and retirement use the existing GPU frame
fence and background loader. No extra thread or runtime format is introduced.

Visual/physics limits requiring review: condensed dialogue/movies, ordinary Toon
costumes, open vault doors, approximate gear/body/radar volumes, constrained-link
physics rather than Panda's collision integrator, and simplified floor sliding. All treasure styles currently share the ice-cream
visual and appear at their final position; style/healing authority stays on the AI.
The ending camera and Resistance UI present the essential sequence; full stock
cinematography and audio are not reproduced. Host tests cannot establish a live
victory or Old 3DS performance. The Azahar checklist below is mandatory.

## CJ plan (not implemented)

Sources: `DistributedLawbotBoss{,AI}.py`, `DistributedLawbotCannon{,AI}.py`,
`DistributedLawbotChair{,AI}.py`, `DistributedLawbotBossGavel{,AI}.py`,
`DistributedLawbotBossSuit{,AI}.py`, and Lawbot Globals / DC definitions.

1. Bake phase_11 lawbotBoss, LawbotCourtroom3, scale, LB_Elevator, cannon,
   juror chairs and gavels as separate movable models with matching collision.
2. Follow common Cog BattleOne, RollToBattleTwo (50 s), PrepareBattleTwo
   (45 s), BattleTwo (jury cannons), optional RollToBattleThree (20 s),
   PrepareBattleThree (45 s), BattleThree (evidence/scales), Victory or Defeat,
   Reward/Epilogue. Acknowledge only the listed actual barrier, not every state.
3. Generate Cannon, Chair, Gavel and BossSuit schemas. Cannon receives index,
   pose, movie, cannon updates and timed fire. requestEnter/requestLeave gates
   local controls; setCannonPosition and setCannonLit use i16/10 angles;
   setLanded is sent after the client-owned flight really lands. Port the stock
   jury ballistics and seat collision, with at most LawbotBossCannonBallMax=12
   airborne balls and chair ownership driven by setToonJurorIndex/state.
4. Scale starts at 1350 of 2700. Eight jurors balance it; each extra/missing juror
   adjusts the start by the Globals 68 units. Difficulty and bonus states alter
   evidence weight, so never hardcode VP's damage. Track lawyer IDs, taunts,
   bonus entry, gavel states and suit doAttack/doProsecute/doStun.
5. touchWitnessStand grants evidence. Real evidence impacts call hitBoss(1),
   healBoss, hitDefensePan/hitProsecutionPan, hitChair(index, result), or
   hitToon(other), as the AI method requires. Lawyer hits use the lawyer object's
   hitByToon. Keep pans, evidence and stun timers in bounded pools.
6. Defeat is a distinct AI state (10 s barrier), not Victory with a negative
   gauge. Reward grants the AI's Cog summon outcome; render the owner updates.

Acceptance: two-client cannon ownership, all juror seat outcomes, scale direction,
bonus weight, lawyer stuns, gavel dodge timing, both 0-damage defeat and
2700-damage victory, and the awarded summon inventory.

## CEO plan (not implemented)

Sources: `DistributedBossbotBoss{,AI}.py`, `DistributedBanquetTable{,AI}.py`,
`DistributedGolfSpot{,AI}.py`, `DistributedBattleDiners{,AI}.py`, food-belt code,
and Bossbot Globals / DC definitions.

1. Bake phase_12 bossbotBoss, golfclub, BanquetInterior_1, inside elevator,
   separate tables, belts, pitchers, food and golf props. Pool tables and golf
   spots from the baked indexed layout. Stream unrelated banquet sections.
2. Follow Introduction → BattleOne → PrepareBattleTwo → BattleTwo (serving) →
   PrepareBattleThree → BattleThree (remaining diner Cog battle) →
   PrepareBattleFour → BattleFour (water/golf) → Victory → Reward/Epilogue.
   Serving duration comes from Globals, and the AI owns its timer/barrier.
3. Add generated BanquetTable, GolfSpot, BattleDiners and food-belt schemas.
   Serving sends requestGetFood(belt, slot, sequence) and requestServeFood
   (table, seat). Only toonGotFood/toonServeFood confirms inventory/consumption.
   Render per-seat diner info/status; use BattleDiners through the Battle API.
4. Tables grant/revoke control through state-owner messages. requestControl /
   requestFree, setPitcherPos, clearSmoothing and firingWater are clsend. Use
   the six signed i32/100 projectile parameters; waterHitBoss is a real swept
   impact, not a periodic damage request. Track reachedTable/hitTable/
   awayFromTable so moving/table-destruction attacks match the AI.
5. GolfSpot has separate ownership and position smoothing; setSwingInfo carries
   index, i16/10 angle and u8 power. A real ball impact calls ballHitBoss;
   speed damage is a separate setSpeedDamage(recovery,timestamp) gauge.
   Globals cap boss damage at 500 and speed damage at 90, recovery 20/minute.
   Do not reuse the VP's 100-point meter or hatch/stun logic.
6. Food/toonup requests carry belt/slot/sequence; await server confirmation.
   Handle water, golf, gear, movement and overtime attack codes separately.
   Victory's 30 s barrier precedes the AI's firing/reward inventory grant.

Acceptance: two-client table/golf ownership, rejected/stale food sequences,
remaining diners entering BattleDiners, moving/table attacks, golf slow/recovery,
water damage to 500, overtime transition and one server-owned firing reward.

## Verification and integration

Build with `DEVKITPRO=$HOME/devkitpro DEVKITARM=$HOME/devkitpro/devkitARM make -j4`.
Host commands: `sh tools/boss_test/run.sh`, isolated Python asset_check.py,
pack_reference.py / preview.py, and regressions.py. All test scratch stays in this
worktree. No protected level, toon-cache/avatar, battle FX/movie/camera, game, or
book implementation files were changed. main.cpp has one grouped BossFrame hook.
The host stream harness now models the documented FrameBegin fence before pump;
production toon-cache code is unchanged. The real Client wire test checks exact hitBoss(1), invalid-phase/membership
rejections, cooperative healing, barrier contexts, round poses and lobby reconnect.
See `docs/test-log.md` for actual results.

For the main agent: integrate the commits and copy the complete **boss/** output
tree to `sdmc:/3ds/toontown3ds/data/boss/`. Existing HQ zone bakes (11000/11100),
toon/clothing and Cog battle assets must also be installed. Start the existing
local server with AI_DEV=1 for a fresh test toon; this already relaxes boss boarding
and tops up laff/gags. No new ref edits or additional server patch is required.
Test a ready paid suit and a rental/fresh toon separately. Turn AI_DEV off to
verify normal merit/minLaff rejection. A fresh 60-laff toon is not guaranteed to
solo both VP Cog rounds; use an appropriately progressed group for a victory run.

Do these Azahar checks in **Old 3DS mode**, then record fps, heap, linear, VRAM,
frame CPU/GPU and command-buffer usage in test-log:

1. Enter 11100 through its real door, stand at the baked elevator locator, board,
   exit with B, reboard and ride; verify normal and forced dynamic-zone handoffs.
   A not-ready production toon stays in the lobby without an eject.
2. Verify the loaded elevator/arena, attached head/torso/legs/treads, rescued NPC
   species/clothes, all cage stops and each sliding ramp's visible/floor alignment.
   Check the condensed intro and that slow SD loading does not lose WaitForToons.
3. Fight both Cog rounds on both sides with one and multiple toons. Verify
   ReservesJoining does not stall, gag input/movies/rewards work and the second
   round is on the upper floor. Compare another client and verify ordinary street
   battles still work after returning.
4. Refill at the cage. Test low pies against closed/open hatches from both ends,
   stun, body pushes, recovery, final pie, real misses and high/low aim. Verify
   cooperative healing, jump-over stomp, swats, directed/strafe gears and body
   contact; HP must match the server. Check the approximate collision volumes.
5. Watch the held fall, reward owner updates, SOS card count, promotion when
   eligible, cage lowering/opening and A return. Verify zero-laff departure,
   disconnect/reconnect and a second encounter without stale projectiles/UI.
6. Remove one boss file: visible error/B return. Restore it and repeat. Stress
   eight toons, both Cog teams, gear sweep and pies; watch the real draw/memory
   peaks. Confirm arena/lobby and old/pending clips do not accumulate.

Autotest labels are `boss: board` (45 s), `boss: rounds` (600 s), `boss: pies`
(600 s), `boss: end` (30 s). Use a trigger containing `from boss: board`; every
label uses a `name*` one-second top/bottom shot series. Cog choices use the public
Battle autoplay. Pie test input repositions to the cage/firing lane and solves a
real ballistic shot; the ordinary collision and guarded clsend paths still decide
all results. It never writes damage or forces a phase. These fixed windows are
observational bounds, not assertions that an underpowered solo toon wins.


## CFO handoff and exact Azahar verification

Install this branch's 3DSX and the complete ignored
`sdmc-out/3ds/toontown3ds/data/boss/` output tree. Preserve existing HQ zones
12000/12100, Toons/clothes, Cog battle assets and these unchanged facility props:
`level/props/goon_pg_full.ttm`, `goon_pg_full_walk.tta`,
`goon_pg_full_collapse.tta`, `goon_pg_full_recovery.tta`, `goon_cone.ttm`.
No runtime edits or new server patch are required in the main checkout.

For a fresh solo development toon start the existing local server with
`AI_DEV=1 AI_DEV_STRONG=1 AI_DEV_WEAK_COGS=1 tools/server/start.sh`.
The existing import hook relaxes boss elevator boarding, gives 137 laff/all tracks/
6000 exp, and makes Cogs and revives 1 HP. It does not fake crane ownership,
helmet state, boss damage, phases or rewards. Without AI_DEV, use a toon with
Cashbot suit/access and completed Cogbucks. In this pinned server, the hood
constructs its CFO elevator with the default minimum laff **0**; checkBoard tests
current HP against that value and readyForPromotion(Cashbot). A configured custom
minimum must still be honored;
verify denied toons remain in the lobby without an eject.

Run these checks in **Old 3DS mode** and record fps, CPU frame/animation time,
heap/linear/VRAM and command buffer peaks in test-log:

1. Enter 12100 normally. A at the baked elevator boards, B exits; reboard and ride.
   Check a forced destination too. Slow SD loading must not time out WaitForToons.
2. Verify CFO head/torso/legs/treads attachment, elevator, both vaults, Resistance
   toon DNA, floor/wall collision and condensed intro/preparation. Confirm BattleOne
   is at (120,-230,0) with teams on X=145 and X=95; test each side, reserve joining,
   gag movies and normal street battles after returning.
3. Control each of four cranes. Test arm bounds, visible cables/magnet, B pickup/
   drop, X release and competing requests from two clients. The other client must
   see cable movement, held/drop poses and control release. Disconnect while holding
   a safe, reconnect to static lobby 12100, then reenter without stale children.
4. Jump-stun small/large goons, check walk/collapse/recovery/radar and head landing;
   test walking-goon pickup/two-second escape, safe strikes on goons, moving-target
   goon throws, treasure pickup/reject at full laff and scale/difficulty changes.
5. Test real misses and slow/fast throws. A safe can become the helmet; another
   sufficiently directed throw removes it. Goons stun/damage an unhelmeted CFO;
   safes damage it while dizzy. Boss-owned dropped helmets must never self-damage.
   Compare all 0–500 damage and local HP with the server/second client. Dodge attack
   11 gears and test electric contact. The physical collision/throw calibration is
   unverified until this pass; inspect shot series if a throw never connects.
6. Complete 500 damage: recovery/roll-out/train, Victory/Reward barriers, Resistance
   reward ID, A confirmation exactly once, second A return to 12000. Test zero-laff
   exit, missing/malformed boss/goon assets with visible B return and a second fight.
7. Stress eight toons and all sixteen goons. Check no command-buffer overflow or
   dropped critical models, 30 fps, geometry <=6 MiB/textures <=8 MiB, Toon cache
   <=4 MiB and no accumulation across clip switches, reward and lobby return.

For unattended observation write the existing autotest trigger with:
```
from cfo: board
only cfo:
```
Labels: **cfo: board** (45 s), **cfo: rounds** (600 s), **cfo: cranes** (600 s),
**cfo: end** (30 s), all with one-second top/bottom shot series
`cfo_board_*`, `cfo_rounds_*`, `cfo_cranes_*`, `cfo_end_*`.
Battle uses public autoplay. Crane observation requests real control and sweeps
ordinary controls/magnet; it never forces a phase/damage. Fixed windows do not
assert a solo victory, and manual aimed throws may be needed to finish calibration.

### CJ / CEO exact field additions and CFO lessons

Before implementing CJ, generate child schemas in addition to existing boss fields:

| Class | Required / receive fields | clsend fields |
|---|---|---|
| DistributedLawbotCannon | bossId:u32, index:u8, posHpr:6*i16/10; movie:(i8,u32,u8), updateCannonPosition:(u32,2*i16/10), setCannonWillFire:(u32,3*i16/10,i16) | requestEnter, requestLeave, setCannonPosition:(2*i16/10), setCannonLit:(2*i16/10), setLanded |
| DistributedLawbotChair | bossId:u32, index:u8; state:char, showCogJurorFlying(), toonJurorIndex:i8 | none |
| DistributedLawbotBossGavel | bossId:u32, index:u8; state:char | none |
| DistributedLawbotBossSuit | SuitBase requireds + posHpr:6*i16/10; doAttack:6*i16/10, doProsecute(), doStun() | hitByToon() |
| DistributedLawbotBoss | state:string, damage:(u16,u8,i16), lawyerIds:u32[], taunt:(i8,i8), toonGotHealed:u32, enteredBonusState(), difficulty:u8 | touchWitnessStand(), hitBoss:u8, healBoss:u8, hitToon:u32, hitDefensePan(), hitProsecutionPan(), hitChair:(u8,u8) |

CEO additions use these exact DC contracts:

| Class | Required / receive fields | clsend fields |
|---|---|---|
| DistributedBanquetTable | index:u8, numDiners:u8, bossId:u32, dinerInfo:3*u8[]; state:(char,u32,i8), dinerStatus:(u8,u8) | requestControl(), requestFree:i8, clearSmoothing:i8, setPitcherPos:(u8,u16%360/100,i16), firingWater:6*i32/100, waterHitBoss:u8 |
| DistributedGolfSpot | index:u8, bossId:u32; state:(char,u32,i8), goingToReward() | requestControl(), requestFree:i8, clearSmoothing:i8, setGolfSpotPos:(u8,u16%360/100,i16), setSwingInfo:(u8,i16/10,u8) |
| DistributedBattleDiners | inherited DistributedBattleFinal required fields and barriers | existing public Battle API |
| DistributedBossbotBoss | state:string, difficulty:u8, damage and speedDamage:(u16,u8,i16), toonGotFood:(u32,i8,i8,u32), toonServeFood:(u32,i8,i8), toonGotHealed:u32, toonGotToonup:(u32,i8,i8,u32) | requestGetFood:(i8,i8,u32), requestServeFood:(i8,i8), hitBoss:u8, hitToon:u32, ballHitBoss:u8, reachedTable:u8, hitTable:u8, awayFromTable:u8, requestGetToonup:(i8,i8,u32) |

Keep chair jurisdiction separate from cannon control, and table/golf owner/state
separate from food acknowledgements. Requesting control never grants it. Fixed
struct arrays have no u16 length prefix; variable arrays use a byte count. Test
full Panda-packed required/OTHER generation through the real Client, each truncated
prefix, molecule ordering, peer control changes and stale child removal. Maintain
per-boss wire impact scales; regenerate rather than copying CFO/VP field IDs.
Replay real timed trajectories and collisions in autotest, without synthetic hits.
Reserve active/pending animation memory together with reused props before loading.
Keep return/reconnect destinations static and reset every child/control on teardown.
