# Combat (street cog battles): protocol and client spec

Status: design, Phase 4+. Upstream is the specification. Paths are relative to
`ref/open-toontown/`, and line numbers are as of the 2026-10-23 clone. Assets are under
`ref/resources/`. Read `zones.md` first: battles happen on street visgroup zones (2101
and so on), so street loading and interest are prerequisites.

## 1. DC surface (etc/toon.dc)

All IDs come from `tools/dcgen/dcgen.py`. To use these classes:

- Add them to `WANTED` and `TABLE_CLASSES` (dcgen.py:37, :58).
- Add every outgoing field to the send whitelist. Sending anything else gets the client
  disconnected.

| Class (line) | Field | Args | Keywords | Dir |
|---|---|---|---|---|
| DistributedSuitPlanner : DO (716) | setZoneId | uint32 | required broadcast ram | S→C |
| DistributedSuitBase : DO (724) | denyBattle | () | (to one avatar) | S→C |
| | setDNAString | blob | required broadcast ram | S→C |
| | setLevelDist | int16 | required broadcast ram | S→C |
| | setBrushOff | int16 | broadcast | S→C |
| | setSkelecog / setSkeleRevives | uint8 / uint8 | required broadcast ram | S→C |
| | setHP | int16 | required broadcast ram | S→C |
| DistributedSuit (734) | **requestBattle** | int16/10 ×6 (x,y,z,h,p,r) | airecv **clsend** | C→S |
| | setSPDoId | uint32 | required broadcast ram | S→C |
| | setPathEndpoints | uint16 start, uint16 end, uint16 minLen, uint16 maxLen | required broadcast ram | S→C |
| | setPathPosition | uint16 legIndex, int16 timestamp | required broadcast ram | S→C |
| | setPathState | int8 | required broadcast ram | S→C |
| DistributedBattleBase : DO (880) | setLevelDoId, setBattleCellId | uint32 | required broadcast ram | S→C |
| | setInteractivePropTrackBonus | int8 | required broadcast ram | S→C |
| | setPosition / setInitialSuitPos | int16/10 ×3 | required broadcast ram | S→C |
| | setZoneId | uint32 | required broadcast ram | S→C |
| | setMembers | uint32[], string×5, uint32[], string×4, int16 | required broadcast ram | S→C |
| | adjust | int16 ts | broadcast | S→C |
| | setMovie | see §5 | required broadcast ram | S→C |
| | setChosenToonAttacks | uint32[] ids, int16[] tracks, int16[] levels, int32[] targets | broadcast ram | S→C |
| | setBattleExperience | see §7 | required broadcast ram | S→C |
| | denyLocalToonJoin | () | | S→C |
| | setBossBattle | uint8 | required broadcast ram | S→C |
| | setState | string, int16 ts | required broadcast ram | S→C |
| | **faceOffDone, toonRequestRun, toonDied, adjustDone, timeout, movieDone, rewardDone** | () | airecv clsend | C→S |
| | **toonRequestJoin** | int16/10 ×3 | airecv clsend | C→S |
| | **joinDone** | uint32 avId | airecv clsend | C→S |
| | **requestAttack** | int8 track, int8 level, int32 av | airecv clsend | C→S |
| DistributedBattle (908) | (no own fields) | | | |

The classes that matter on `DistributedToon` (443) for combat:

- Health, broadcast and ownrecv:
  - `setMaxHp(int16)` (450) and `setHp(int16)` (451).
  - `toonUp(uint16)` (452) and `takeDamage(uint16)` (453).
- `setBattleId(uint32)`, required broadcast ram (454). It is nonzero while the toon is in
  a battle.
- Owner-only (ownrecv), required, db:
  - `setExperience(blob)` (455)
  - `setMaxCarry(uint8 = 20)` (456)
  - `setInventory(blob)` (460)
- `setTrackAccess(uint16[] = [0,0,0,0,1,1,0])`, required broadcast ownrecv (457).
- `setEarnedExperience(uint16[])`, ownrecv (484).

Wire notes (Astron):

- `blob`, `string` and every `T[]` have a **u16 byte-length** prefix.
- `int16/10` is sent as the value ×10, rounded.
- Timestamps are 16-bit network time (`globalClockDelta`), as in `setSmPosHpr`.

Required-field order in ENTER_OBJECT follows declaration order (inherited fields first):

- **Suit:** setDNAString, setLevelDist, setSkelecog, setSkeleRevives, setHP, setSPDoId,
  setPathEndpoints, setPathPosition, setPathState.
- **Battle:** setLevelDoId, setBattleCellId, setInteractivePropTrackBonus, setPosition,
  setZoneId, setInitialSuitPos, setMembers, setMovie, setBattleExperience, setBossBattle,
  setState.

## 2. Data formats

**Inventory blob** (`toon/InventoryBase.py:48-56`, read side :58-73):

- 7 tracks × 7 levels = **49 bytes**, one `uint8` count each.
- Order is track-major: byte index = `track*7 + level`.
- A short blob is zero-filled when read (:65-68).
- The DC default is 1× level-0 throw (cupcake, byte 28) and 1× level-0 squirt (squirting
  flower, byte 35); toon.dc:460.
