# Facility Room Games Specification

Status: Phase 4+, implemented on branch `levelgames`.

This document details the client-side design, network protocol, mathematical models, asset pipeline,
and runtime execution for the five facility room games across Cog Headquarters:
1. **Laser Fields & Security Cameras** (Lawbot DA Offices / Stages)
2. **Crates on Grids & Crushers** (Sellbot Factory & Cashbot Mints)
3. **Mazes** (Bossbot Country Clubs)
4. **Mole Fields** (Bossbot Country Clubs)
5. **Golf Greens: Bust-a-Cog** (Bossbot Country Clubs)

---

## 1. Laser Fields & Security Cameras (`DistributedLaserField`, `DistributedSecurityCamera`)

### 1.1 `DistributedLaserField` (Lawbot DA Offices)
- **Base class**: `BattleBlocker` (blocks room doorway until solved or battle won).
- **Rooms**:
  - `LawbotOfficeBoilerRoom_Security00`
  - `LawbotOfficeDiamondRoom_Security00`
  - `LawbotOfficeGearRoom_Security00`
  - `LawbotOfficeLobby_Trap00`
  - `LawbotOfficeBoilerRoom_Trap00`
  - `LawbotOfficeDiamondRoom_Trap00`

#### Network Surface (`toon.dc`):
| Field | Type | Keywords | Direction | Purpose |
|---|---|---|---|---|
| `setGrid` | `uint8 gridNumX, uint8 gridNumY` | required broadcast ram | S→C | Grid dimensions (e.g. 8×8 or 12×12) |
| `setField` | `uint8 fieldData[]` | required broadcast ram | S→C | Layout: `[0]=gridNumX, [1]=gridNumY, [2..]=symbols` |
| `setActiveLF` | `uint8 active` | broadcast ram | S→C | Enable or disable field lasers |
| `setSuccess` | `uint8 success` | broadcast ram | S→C | 1 = puzzle solved, 0 = fail / trip alarm |
| `setGridGame` | `string gameName` | broadcast ram | S→C | Game type (`MineSweeper`, `Roll`, `Avoid`, `Drag`) |
| `hit` | `int8 newX, int8 newY, int8 oldX, int8 oldY` | airecv clsend | C→S | Local toon stepped on tile `(newX, newY)` |
| `trapFire` | `()` | airecv clsend | C→S | Toon triggered hazard tile / camera beam |

#### Upstream Behaviour & Client Duties:
1. **Grid Transform**: Spans `[0, gridScaleX]` in local X and `[0, gridScaleY]` in local Y.
   - Tile indices: `tileX = floor(localX / (gridScaleX / gridNumX))`, `tileY = floor(localY / (gridScaleY / gridNumY))`.
2. **Visuals** (`createGrid`, drawn by `Renderer::drawAdditive`): no floor model. Each tile whose
   `setField` symbol is non-zero gets that symbol's outline (`makeGridSymbols`, generated into
   `source/world/laser_symbols.h` by `tools/gen_laser_symbols.py`) as a 1-pixel wire in symbol colour
   x 0.25, plus a beam fan from the spec `projector` point (black there, symbol colour x 0.04 on the
   floor), all additive (`ColorBlendAttrib.MAdd`) with no depth write at `zFloat = 0.05`. The Toon's tile
   adds `symbolSelect`. `genGrid` only runs while `activeLF`, so the drawn grid freezes when the AI
   turns the field off.
   - `setField` is `[gridNumX, gridNumY, column-major tiles]`: tile (x, y) is `fieldData[2 + x * numY + y]`.
   - Traps (`sendFail`) are symbols 11 (hidden bomb) and 12 (skull). 13 is the harmless green triangle.
   - In range (`__detect`, 0.75 of the larger side from the centre) the camera goes to
     (0, -20 h / 3, h + 8) and the stage shows the game name (`LaserGame*`; stock uses the suit font,
     we have only the sign font).
