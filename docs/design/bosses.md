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

## Lawbot CJ implementation (host/build verified, 2026-10-24)

`CjBattle` is an independent encounter controller called from the start of the
existing `BossBattle::frame` hook. It does not change `BossBattle::board`, main.cpp,
level runtime, battle movies/camera, toon cache/avatar or game/book code.

The lobby is 13100, successful return is Lawbot HQ 13000 and sadness returns to
2000. The BPA1 anchor is read once when entering the lobby. The actual baked
anchor is (118.849998, 87.059502, 51.924500). A targeted CJ elevator destination is
authorized even if the AI cleared its seat in the same TCP turn. Disconnect
reconnects to static lobby 13100 instead of the abandoned dynamic arena.

The encounter registers during loading, acknowledges only actual matching barriers,
reuses public Battle for the Cog rounds/reserves and supports both battle sides.
The initial boss pose is (-2.798,-60,0,0); its later pose is
(-2.798,89,19.145,0). Introduction and travel cinematics are condensed. Jury
BattleTwo runs for the real 70-second interval; its barrier cannot be acknowledged
before 70.5 seconds. It does not advance from a scripted timer alone.

Independent child snapshots have per-kind limits 8 cannons, 12 chairs, 8 gavels and
10 lawyers. Required/OTHER parsing is transactional, including DNA with embedded
NULs. Ownership, encounter membership, phase, ammunition, flight and finite angle
checks guard all cannon requests. Each actual broadcast flight follows the stock
80-unit/second ballistic trajectory with gravity 32, is swept against the moving
chairs/static collision, reports one chair result, then sends setLanded once.
The next shot waits for the AI's next LOAD. The NPC flies; the player remains at
the cannon. Juror NPC DNA is stock 2001+index. Suit juror visuals use a shared
Big Wig placeholder and a straight 10-second arrival rather than the upstream
random suit/propeller sequence. Chair pitch animation and full seated toon clips
are simplified; the indexed, moving chair positions and server seat outcomes are
preserved.

Scale damage is AI-owned (0..2700, initial 1350). CJS1 stores the actual beam and
pan pivots; the beam tilts -40..40 degrees and pans counter-rotate. Evidence uses
the existing physical pie release/charge/trajectory and swept impacts. A real
defense hit sends **hitBoss(1)**: the AI applies juror/bonus weight. Stock AI has
no hitDefensePan/hitProsecutionPan methods despite their DC declarations. A local
prosecution hit is cosmetic; clients do not send those fields or healBoss.
Witness contact requests ammunition, other-toon impacts use hitToon(other), and
lawyer impacts send hitByToon on the actual lawyer object. Lawyers use their
received DNA/pose and 5-second stun. Their attack lawbooks travel toward the
received target over the stock 3 seconds, after the 3.15-second release; AI owns
prosecution healing and the 20-second all-lawyer stun bonus. Gavel pose tables
match upstream timings/headings. Swept actor/pan boxes and gavel head/handle
volumes are conservative approximations requiring emulator review. Bench attack
4 only hits grounded toons during its release window; zap codes 13/14/15 distinguish
gavel head/handle/lawyer hits. Defeat, Frolic and victory/reward are distinct;
owner Cog-summon inventory (32 masks; single/building/invasion bits 1/2/4) and
promotion updates drive the reward display. No client grant or forced victory.

Controls: A boards the lobby elevator, enters a nearby free cannon, or jumps.
B exits the lobby seat; in a cannon B fires and X leaves, Circle Pad aims; in
court B holds/releases evidence and D-pad adjusts pitch. Walk to the witness at
(54,100,0) for evidence. A in Epilogue/Frolic returns to the static destination.

### CJ assets and memory

Reproduce with `.venv/bin/python -I -B tools/pipeline/bake_boss.py --cj`.
Copy every `cj_*` file from `sdmc-out/3ds/toontown3ds/data/boss/` to SD
`3ds/toontown3ds/data/boss/`. The normal Lawbot HQ/lobby, toon/clothing and Cog
asset sets are also required. No server patch is needed beyond the existing
local AI_DEV hooks. `docs/design/cj_assets.json` is the complete reproducible
manifest; generated game assets stay ignored and outside the MIT license.

16 TTM models: four boss parts, arena, jury box, scale stand/beam/two pans,
cannon base/barrel, chair, gavel, tart and lawbook. 96 TTA files cover 24 clips ×
four parts at 12 fps. Other files: TTC collision, BPA1 lobby anchor, CJS1 v1/count4
barrel/beam/pan vectors, BND1 nine NPC DNA records. Existing format versions are
unchanged. ETC1/ETC1A4 and small single-channel textures use maximum 128 dimensions.