- The server re-sends the whole blob after each round in which a gag was used
  (`DistributedBattleBaseAI.py` ~1355/1361, `toon.d_setInventory(...)`). It also re-sends it
  after rewards (`BattleExperienceAI.py:150`).
- The client never edits inventory locally.

**Experience blob** (`toon/Experience.py:25-39`):

- 7 × `uint16` = 14 bytes, one per track.
- Gag unlock thresholds are `Levels[track][level]`
  (`toonbase/ToontownBattleGlobals.py:44`). For throw and squirt they are 0/10/50/400/2000/6000/10000.
- A level is usable when exp ≥ threshold **and** `trackAccess[track]` is 1.
- `MaxSkill` = 10500 (:95).

**Track and level IDs** (`ToontownBattleGlobals.py:23-36`):

| Track | ID |
|---|---|
| HEAL | 0 |
| TRAP | 1 |
| LURE | 2 |
| SOUND | 3 |
| THROW | 4 |
| SQUIRT | 5 |
| DROP | 6 |

- Levels run 0..6. Unpaid accounts are limited to level 4 or lower
  (`MAX_UNPAID_LEVEL_INDEX`, :37).
- Prop names are in `AvProps` (:480). Throw is
  tart/fruitpieslice/creampieslice/fruitpie/creampie/cake/cake. Squirt is
  flower/waterglass/waterballoon/bottle/firehose/stormcloud/stormcloud.
- Accuracy is in `AvPropAccuracy` (:581): throw 75, squirt 95.
- Damage ranges are in `AvPropDamage` (:638): throw L0 4–6, squirt L0 3–4.
- The group/single target table is `AvPropTargetCat` (:689). Throw L0–4 and squirt L0–4
  are single target.

**Special "track" values** (`battle/BattleBase.py:25-35`):

| Name | Value |
|---|---|
| NO_ATTACK | −1 |
| UN_ATTACK | −2 |
| PASS_ATTACK | −3 |
| PASS | 98 |
| SOS | 99 |
| NPCSOS | 97 |
| PETSOS | 96 |
| FIRE | 100 |

**SuitDNA blob** (`suit/SuitDNA.py:177-201`):

- The first byte is the type. `'s'` is followed by a 3-byte name (NUL padded, e.g.
  `f\0\0`) and then a 1-byte dept (`c`/`l`/`m`/`s`). `'b'` is followed by a dept byte only.
- Head codes are listed in `suitHeadTypes` (:10). Bodies: A/B/C lists are at :42/:56/:65.
- **Max HP** = `SuitAttributes[name]['hp'][levelDist]` (`suit/SuitBase.py:53-55`).
- **Displayed level** = `SuitAttributes[name]['level'] + levelDist + 1` (SuitBase.py:63-66,
  `battle/SuitBattleGlobals.py:24`).
- Example: the Flunky `'f'` has hp (6,12,20,30,42) (SuitBattleGlobals.py:113-137).

## 3. Suits on streets