3. **Interactivity**:
   - As the local toon moves onto a tile, detect index change and send `hit(newX, newY, oldX, oldY)`.
   - Stepping on a hazard tile (skull/bomb in Avoid/Minesweeper) sends `trapFire()`.
   - On `setSuccess(1)`: plays `LB_capacitor_discharge_3.tsnd`, deactivates lasers.
   - On `setSuccess(0)`: plays `LB_sparks_1.tsnd`, alarm sounds, blocker starts Cog battle.

---

### 1.2 `DistributedSecurityCamera` (Lawbot DA Offices)
- **Base class**: `DistributedEntity`
- **Rooms**: Paired with laser fields in Lawbot DA Office Security rooms.

#### Network Surface (`toon.dc`):
| Field | Type | Keywords | Direction | Purpose |
|---|---|---|---|---|
| `setTarget` | `uint8 targetHash` | broadcast ram | S→C | Selects which entity anchor the camera pursues |
| `trapFire` | `()` | airecv clsend | C→S | Toon caught in camera spotlight |

#### Upstream Behaviour & Client Duties:
1. **Tracking**: nothing moves until `setTarget` arrives. `targetHash % 3` counts through the spec's
   `trackTarget1..3` that are set; the floor spot (starting at (10, -5)) chases that entity with the
   spec's `accel` / `maxVel` (per frame, as upstream), decelerating within `radius / 2 + 1`.
2. **Visuals**: `square_stomper` (baked whole as `security_camera.ttm`) at (0, 1, 0) under a node at the
   spec `projector` that looks at the spot (`hideModel` hides it); a beam fan from the projector (grey
   0.2 there, black on the floor) and a floor disk (grey 0.2, red for 2 s after `trapFire`), additive,
   one-sided, no depth write.
3. **Detection**: the Toon within `radius` of the spot (x / y) sends `trapFire` at most every 2 s, with
   the alarm and shutter sounds.

---

## 2. Crates on Grids & Crushers (`DistributedCrate`, `DistributedGrid`, `DistributedCrushableEntity`)

### 2.1 `DistributedCrate` (Sellbot Factory & Cashbot Mints)
- **Base class**: `DistributedCrushableEntity`
- **Rooms**:
  - Sellbot Factory: Crate rooms (`SellbotLegFactorySpec.py`)
  - Cashbot Mints: `CashbotMintBoilerRoom_Action00`, `CashbotMintGearRoom_Battle01`, `CashbotMintDuctRoom_Action00`, etc.
- **Models**:
  - Factory: `phase_9/models/cogHQ/woodCrateB.bam` (modelType 0) or `metal_crateB.bam`
  - Mint: `phase_10/models/cashbotHQ/CBWoodCrate.bam` (modelType 1) or `CBMetalCrate2.bam`

#### Network Surface (`toon.dc`):
| Field | Type | Keywords | Direction | Purpose |
|---|---|---|---|---|
| `requestPush` | `uint8 side` | airecv clsend | C→S | Request to push crate from side (0..3) |
| `setReject` | `()` | broadcast | S→C | Push rejected (blocked or invalid) |
| `setAccept` | `()` | broadcast | S→C | Push accepted by server |
| `setMoveTo` | `uint32 avId, int16/10 x0,y0,z0, x1,y1,z1` | broadcast ram | S→C | Animate crate & pushing avatar |
| `setDone` | `()` | airecv clsend | C→S | Crate movement completed |
| `setCrushed` | `uint32 crusherDoId, uint8 axis` | broadcast ram | S→C | Crate crushed by stomper |

#### Upstream Behaviour & Client Duties:
1. **Push Sides & Orientations**:
   - `CrateNormals`: `[0] = (+1, 0, 0)`, `[1] = (-1, 0, 0)`, `[2] = (0, +1, 0)`, `[3] = (0, -1, 0)`.
   - `CrateHprs`: `[0] = 90°`, `[1] = 270°`, `[2] = 180°`, `[3] = 0°`.
2. **Push Detection**:
   - Toon walks against crate face with forward movement (`arrow_up` / Circle Pad).
   - Surface normal dot product determines `crateSide`.
   - Crate sends `requestPush(side)` and plays `CHQ_FACT_crate_effort.tsnd`.