| CJ pool | Bytes | Limit |
|---|---:|---:|
| Model geometry | 192,110 | Included below |
| Largest four-part clip | 1,609,920 | 1,700,000 runtime cap |
| Geometry + actual active/pending clip pair | 3,411,950 | 6,291,456 |
| Geometry + reserved clip caps | 3,592,110 | 6,291,456 |
| Textures | 785,664 | 8,388,608 |
| Collision regular heap | 48,440 | Separate from linear pools |
| All CJ files on disk (116) | 16,528,929 | Streamed from SD |
| Build text + data + BSS | 2,066,100 | 4,194,304 |

The manifest has 6,773 unique triangles / 64 batches, **not a worst-frame count**:
chairs, cannons, gavels, NPCs and lawyers instance geometry. Existing renderer
frustum/command guards remain active. Measure the simultaneous rendered peaks
in Old 3DS mode; no 30 fps or hardware-budget claim is made. One active and one
pending four-part clip set are admitted together and published atomically after
the GPU fence. Models load serially. Missing/corrupt/over-budget assets show B
return. The arena and lobby are never resident together; shared toon/Cog caches
retain their existing bounded budgets.

### CJ verification and main-agent handoff

Host tests: `sh tools/boss_test/run.sh` (ASan/UBSan, 177 independent Panda vectors,
all truncated field and child-entry prefixes, ownership/payload guards, reload,
barriers, jury/ballistics/gavels/scale logic, reward masks and real baked floor
queries), `PORT=7498 bash tools/nettest/run_tests.sh`, and
`.venv/bin/python -I -B tools/boss_test/regressions.py` (33 suites). The obsolete standalone factory runner now tests current Level/LevelSpec
contracts and real upstream TLV data without changing protected facility code. Asset checks pass for all three bosses.
Two independent final CJ bakes match every byte; VP and CFO rebakes exactly match
their committed manifests (115 files each). Decoded TTM/TTA texture/attachment
previews are in `sdmc-out/bosses4/`. Build succeeds with zero source warnings;
the inherited devkitARM crtn.o GNU-stack linker warning remains.

No Azahar or real server encounter was run. The main agent must do these checks:

1. Install the complete CJ set and existing 13000/13100 + toon/Cog assets. Start
   `AI_DEV=1 AI_DEV_STRONG=1 AI_DEV_WEAK_COGS=1 tools/server/start.sh`. Use both a
   fresh development toon and a production-ready group; with AI_DEV disabled,
   verify normal merit/laff rejection. Enter 13100 through its actual door.
   At the BPA1 anchor, board, B exit, reboard, then ride. Confirm both normal and
   forced target messages, seat clearing and no interest/eject loop. Verify the
   cached anchor removes SD reads from subsequent lobby frames.
2. Watch entry, intro, both Cog sides and reserves. Verify collision at entry
   and the correct upper-bench placement in a server configuration that uses
   the optional second Cog round. Test slow SD loading and missing TTM/TTA/CJS1.
3. With two clients compete for a cannon: only its owner aims/fires; launch the
   correct NPC, retain the toon at the cannon, hit front/back/edge seats, miss,
   reload, exhaust all 12 shots and exit. Compare chair occupancy with stock
   client, moving box alignment and the 70-second transition. Validate the
   simplified juror/chair visuals and conservative chair impact boxes.
4. In court inspect scale alignment and direction at 0/1350/2700, refill at the
   witness, throw low/high/charged evidence, deliberately miss, hit both pans,
   heal another toon and stun all lawyers. Confirm weight/20-second bonus and
   prosecution healing against the stock client. Dodge every gavel heading and
   bench wave; verify jumps, zap codes and HP agree with AI. Review conservative
   gavel/pan/actor volumes and lawbook timing at low frame rates.
5. Earn a real victory: verify fall, Reward/Epilogue, summon mask increment,
   promotion and A return to 13000. Separately test zero-scale Defeat/Frolic,
   zero-laff sadness, disconnect/reconnect to 13100, and a second encounter
   without retained clips, child actors or battle UI. Recheck VP/CFO boarding
   against the main agent's independent board fix; this branch leaves it intact.
6. In Old 3DS mode stress eight toons plus all children; record fps, heap,
   linear, VRAM, frame time, draws/triangles and command usage in test-log. Repeat
   on hardware before asserting the 30 fps target. Check both screens, missing
   file B return and no memory growth across repeated fights.