- **Planner.** One `DistributedSuitPlanner` per street lives in the *branch* zone (for
  example 2100; `hood/HoodDataAI.py:115-123`).
  - The client gets it because the Astron street interest set always includes
    `ZoneUtil.getBranchZone(zone)` (`town/Street.py:356-360`).
  - From `setZoneId` the client loads the street DNA,
    `phase_5/dna/toontown_central_2100.dna` (`suit/SuitPlannerBase.py:490-516`).
  - From that DNA it uses `store_suit_point [idx, TYPE, x y z(, door)]`, the
    `suit_edge [a b]` lines inside each `visgroup` (the edge's zone is its visgroup), and
    `battle_cell [w h x y z]`.
  - Point types: STREET_POINT, FRONT_DOOR_POINT, SIDE_DOOR_POINT, COGHQ_IN/OUT. TTC 2100
    has 274 points, 511 edges and one battle cell per visgroup.
  - **The baker must export this graph** (points, edges with zone, battle cells) into the
    street `.tz`.
- **Suit objects** live in visgroup zones. The AI moves a suit with `sendSetZone` as it
  crosses legs (`suit/DistributedSuitAI.py:213-256`). The client sees suits through its
  vis-zone interest.
- **Path reconstruction.** No position is ever broadcast, except the debug-only
  `debugSuitPosition`. The client must recompute the path:
  1. `setPathEndpoints(start, end, minLen, maxLen)` maps point indices through
     `sp.pointIndexes`. The client then calls
     `dnaStore.getSuitPath(start, end, minLen, maxLen)` and builds a
     `SuitLegList(path, dnaStore, SuitWalkSpeed = 4.8, fromSky = 6.5, toSky = 6.5,
     fromSuitBuilding = 2.0, toSuitBuilding = 2.5, toToonBuilding = 2.5)`. References:
     `suit/DistributedSuit.py:209-229`, `suit/SuitBase.py:83-84`, `suit/SuitTimings.py`,
     `toonbase/ToontownGlobals.py:575`.
  2. `setPathPosition(legIndex, ts)` sets
     `pathStartTime = localTime(ts) − legList.getStartTime(legIndex)` (:239-248).
  3. `setPathState` takes these values (:299-320):

     | Value | Meaning |
     |---|---|
     | 0 | stop |
     | 1 | walk the path from the current time |
     | 2 | FlyAway |
     | 3 | in battle (no-op) |
     | 4 | DanceThenFlyAway (victory dance 9.08 s, then toSky) |

  4. Leg types map to the FSM states FromSky, WalkToStreet, WalkFromStreet, Walk, ToSky,
     FromSuitBuilding, ToSuitBuilding, ToToonBuilding, FromCogHQ and ToCogHQ (:420-576).
     Each leg is a linear lerp from A to B, and the heading comes from `calculateHeading`.
  - **Risk (resolved, see "Client implementation status").** `getSuitPath` and
    `SuitLegList` live in C++ `libtoontown` (imported via
    `from panda3d.toontown import *`, `SuitBase.py:2`). Neither is in `ref/`, and the AI
    uses the same code, so a port must reproduce it bit-for-bit (graph search order and leg
    timings).
  - **Fallback that keeps battles working.** Place each suit at the battle cell or nearest
    suit point of its current zone, and wander along that zone's edges at 4.8 u/s. The AI
    takes the battle's suit start position from the `requestBattle` args
    (`DistributedSuitAI.py:81-82`, `DistributedBattleAI.py:18`). The battle itself sits
    on the zone's DNA battle cell (`DistributedSuitPlannerAI.py:1022`), so approximate
    suit positions are tolerated.
- **Battle trigger.**
  - The client detects contact between the local toon and the suit's body tube:
    `CollisionTube(0,0,0.5, 0,0,height−1, r=1)` (`otp/avatar/Avatar.py:541`,
    `OTPGlobals.py:67`). Flunky height is 4.88 (`suit/Suit.py:410`).
  - Detection is enabled only in path legs (`DistributedSuit.py:420-510`).
  - On contact, the client goes to place state WaitForBattle and plays neutral. It sends
    `requestBattle(suitPos, suitHpr)` (:400-415) and stops walking.
  - The AI denies the request if the suit is not in pathState 1 or not in a Walk leg, or
    if the zone has no battle cell. A denial sends `denyBattle` to the toon plus a
    broadcast `setBrushOff(idx)` (`DistributedSuitAI.py:60-90`). On `denyBattle` the
    client returns to walk (`DistributedSuitBase.py:290-295`).

## 4. Battle lifecycle

- **Creation.** On acceptance the AI does the following
  (`DistributedSuitPlannerAI.py:1008-1031`, `battle/DistributedBattleAI.py:14-36`):
  - Generates a `DistributedBattle` in the suit's zone at the zone's battle cell.
  - Adds the toon and calls `toon.b_setBattleId(battleDoId)`.
  - Puts the suit in pathState 3 through `releaseControl` (:57).
  - Enters **FaceOff**.
- **Placement.** The battle node is at `setPosition` and turns to face
  `setInitialSuitPos` (`DistributedBattleBase.py:353-360`). Slot offsets are in
  `BattleBase.py:172-193`:
  - Suits are at `suitPoints[n−1][i]`, y≈+5.
  - Toons are at `toonPoints[n−1][i]`, y≈−6.
  - The pending (joining) spots are `suit/toonPendingPoints`.
- **`setMembers` decoding** (`DistributedBattleBaseAI.py:241-293`,
  `DistributedBattleBase.py:375-576`):
  - `suits` and `toons` are doId lists.
  - Each of the string fields is a run of ASCII digits, and each digit indexes into
    suits/toons. In order the strings are: suitsJoining, suitsPending, suitsActive,
    suitsLured, suitTraps, toonsJoining, toonsPending, toonsActive, toonsRunning.
  - `suitTraps` is one char per suit: a trap level, or `'9'` for none.
  - When the local toon first appears in `toons`, a street client calls
    `place.enterZone(battle.zoneId)`, which sends setLocation and sets interest
    (`DistributedBattleBase.py:560-563`). It also enters `HasLocalToon`, which puts the
    place in battle state, plays battle music and sets `setAnimState('off')`
    (`BattlePlace.py:37-43`).
  - Upstream broadcasts position only in the Walk state (`hood/Place.py:755,774`). While
    `battleId ≠ 0` our client must **stop `setSmPosHpr`**. Toon positions are then derived
    locally on every client from the battle slots.
- **FSM.** Client states are defined at `DistributedBattleBase.py:86-102`, AI timers at
  `DistributedBattleBaseAI.py:1226-1300`. The AI drives each state with
  `setState(name, ts)`.

| State | Client must | Client sends | AI timeout |
|---|---|---|---|
| FaceOff | Suit taunts for 3.5 s. Suit walks and toon runs to slot 0 over `dist/4.8` s (`BattleBase.py:252-257`, `DistributedBattle.py:73-152`) | `faceOffDone` (only toons[0]) | `faceoffTime + 3.5 + 2.0` (`DistributedBattleAI.py:54-63`) |
| WaitForInput | Show the gag menu and a 20 s countdown (`CLIENT_INPUT_TIMEOUT`, `TTLocalizerEnglishProperty.py:9`) | `requestAttack` / `toonRequestRun` / `timeout` | 22 s (`BattleBase.py:162`) → movie with what was chosen |
| MakeMovie | nothing (0.8 s delay when there are >1 toons) | — | — |
| PlayMovie | Play `setMovie` (§5) | `movieDone` | `12·(toons+NPC) + 12·suits + 2` s; 5 s after the first responder (:1276-1289, :938-957) |
| WaitForJoin | transient | — | — |
| Reward | Play the victory/reward panel (§7) | `rewardDone` | 120 s (`REWARD_TIMEOUT`); 5 s after the first responder |
| Resume | Leave the battle and go back to walk | — | — |

- The battle object is deleted after Resume (`DistributedBattleAI.py:125-133`).
- `battleId` goes back to 0 (`DistributedBattleBaseAI.py:1660-1664`).
- Surviving suits `resume()` their path.
- **Adjust.** A parallel FSM (`DistributedBattleBaseAI.py:586-609, 1741-1809`) handles
  repositioning after joins or deaths:
  - The AI broadcasts `adjust(ts)`.
  - Clients lerp members to the new slots and send `adjustDone`.
  - The AI also times this out (`__estimateAdjustTime`).
- **Joining an existing battle.**
  - Battle lockout tube radius is 9 (`DistributedBattleBase.py:1531-1537`). On entering
    it the client sends `toonRequestJoin(posRelBattle)` (:1549-1567).
  - The AI adds the toon to `joiningToons`, or sends `denyLocalToonJoin`.
  - Every client in the battle plays the join walk. When it finishes, the client sends
    `joinDone(joinerAvId)` (:930-933).
  - The server times this out after `MAX_JOIN_T + 2` = 22 s (`DistributedBattleBaseAI.py:534-540`).
- **Running away.** `toonRequestRun` is accepted only while Runable (WaitForInput). The
  run takes `TOON_RUN_T` = 3.3 s.

## 5. Gag selection and setMovie

- **requestAttack(track, level, av)**, client side at `DistributedBattleBase.py:1140-1241`.
  The `av` argument is a **doId**, not an index:
  - **Single-target attack:** `av` = `activeSuits[target].doId`.
  - **Group attack:** `av` = −1.
  - **Heal:** the doId of the target toon.
  - **PASS:** `(98, −1, −1)`.
  - **UnAttack (change mind):** `(−2, −1, −1)`.
  - **SOS / NPCSOS:** `(99|97, −1, friendId)`.
  - **FIRE:** `(100, −1, suitDoId)`.
- **AI validation** (`DistributedBattleBaseAI.py:1004-1093`):
  - The state must be WaitForInput and the toon must be active.
  - The track must be 0..6 and the level 0..6.
  - The toon must have `numItem(track, level) > 0`. Otherwise the request is silently
    turned into NO_ATTACK.
  - Paid gags from a non-paid account are dropped.
  - If any request fails these checks, the AI sends `setChosenToonAttacks`. Once every
    active toon has responded, the AI makes the movie.
- **`setMovie` argument list** (toon.dc:889; packed at `DistributedBattleBaseAI.py:313-378`):
  1. `int8 movieHasBeenMade`
  2. `uint32[] activeToons`
  3. `uint32[] activeSuits`
  4. **4 toon attacks**, each laid out as:
     `int8 toonIdx, int8 track, int8 level, int32 target (suit index or toon index or −1),
     int16[] hp (per suit), int16 accBonus, int16 hpBonus, int16[] kbBonus,
     int8 diedBits, int8 reviveBits`
  5. **4 suit attacks**, each laid out as:
     `int8 suitIdx, int8 attack, int8 targetToonIdx, int16[] hp (per toon),
     int8 toonDiedBits, int8 beforeToons, int8 tauntIdx`
  - Unused entries have id −1 or track −1 (`BattleBase.py:75-121`).
  - `attack` indexes into **that suit's** `SuitAttributes[name]['attacks']` list. It is
    not a global ID (`Movie.py:812-819`, `SuitBattleGlobals.py:94-110`). The animation
    name is `SuitAttacks[atkName][0]`, for example the Flunky's PoundKey → `'phone'`,
    Shred → `'shredder'`, ClipOnTie → `'throw-paper'` (:2889+).
- **Play order** (`Movie.py:406-466`):
  - Toon attacks first, in the order FIRE, SOS, NPCSOS, PETSOS, HEAL, TRAP, LURE, SOUND,
    THROW, SQUIRT, DROP.
  - Within a track, attacks are sorted by level (`BattleBase.py:122-157`).
  - Suit attacks come after the toon attacks (`Movie.py:238-245`).
- **HP is server authoritative.** After the round the AI applies HP with `quietly=1` and
  `setHp` broadcasts the new total (`DistributedBattleBaseAI.py:1612-1631`,
  `DistributedToonAI.py:1081-1093`).
  - The client shows the damage numbers from the movie and takes the laff value from
    `setHp`.
  - HP ≤ 0 means the toon went sad. The AI zeroes the inventory and removes the toon. The
    client plays `lose` and teleports to the playground (`DistributedToon.py:682-696`).
  - Dead suits drop out of `setMembers`, and their DOs are deleted.
- **Skipping is legal.** `movieDone` is accepted at any time during PlayMovie, and
  upstream has a `SkipMovie` flag (`ToontownBattleGlobals.py:12`,
  `DistributedBattleBase.py:1258-1268`).
  - A minimal client may play a short summary and send `movieDone` early.
  - The server then waits only 5 s for the other toons.

## 6. Movie rendering: essential vs skippable

The minimum set (throw L0, squirt L0, Flunky/suitC, generic suit attack) uses these
assets. All were verified with `find`.

**Toon animations** (map in `toon/Toon.py:50-139`; files are
`phase_3.5/models/char/tt_a_chr_dgm_<torso>_<part>_<anim>.bam`):

| Toon anim | File |
|---|---|
| `throw` | `pie-throw` |
| `pushbutton` | `press-button` |
| `cringe` | `cringe` |
| `conked` | `conked` |
| `sidestep-left` | `sidestep-left` |
| `sidestep-right` | `jump-back-right` |
| `duck` | `duck` |
| `victory` | `victory-dance` |

`lose` and `sad-walk` come from phase 4/5.

**Props:**

- `phase_3.5/models/props/tart.bam` (cupcake).
- `splat-mod.bam` + `splat-chan.bam`, tinted per `Splats` (`BattleProps.py:201`).
- `squirting-flower.bam`.
- `button.bam`.
- Suit props `phone.bam` and `receiver.bam`.
- `phase_4/models/props/propeller-mod|chan.bam` (fly-in/out).
- `phase_5/models/props/kapow-mod|chan.bam`.
- `splash.bam`.

**Suit (Flunky):**

- Body: `phase_3.5/models/char/suitC-mod.bam`.
- Head: `suitC-heads.bam` (parts `flunky` + `glasses`, `Suit.py:404-410`).
- Scale: 4.0/4.14.
- Dept textures: `phase_3.5/maps/c_blazer.jpg`, `c_leg`, `c_sleeve`
  (`Suit.py:~680`).
- Animations: `suitC-neutral`, `walk`, `pie-small`, `squirt-small`, `lose`,
  `phone`/`shredder`/`throw-paper`, `victory`, `slip-forward`, `sidestep-*`.
- Suit animations load from phase 3.5/4/5/12 depending on body and anim
  (`Suit.py:630-665`).

**Audio:**

- Music: `phase_3.5/audio/bgm/encntr_general_bg.ogg` (`town/TownLoader.py:56`).
- `phase_3.5/audio/sfx/AA_pie_throw_only.ogg`, `AA_tart_only.ogg`.
- `AA_squirt_flowersquirt.ogg` (miss: `phase_5/.../AA_squirt_flowersquirt_miss.ogg`).
- `SA_hangup.ogg`, `Cog_Death.ogg`, `ENC_cogfall_apart.ogg`.
- `phase_5/audio/sfx/ENC_propeller_in|out.ogg`.

**Throw L0 timing** (`MovieThrow.py:16-20, 265-380`):

- The pie leaves the hand at 2.7 s and hits at 3.0 s.
- On a hit, the splat shows at suit height and the suit plays `pie-small-react`.
- On a miss, the suit dodges with `sidestep` at 2.45 s.

**Squirt L0 timing** (`MovieSquirt.py:255-310`):

- The toon plays `pushbutton` with the button prop in the left hand.
- The flower spray starts at 2.5 s and makes contact at 2.7 s.
- The suit plays `squirt-small-react`.

**Suit death** (`MovieUtil.py:300-340`):

- `lose` plays for 6.0 s (`SUIT_LOSE_DURATION`, :11) plus gear particles and a kapow at
  5.4 s.
- **Essential:** the lose animation, the sound and the removal.
- **Skippable:** the particles. Replace them with a sprite burst.

| Element | Implementation status |
|---|---|
| FaceOff walk-in + taunt bubble | **Implemented** (positions the actors; taunt text from `SuitBattleGlobals.getFaceoffTaunt`) |
| Toon throw/squirt anims + prop + hit/miss react + damage number | **Implemented**: gag props in hand via JNT tracks (`def_joint_right_hold`), parabolic pie flight (release 2.7s, hit 3.0s), animated morph splat with pie color tint (purple for tart/fruitpie, yellow for creampie), squirt flower/gun/bottle spray with splash prop (light blue tint), sound megaphone, drop button, heal feather/lipstick |
| Suit attack | **Implemented**: suit props in hand via JNT (`joint_Rhold`/`joint_Lhold`), phone + receiver pound with `SA_hangup` sound, thrown projectile, toon cringe on hit with `toon_hit` sound, sidestep dodge on miss |
| Suit death | **Implemented**: `lose` animation plays for 5.4s, followed by animated `kapow` explosion burst (duration 0.6s) with `Cog_Death` and `ENC_cogfall_apart` audio |
| Battle SFX | **Implemented**: bounded LRU audio cache for battle cues (`pie_throw`, `throw_tart_hit`, `throw_slice_hit`, `throw_miss`, `squirt_*`, `toon_hit`, `SA_hangup`, `Cog_Death`) |
| Gag icons | **Implemented**: 2D gag icon atlas (`inventory_atlas.ttm`, 50 icons) mapped via `GagAtlas` across battle panel cells, Shticker Book Gags tab, and Shop panel |
| Battle camera (`MovieCamera.py`) | Fixed orbit camera |
| Autotest & Verification | Host test `tools/battle_fx_test/` and autotest showcase `"battle fx showcase"` (`battle_fx`) |
| Reward (`MovieToonVictory`, `RewardPanel`) | Bottom-screen panel: exp bars from `origExp + earnedExp` |

## 7. Rewards and experience

- **Earned experience during a round.** Each hit adds `(level+1) × multiplier` to the
  gag's track, capped at `ExperienceCap` = 200 per track per battle
  (`BattleCalculatorAI.py:711-737`, `ToontownBattleGlobals.py:103`). The AI sends the
  running total with `setEarnedExperience(uint16[])` (`DistributedBattleBaseAI.py:1200-1212`).
- **`setBattleExperience`** (toon.dc:891; `BattleExperienceAI.py:14-95`) contains:
  - Four toon blocks: `int32 id (−1 empty), int16[7] origExp, int16[7] earnedExp,
    uint32[] origQuests, int16[] items, int16[] missedItems, int16[4] origMerits,
    int16[4] merits, uint32[4] parts`.
  - Then `uint8[] deathList`, in groups of 4 bytes:
    `suitHeadTypes index, level, toonBits, flags`.
  - Then `int16[] uberList` and `uint32[] helpfulToons`.
- **Commit.** On entering Reward the AI calls `assignRewards`. That sends `setExperience`
  and `setInventory` (newly unlocked gag levels are added) and broadcasts
  `setAnimState('victory')` for each toon (`BattleExperienceAI.py:121-152`).
- Quest and merit credit is server-side. The client only displays it.

## 8. Minimal "fight one cog with throw/squirt" client

**Prerequisites:**

- A street zone (2100) loaded with its visgroup interest. The interest includes the
  branch zone.
- The `.tz` must carry the suit graph and battle cells (§3).
- A new toon owns exactly 1 cupcake and 1 flower (toon.dc:460), so it gets two attacks and
  then has to PASS. Buying gags (gag shop) is a separate feature.

**Message sequence:**

1. ENTER_OBJECT DistributedSuitPlanner (zone 2100). Load the path graph.
2. ENTER_OBJECT DistributedSuit (zone 21xx).
   - Decode DNA, level and HP.
   - Build the path, or use the fallback.
   - Render with walk/neutral.
3. Local toon touches the suit tube. The client sends `DistributedSuit.requestBattle(x,y,z,h,p,r)`,
   then `setSmStop` and `setAnimState('neutral')`. On `denyBattle`, go back to walk.
4. The server generates DistributedBattle with its required fields, including
   `setState('FaceOff', ts)` and `setMembers`. In `setMembers`:
   - If the local toon is in `toons`: send setLocation + interest for `battle.zoneId`.
   - Stop the position broadcast.
   - Send `setAnimState('off')`.
   - Switch the bottom screen to the Gags tab.
5. Play the FaceOff (or snap after ≤ 3.5 s). The client sends `faceOffDone`.
6. Server sends `setMembers` (now active) and `setState('WaitForInput')`.
   - Show the gag grid from inventory × trackAccess × experience thresholds.
   - Start the 20 s timer.
   - The player picks something. The client sends one of:
     - `requestAttack(4, 0, suitDoId)` for throw
     - `requestAttack(5, 0, suitDoId)` for squirt
     - `requestAttack(98, −1, −1)` for PASS
     - `toonRequestRun()` to run away
     - `timeout()` when the timer runs out
7. Server sends `setChosenToonAttacks`, then `setMovie(...)`, then `setState('PlayMovie')`.
   - Play the toon attack, then the generic suit attack.
   - The client sends `movieDone`.
   - The server follows with `setHp`, `setInventory` and `setMembers`.
8. If the suit is alive, go back to step 6.
9. When the suit dies, the server sends `setBattleExperience`, then `setState('Reward')`,
   then `setExperience` and `setInventory`.
   - Show the reward panel.
   - The client sends `rewardDone`.
10. Server sends `setState('Resume')` and `setBattleId(0)`, and deletes the battle object.
    - Restore walk mode and the position broadcast.
    - Send the zone update again with the street vis list.
    - If the local toon went sad (`setHp ≤ 0`), teleport to the playground (zone change,
      see zones.md) instead.

**Must handle (otherwise you get softlocks or disconnects):**

- Send only the clsend fields listed in §1.
- Ignore `setState` transitions that come while our animation is still running. The
  server is authoritative.
- Handle the battle object's deletion at any time. If the AI aborts, the DO leaves
  without Resume.
- Handle `denyLocalToonJoin`.
- Treat other toons' joins (`toonsJoining`) by sending `joinDone(theirId)` after a short
  lerp. Otherwise every round stalls 22 s.
- After `adjust`, send `adjustDone`.

**Effort.** These estimates assume street loading already exists:

| Work | Effort |
|---|---|
| Suit bake (suitC + heads + 8 animations, dept textures) | 2 d |
| Suit DO + path (port of the libtoontown path search; the fallback is 1 d) | 3–4 d |
| Battle protocol / FSM / setMembers / setMovie decoders, plus dcgen work | 3 d |
| Gag UI (bottom screen) + timer | 2 d |
| Movie playback (2 gags + generic suit attack + death) | 3 d |
| Reward panel | 1 d |
| **Total** | **≈14–15 dev-days** |

Full parity (all 7 tracks × 7 levels, lure/trap state, around 100 suit attacks, multi-toon
join/adjust, SOS/NPC/pets, building battles) is **≈60–90 dev-days**, and it is
particle-heavy.

## Client implementation status (street cogs walking, no battles)

Branch work, 2026-10-24. Cogs generate, walk their street paths and fly in and out on the
3DS client. Nothing was run in Azahar or on hardware; every claim below says how it was
checked. Battles are not started: a cog never sends `requestBattle`.

### What is implemented

| Piece | Where | Checked by |
|---|---|---|
| DC surface: `DistributedSuitPlanner`, `DistributedSuit` (all fields in section 1 plus inherited base fields) in `WANTED` and `TABLE_CLASSES` | `tools/dcgen/dcgen.py`, `source/net/dc_generated.h` (DC hash still 547155168) | `tools/nettest` unit: 621 field descriptors against Panda-packed vectors, 0 failures |
| Net parse: planner `setZoneId`, suit `setDNAString/LevelDist/Skelecog/SkeleRevives/HP/SPDoId/PathEndpoints/PathPosition/PathState`, location changes, leaving | `source/net/client.{h,cpp}` (`Suit`, `SuitPlanner`, `findSuit`, `suitPlannerZone`; events `SuitSpawn/SuitUpdate/SuitGone/SuitPlannerUp`); every layout has a `TT_SIG` static_assert | nettest single-client scenario: `tools/nettest/fake_server.py` now generates a planner and a Flunky; the client logs both |
| Street suit graph (`.tsp`) for all 17 streets | `tools/pipeline/bake_suit_paths.py` | `bake_suit_paths.py --check`; 274 points / 511 edges for 2100 match section 3 |
| Path search and leg list | `source/world/suit_path.{h,cpp}` (pure C++, no libctru) | `tools/suit_test/run.sh`: 6,800 random queries against the real libtoontown, see below |
| Cog table (name, dept, base level, hp, height) | `tools/gen_suit_tables.py` -> `source/world/suit_tables.h` | `--check` |
| Runtime actors: placeholder box, nametag, shadow | `source/world/suits.{h,cpp}`, `romfs/baked/suit_placeholder.ttm` (`tools/pipeline/bake_suit_placeholder.py`), wiring in `source/main.cpp` | `make -j4`: zero warnings in `source/` |

### The libtoontown question

The section 3 "Risk" no longer applies. The Open Toontown Panda3D fork that
`tools/server/setup.sh` builds contains the real `DNAStorage::get_suit_path_breadth_first`
and `SuitLegList` sources (`tools/server/.deps/panda3d-ott/panda/src/{dna,suit}`), and its
`panda3d.toontown` module is importable from `tools/server/.venv`. So the port is a
step-for-step reimplementation, not the documented fallback, and it is verified against that
module (`tools/suit_test/reference.py` records answers, `suit_test.cpp` replays them). The
sources were read as a specification and nothing was copied. Result on the 17 streets: 6,800
random (start, end, minLen, maxLen) queries, 5,329 with a path; every path matches point for
point, all 452,391 legs match (type, start time, leg time, zone, points A/B, block), 1.3
million walk-leg positions match `SuitLeg.getPosAtTime`, 0 failures.

Details of the search that are easy to get wrong (all reproduced):

- Working paths form a chain. Each generation prepends every new path as it is created, so
  the next chain is the creation order **reversed**; the first path in chain order that
  touches the end point wins.
- The first `minLen - 1` generations only expand: loops allowed, door points
  (FRONT_DOOR/SIDE_DOOR, "terminal") never entered, never straight back to the previous
  point, and the end point is not looked for.
- Later generations check for an edge to the end point **first** (any edge), then extend
  with the same rules, and each point may be claimed once per generation. The original takes
  `visited_points` by value, so the set starts empty every generation.
- Edge order is the order `store_suit_edge` accepted them (dropped: unknown end points,
  start == end, repeated pairs). That is why the `.tsp` keeps file order.
- A path is capped at `maxLen` points (the AI uses 40 / 300). Street graphs are mostly one
  lane wide, so working sets stay tiny (at most a handful of live walks per generation in the
  pre-pass); the working pool grows on the heap and gives up cleanly at about a million
  nodes.

### File formats

`.tsp` (`TSP1`, little endian), next to `<branch>.tz` in `sdmc:/3ds/toontown3ds/data/zones/`;
the authoritative spec is the docstring of `tools/pipeline/bake_suit_paths.py`:

| Section | Size | Contents |
|---|---|---|
| Header | 16 | `"TSP1"`, u16 version 1, u16 branch zone, u16 numPoints, u16 numEdges, u16 numCells, u16 0 |
| Point | 20 each | u16 DNA index, u8 type (0 street, 1 front door, 2 side door, 3 CogHQ in, 4 CogHQ out), u8 0, f32 x y z, u16 landmark block, u16 0 |
| Edge | 6 each | u16 start index, u16 end index, u16 zone (the visgroup it is in) |
| Battle cell | 16 each | u16 zone, u8 width, u8 height, f32 x y z |

The 17 files total 115 KB (4–10 KB each); at runtime one graph is resident (about 10 KB).
Output is in `sdmc-out/3ds/toontown3ds/data/zones/<2100,2200,2300,1100,1200,1300,3100,3200,
3300,4100,4200,4300,5100,5200,5300,9100,9200>.tsp` (gitignored; regenerate with
`.venv/bin/python -I tools/pipeline/bake_suit_paths.py`).

### Runtime behaviour

The street graph is chosen by the cog's planner (`suitPlannerZone(spDoId)`), falling back to
the branch of the zone the player is in. `setPathEndpoints` rebuilds the path (DNA indices go
straight to `genPath`; `maxPathLen == 0` means no path yet). `setPathPosition(index, ts)`
gives `elapsed = (now - localTime(ts)) + legStartTime(index)`, using the same
`Client::networkToLocal` as the classic characters. Each leg's pose follows
`DistributedSuit.enter<Leg>`:

| Leg | Pose |
|---|---|
| Walk | lerp A -> B over the leg time |
| WalkToStreet / WalkFromStreet | start 2.5 units out of the door / stop 2.5 units short of it (`STAND_OUTSIDE_DOOR`), floor ray on |
| FromSuitBuilding / ToSuitBuilding | walk out to / in from 2.5 units from the door along the door-to-street line, over `walkSpeed * legTime` more units into the building |
| ToToonBuilding | stand 2.5 units short of the front door, neutral |
| FromSky | descend from `6.5 * 4.8 = 31.2` units above point A over 4.375 s, then wait 2.125 s; fade in over 1 s |
| ToSky | wait 2.125 s, climb 31.2 units over 4.375 s, fade out over the last second |
| FromCogHQ / ToCogHQ | hidden / neutral (no suit street has CogHQ doors) |
| Off | hidden |

The fly constants come from `suit{A,B,C}-landing.bam` (79 frames at 24 fps, `groundF` 28), as
in `DistributedSuitBase.beginSupaFlyMove`. Heading is `calculateHeading(A, B)`. Path states:
1 walks; 0 and 3 freeze where the cog is (3 is a no-op upstream); 2 flies away from the
current pose; 4 waits 9.08 s (victory dance) then flies away. The cog is hidden until a path
state puts it on a leg, like the FSM's Off state.

**Fallback.** If the graph file is missing or no path exists, the cog stands on the battle
cell of its zone (from the `.tsp`) and is hidden when there is none. It does not wander.

**Nametag.** `"<Name> Level <n>"` through `ui::Balloons`, n = `baseLevel + levelDist + 1`
(`SuitBase.getActualLevel`), coloured per department. Upstream also shows the department on
a second line; the single line fits the existing balloon code.

### Remaining

- **Battle start.** Detect contact with the cog's body tube (`CollisionTube(0, 0, 0.5, 0, 0,
  height - 1, r = 1)`) on Walk/FromSky/ToSky/door legs; go to place state WaitForBattle; send
  `requestBattle(x, y, z, h, p, r)` through a new `TT_NET_UPDATE(w, suitDoId, DistributedSuit,
  requestBattle)` (the id and signature are already generated; the whitelist entry and a
  sender do not exist); handle `denyBattle` (return to walk) and `setBrushOff` (chat bubble).
  Then everything in sections 4 to 7 (`DistributedBattle` parse, FaceOff, movies, rewards).
  Position broadcasts must stop while `battleId != 0`.
- **Real cog models.** Replace the box with the baked suit A/B/C bodies, heads, department
  textures and the walk, neutral, landing and victory animations; propeller prop and fade
  (the pose already carries `alpha` and `anim`); door-open animation for the door legs;
  skelecog look; health bar; `setHP` is parsed but unused.
- **Shared placeholders:** `kMaxSuits` is 16 (a street visibility set rarely holds more);
  extra cogs are dropped with a log line.
- **Not measured:** per-frame cost of up to 16 cogs, SD read of the `.tsp` on hardware,
  path search time on an Old 3DS (the whole 6,800-query host test, including 7 million pose
  samples, runs in 0.65 s on the PC).
  Not run in Azahar or on hardware.
- **Not parsed:** `DistributedSuit` subclasses of other families (`DistributedFactorySuit`,
  boss cogs, tutorial suit), `DistributedBuilding` suit takeovers, planner list queries,
  `debugSuitPosition` (the debug-only resync check).