3. **Movement**:
   - On `setMoveTo(avId, x0, y0, z0, x1, y1, z1)`:
     - Interpolates crate position from `p0` to `p1` over `T_PUSH = 1.5 s`.
     - Plays sliding sound `CHQ_FACT_crate_sliding.tsnd`.
     - If pushing toon is local toon, carries toon forward with crate.
     - Upon arrival, local toon sends `setDone()`.
4. **Collision & Stomper Crushing**:
   - Solid bounding box: `[-1.4, -1.4, 0]` to `[+1.4, +1.4, 2.8]` (scaled by `scale`).
   - Top face acts as a walkable moving platform floor.
   - If stomper descends on crate: `setCrushed` triggers squash animation (`Z` scales down to 0.25, `X/Y` expands to 1.2), then pops back up after 2.0 s.

---

### 2.2 `DistributedGrid` & `DistributedGridGoon`
- **`DistributedGrid`**: `FloorWear.bam` (`grid_floor.ttm`) scaled `cellSize / 6` times the long and short
  sides (turned 90 when there are more rows than columns), flipped (`P 180`) and centred 0.025 above the
  grid. Its own colour (white, alpha 0.36) wins over the node's `setColor`, so stock shows faint white scuffs.
- **`DistributedGridGoon`**: Inherits from `DistributedGoon`, receives `setPathPts(xi, yi, zi, xf, yf, zf)` to navigate grid lanes between crate obstacles.
- **Crushed crates** (`crushTask`): squashed to (1.2, 1.2, minScale) under the stomper and set back to
  scale 1 two seconds later; we use 0.25 for minScale.

---

## 3. Mazes (`DistributedMaze`)

- **Rooms**:
  - `BossbotCountryClubMazeRoom_Battle00`
  - `BossbotCountryClubMazeRoom_Battle01`
  - `BossbotCountryClubMazeRoom_Battle02`

#### Network Surface (`toon.dc`):
| Field | Type | Keywords | Direction | Purpose |
|---|---|---|---|---|
| `setRoomDoId` | `uint32 roomDoId` | required broadcast ram | S→C | Room containing maze geometry |
| `setGameStart` | `int16 timestamp` | broadcast | S→C | Maze timer starts |
| `setClientTriggered` | `()` | airecv clsend | C→S | Toon entered maze entrance trigger |
| `setFinishedMaze` | `()` | airecv clsend | C→S | Toon reached maze exit finish trigger |
| `toonFinished` | `uint32 avId, uint8 place, uint8 lastToon` | broadcast | S→C | Notification of toon reaching finish |
| `setGameOver` | `()` | broadcast | S→C | Game timeout / completion |
| `damageMe` | `()` | airecv clsend | C→S | Penalty damage on timeout |

#### Upstream Behaviour & Client Duties:
1. **Procedural Geometry Setup**:
   - Each maze room contains 1 to 4 sections (`Maze_Inside_0`, `Maze_Inside_1`, `Maze_Inside_2`, `Maze_Inside_3`).
   - Rotations shuffled deterministically: `rotations = [0, 0, 90, 90, 180, 180, 270, 270]` using RNG `random.Random(entId * roomDoId)`.
   - Removes hedge blocks (`HedgeBlock_0_1`, etc.) per section count using the same RNG.
2. **Gameplay Flow**:
   - **Entrance**: Radius 8.0 sphere at `ENTRANCE` node. Walking in sends `setClientTriggered()`.
   - **Duration**: `GameDuration = 35.0 + numSections * 15.0` seconds. Countdown timer shown on screen.
   - **Exit**: Radius 15.0 sphere at `finish` node. Walking in sends `setFinishedMaze()`.
   - **Timeout**: If timer expires and toon has not finished:
     - Sends `damageMe()` (takes 20 damage).
     - Teleports toon to exit finish node.

---

## 4. Mole Fields (`DistributedMoleField`)

- **Rooms**:
  - `BossbotCountryClubTeeOffRoom_Action00`
  - `BossbotCountryClubTeeOffRoom_Action01`
  - `BossbotCountryClubTeeOffRoom_Action02`