Autotest trigger text `from cj: board` runs `cj: board` (45 s), `cj: rounds`
(600 s), `cj: court` (1800 s), `cj: end` (30 s). All use one-second top/bottom
shot series `cj_board*`, `cj_rounds*`, `cj_court*`, `cj_end*`. Public Battle autoplay
handles Cogs; jury autoplay solves and fires real shots; court autoplay refills
and solves real defense shots. Ordinary collision and guarded senders determine
results. Script duration never forces boss phases or victory. Run a manual
production encounter as well; the script is a diagnostic, not victory proof.


## CEO boss: architecture, assets and verification

Sources: `DistributedBossbotBoss{,AI}.py`, `DistributedBanquetTable{,AI}.py`,
`DistributedGolfSpot{,AI}.py`, `DistributedBattleDiners{,AI}.py`, `DistributedFoodBelt{,AI}.py`,
and Bossbot Globals / DC definitions.

### Architecture and gameplay phases

1. **Boarding and elevator entry.**
   Toons board the Bossbot HQ CEO elevator (`DistributedBBElevator`) from lobby zone 10100.
   On elevator ride completion, toons enter the banquet room (dynamic arena zone). The
   encounter registers during zone loading and acknowledges matching state barriers.
   Lobby disconnect/reconnect safely routes back to static zone 10100.

2. **Phase flow and state progression.**
   - **Introduction:** Condensed cinematic intro, CEO speech (`Ff_speech` -> `Ff_neutral`),
     camera transitions, and arena initialization.
   - **BattleOne (Waiters):** Standard Cog battle round against waiter Cogs using the public
     Battle API (`DistributedBattleWaiters`). Toons defeat waiter Cogs to advance.
   - **PrepareBattleTwo & BattleTwo (Banquet serving):** 2 food belts (`DistributedFoodBelt`)
     and 4 banquet tables (`DistributedBanquetTable`). Belts carry oil cans and toonup food.
     Toons approach belts and send `requestGetFood(belt, slot, seq)` or `requestGetToonup`.
     Toons carry food to tables and send `requestServeFood(table, seat)`. Only server
     broadcast confirmations (`toonGotFood`, `toonServeFood`, `toonGotToonup`) alter toon
     inventory/laff. Table diners transition between hunger states (`kDinerStatusInactive`
     to `kDinerStatusHidden`); full diners leave happily while unserved diners get angry.
     The server owns the 300 s serving timer/barrier.
   - **PrepareBattleThree & BattleThree (Diner Cog battle):** Unsated diners jump down to
     battle toons (`DistributedBattleDiners`), again handled through the public Battle API.
   - **PrepareBattleFour & BattleFour (Table water & golf spots):**
     - **Banquet tables / water pitchers:** Toons approach an unflipped table and press A to
       send `requestControl()`. State receipt with matching owner grants pitcher control.
       Circle Pad aims heading/pitch; B fires water streams (`firingWater` with 6 packed
       `i32/100` coordinates). A swept 3D segment detects boss impacts and sends `waterHitBoss`
       followed by `hitBoss(1)`. CEO targets tables to crush them (`reachedTable`, `hitTable`,
       `awayFromTable`), flipping tables and forcing toon release (`requestFree`).
     - **Golf spots:** Toons approach a free golf tee (`DistributedGolfSpot`) and press A to
       `requestControl()`. Circle Pad aims heading/pitch; holding B charges swing power.
       Releasing B sends `setSwingInfo(index, angle, power)`. Swept golf ball impacts send
       `ballHitBoss(power)`, applying speed damage.
   - **CEO attacks and boss stats:**
     - Boss damage gauge is capped at 500 (`kCeoMaxDamage`).
     - Speed damage gauge is capped at 90 (`kCeoMaxSpeedDamage`), recovering at 20/minute
       (`kCeoSpeedRecoverRate`). Boss speed and tread roll rate vary dynamically with speed
       damage and health.
     - Attacks include directed gear throws (`kAttackCodeGears`), gear showers (`kAttackCodeSweep`),
       stomp jump shockwaves (`kAttackCodeStomp`), table-charge crushes (`kAttackCodeMoveTable`),
       and overtime fire bursts (`kAttackCodeFire`).
   - **Victory & Epilogue:**
     - 500 damage triggers the victory fall animation (`Fb_fall`).
     - 30-second victory barrier precedes server reward grants.
     - Reward/Epilogue: CEO grants Pink Slips (`setPinkSlips`) and suit promotion.
     - Pressing A on the exit elevator returns to Bossbot HQ lobby (zone 10000/10100).

### CEO assets and memory budget

Reproduce with `.venv/bin/python -I -B tools/pipeline/bake_boss.py --ceo`.
Assets reside in `sdmc-out/3ds/toontown3ds/data/boss/ceo_*`. Manifest contains 111 files,
including 96 animation clips (24 clips x 4 parts at 12 fps) and 9 static models/props.

| CEO pool | Measured | Budget / Limit |
|---|---:|---:|
| Model geometry (all parts & props) | 324,876 B | 6,291,456 B |
| Largest four-part clip | 1,505,664 B | 1,700,000 B runtime cap |
| Geometry + active/pending clip pair | 3,336,204 B | 6,291,456 B linear cap |
| Textures (ETC1 / ETC1A4 / 8bpp) | 619,392 B | 8,388,608 B linear cap |
| Collision heap (`ceo_arena.ttc`) | 55,248 B | Regular heap |
| All CEO files on SD (111 files) | 15,543,141 B | Streamed from SD |
| Triangles / batches | 9,930 tris / 59 batches | <= 15,000 tris / <= 150 draws |

### CEO DC field contracts

Generated via `tools/dcgen/dcgen.py` into `source/net/dc_generated.h`:

| Class | Required / receive fields | clsend fields |
|---|---|---|
| DistributedFoodBelt | bossCogId:u32, index:u8; state:char | none |
| DistributedBanquetTable | index:u8, numDiners:u8, bossCogId:u32, dinerInfo:3*u8[]; state:(char,u32,i8), dinerStatus:(u8,u8) | requestControl(), requestFree:i8, setPitcherPos:(u8,u16%360/100,i16), clearSmoothing:i8, firingWater:6*i32/100, waterHitBoss:u8 |
| DistributedGolfSpot | index:u8, bossCogId:u32; state:(char,u32,i8), goingToReward() | requestControl(), requestFree:i8, setGolfSpotPos:(u8,u16%360/100,i16), clearSmoothing:i8, setSwingInfo:(u8,i16/10,u8) |
| DistributedBattleWaiters / DistributedBattleDiners | inherited DistributedBattleFinal required fields | existing public Battle API |
| DistributedBossbotBoss | state:string, battleDifficulty:u8, damage & speedDamage:(u16,u8,i16), toonGotFood:(u32,i8,i8,u32), toonServeFood:(u32,i8,i8), toonGotHealed:u32, toonGotToonup:(u32,i8,i8,u32) | requestGetFood:(i8,i8,u32), requestServeFood:(i8,i8), hitBoss:u8, hitToon:u32, ballHitBoss:u8, reachedTable:u8, hitTable:u8, awayFromTable:u8, requestGetToonup:(i8,i8,u32) |

### Verification and main-agent handoff

Host tests in `tools/boss_test/run.sh`:
- 221 Panda vectors for VP, CFO, CJ, and CEO field serialization and truncation guards.
- CEO kinematics, roll speed scaling, barrier delay, clip lookup, and BossState bounds.
- Real `Client` test suite (`ceo_client.inc`): food belts, serving food/toonup, table pitcher
  control/aim/water firing, golf spot control/aim/swing, attacks, pink slips, and reconnect.
- Floor collision validation (`ceo_collision.cpp`): 11 critical gameplay locations on
  `ceo_arena.ttc` aligned with zero height error.
- Regression suite: all 33 test suites pass (`tools/boss_test/regressions.py`).
- DevkitARM build: 0 compiler warnings in `source/`.

Azahar verification steps (in Old 3DS mode):
1. **ceo: board** (45 s): Board CEO elevator in Bossbot HQ lobby (10100), verify countdown,
   door close, transition to arena, and initial boss intro dialogue/pose.
2. **ceo: banquet** (600 s): Food belts and table serving. Collect food from moving belt slots,
   serve hungry diners, verify diner satisfaction animations, and monitor waiter battle round.
3. **ceo: rounds** (600 s): Battle round with remaining diner Cogs using Battle autoplay.
4. **ceo: golf** (600 s): Table water pitchers and golf spots. Aim pitcher and shoot water streams
   at CEO; aim golf spots and charge swing power to slow down CEO; dodge CEO gear/stomp attacks.
5. **ceo: end** (30 s): CEO defeat/fall animation, victory barrier, pink slip reward sequence,
   and elevator exit back to Bossbot HQ.

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