- **Models**:
  - Hole: `phase_12/models/bossbotHQ/mole_hole.bam`
  - Normal mole (whackable): `phase_12/models/bossbotHQ/mole_norm.bam`
  - Cog bomb mole (hazard): `phase_12/models/bossbotHQ/mole_cog.bam`
  - Whacked mole: `phase_12/models/bossbotHQ/mole_hit.bam`

#### Network Surface (`toon.dc`):
| Field | Type | Keywords | Direction | Purpose |
|---|---|---|---|---|
| `setGameStart` | `int16 ts, uint8 target, uint16 duration` | broadcast | S→C | Starts mole field game |
| `setClientTriggered` | `()` | airecv clsend | C→S | Toon stepped onto field |
| `whackedMole` | `int8 moleIndex, int16 popupNum` | airecv clsend | C→S | Successfully whacked normal mole |
| `whackedBomb` | `int8 moleIndex, int16 popupNum, int32 ts` | airecv clsend | C→S | Stepped on bomb mole hazard |
| `updateMole` | `int8 moleIndex, int8 status` | broadcast | S→C | Mole status update from other player |
| `setScore` | `int16 score` | required broadcast ram | S→C | Team total moles whacked |
| `damageMe` | `()` | airecv clsend | C→S | Penalty damage on restart |
| `setPityWin` | `()` | broadcast | S→C | Game auto-completed by server |

#### Upstream Behaviour & Client Duties:
1. **Grid Setup**:
   - `numSquaresX * numSquaresY` mole hills (e.g. 4×4 = 16 hills).
   - Spaced by `spacingX` and `spacingY` (typically 8.0 units).
2. **Deterministic Schedule (`MoleFieldBase.scheduleMoles`)**:
   - Seeded with `random.Random(entId * levelDoId)`.
   - Generates exact list of `(startTime, moleIndex, moveUpTime, stayUpTime, moveDownTime, moleType)`:
     - `MoveUpTime`: starts at 1.0 s, decays by 0.95x down to 0.5 s min.
     - `StayUpTime`: starts at 7.0 s, decays by 0.95x down to 3.0 s min.
     - `MoveDownTime`: starts at 1.0 s, decays by 0.95x down to 0.5 s min.
     - `TimeBetweenPopup`: starts at 1.5 s, decays by 0.95x down to 0.25 s min.
     - Ratio: 3 normal moles (`HILL_MOLE`) to 1 cog bomb (`HILL_BOMB`).
3. **Collisions & Whacking**:
   - Radius 1.0 collision sphere at mole head while mole is raised.
   - Stepping on normal mole (`HILL_MOLE`):
     - Plays `Mole_Stomp.tsnd` / `MG_Tag_C.tsnd`.
     - Mole turns into `mole_hit.bam` squash animation.
     - Sends `whackedMole(moleIndex, popupNum)`.
   - Stepping on Cog bomb (`HILL_BOMB`):
     - Plays explosion `Mole_Surprise.tsnd` and toon yell.
     - Sends `whackedBomb(moleIndex, popupNum, timestamp)`. Toon takes damage and stumbles.
4. **Win Condition**:
   - When score matches `molesWhackedTarget`, game is won! Blocker deactivates.

---

## 5. Golf Greens: Bust-a-Cog (`DistributedGolfGreenGame`, `world/bust_a_cog.*`)

- **Base class**: `BattleBlocker`
- **Rooms**: `BossbotCountryClubGreenRoom_Action00..02` (`timeToPlay` 120 s, `puzzleBase` 3 + 1 per Toon).
- **Models** (`bake_level_props.py golf bustacog`): on the floor `bust_a_cog_golf_green` (0.24) and its
  flag (0.5, H -45 at (3, 4, 0), hidden while playing); the board `bust_a_cog_background`, the shooter
  (`moving_part` baked apart, turned by the aim), the holes (`bust_a_cog_hole`) and GameSprite3D's balls
  `bust_a_cog_ball_*`; sounds as `bac_*.tsnd` next to them.

#### Network Surface (`toon.dc`):
| Field | Type | Direction | Purpose |
|---|---|---|---|
| `requestJoin` | `()` | C→S | the Toon entered the 11.5 sphere at the green |
| `leaveGame` | `()` | C→S | Exit for Now, or after `signalDone` |
| `acceptJoin` | `uint16 time, int32 timestamp, uint32 avIds[]` | S→C | joined Toons; with us newly in it: start, then `requestBoard(0)` |
| `requestBoard` | `uint8 success` | C→S | the last board's result; asks for the next |
| `startBoard` | `{posX, posZ, typeIndex}[], uint8 attackPattern[]` | S→C (to us) | a board |
| `boardCleared` | `uint32 avId` | S→C (to us) | someone else cleared our board: fail it |
| `helpOthers` | `uint32 avId` | S→C | another Toon solved one: our next ball is a bomb |
| `informGag` | `uint8 track, uint8 level` | S→C (to us) | the gag won (bonus board, not drawn yet) |
| `scoreData` | `uint8 total, uint8 closed, {avId, score}[]` | S→C | Puzzles Left = total - closed |
| `setTimerStart` | `uint16 time, int32 timestamp` | S→C | the ToontownTimer |
| `signalDone` | `uint8 success` | S→C | all done (Success! / Out of Time!); a failure costs every joined Toon 20 laff |

#### Game (client side, from `DistributedGolfGreenGame` + `GameSprite3D`)
- Board frame: under the green at (0, 10, 2), P 90 (lying flat), seen from `__setCamera`: (0, 12, 27),
  H 180, P -70, vertical fov 46.8. The Toon stands at `toonPoints[index]` facing H 180.
- 9 x 15 hex grid, cell 1.4 x 1.12; balls of radius 0.7. Types: 0 red, 1 blue, 2 green, 3 the Cog ball,
  4 black, 5 purple, 6 yellow, 7 bomb (orange / black), 8 wild (cycling colours), 9 steel (unbreakable),
  10 fire (gives a bomb), 11 unknown (gives a wild ball).
- The attack pattern feeds the shooter and the waiting ball; a shot flies at 14 units/s, bounces off the
  side walls, sticks to the nearest open cell with a neighbour (any cell at the top). Three or more of a
  colour (wild matches any) clear; balls no longer connected to the top fall; bombs clear their
  neighbours. Every 10 s the board rolls down a row (warning bump at 3, 2, 1); a ball at the bottom
  row fails the board. No Cog balls left = solved. Each result goes back as `requestBoard`.
- 3DS controls: Circle Pad or D-Pad turns the aim, A shoots, B is Exit for Now.
- Not drawn yet: the Toon head score panels, the bonus board (`informGag` / `helpOthers`), the How to
  Play dialog, the Quit / timer buttons' QuitBtn art (the row countdown is not shown), the kapow on bombs.

---

## 6. Verification Checklist in Azahar & Hardware

1. **`da office: lasers`**:
   - Verify laser grid lines are rendered on floor tiles without Z-fighting (`zFloat = 0.05`).
   - Verify stepping onto tiles sends `hit()` and triggers symbol updates.
   - Verify stepping on a skull/bomb sends `trapFire()` and plays spark sfx.
2. **`da office: security cam`**:
   - Verify camera rotates smoothly and spotlight tracks target on the floor.
   - Verify entering camera cone triggers alarm sound and turns beam red.
3. **`factory: crates`**:
   - Verify crate collision box blocks toon passage and allows jumping on top.
   - Verify walking into crate face sends `requestPush()` and plays creak sound.
   - Verify crate smoothly slides across grid over 1.5 s and carries pushing toon.
   - Verify stomper squashes crate and crate recovers.
4. **`club: maze`**:
   - Verify maze sections are rotated deterministically and hedges block passage.
   - Verify entering entrance trigger starts timer, and reaching exit sends `setFinishedMaze()`.
5. **`club: moles`**:
   - Verify mole hills appear in grid and moles pop up according to schedule.
   - Verify stepping on normal mole whacks it and increments score counter.
   - Verify stepping on bomb mole causes explosion sound and hazard damage.
6. **`club: golf green`**:
   - Verify 4 tee-off spots are placed in front of green.
   - Verify stepping on spot sends `requestJoin()` and blocker opens upon completion.
