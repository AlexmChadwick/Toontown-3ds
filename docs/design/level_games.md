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
2. **Visuals**:
   - Ceiling projector node at `(gridScaleX * 0.5, gridScaleY * 0.5, 25.0)`.
   - Grid wireframe / lines connecting cell borders on the floor (`zFloat = 0.05`).
   - Active symbols drawn at tile centers (Minesweeper numbers 1-9, bombs, skulls, colored tiles).
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
1. **Tracking Mechanics**:
   - Projector anchored at `(0, 0, 25.0)` in entity space.
   - Floor target `(trackX, trackY)` smoothly pursues `(targetX, targetY)` of target entity 1, 2, or 3:
     - `accel = 0.5`, `maxVel = 1.0`, `deccel = 1.0 - 7.0 * dt`.
     - Direction vector normalized and scaled by component proportions.
2. **Detection**:
   - Spotlight radius: `self.radius = 5.0` units on the floor.
   - Distance: `sqrt((localToonX - trackX)^2 + (localToonY - trackY)^2)`.
   - If `distance < radius` and `canDamage`:
     - Plays alarm `CHQ_GOON_tractor_beam_alarmed.tsnd` and shutter `LB_camera_shutter_2.tsnd`.
     - Spot turns from green/white to red alert.
     - Sends `trapFire()`. Cooldown period: 2.0 s.

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
- **`DistributedGrid`**: Renders textured floor wear quad `phase_9/models/cogHQ/FloorWear.bam` sized to `(cellSize * numCol, cellSize * numRow)`.
- **`DistributedGridGoon`**: Inherits from `DistributedGoon`, receives `setPathPts(xi, yi, zi, xf, yf, zf)` to navigate grid lanes between crate obstacles.

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

## 5. Golf Greens: Bust-a-Cog (`DistributedGolfGreenGame`)

- **Base class**: `BattleBlocker`
- **Rooms**:
  - `BossbotCountryClubGreenRoom_Action00`
  - `BossbotCountryClubGreenRoom_Action01`
  - `BossbotCountryClubGreenRoom_Action02`
- **Models**:
  - Board hole: `phase_12/models/bossbotHQ/bust_a_cog_hole.bam`
  - Ball: `phase_6/models/golf/golf_ball.bam`

#### Network Surface (`toon.dc`):
| Field | Type | Keywords | Direction | Purpose |
|---|---|---|---|---|
| `requestJoin` | `()` | airecv clsend | C→S | Toon stepped on golf tee-off spot |
| `leaveGame` | `()` | airecv clsend | C→S | Toon exited golf spot |
| `acceptJoin` | `uint16 boardsLeft, int32 timer, uint32 joined[]` | broadcast ram | S→C | Joins player into game session |
| `requestBoard` | `uint8 index` | airecv clsend | C→S | Request puzzle board data |
| `startBoard` | `golfGreenGameBoardData board[], uint8 choices[]` | broadcast | S→C | Initial board ball layout |
| `signalDone` | `uint8 boardIndex` | broadcast ram | C→S | Board cleared |
| `scoreData` | `uint8 boardsLeft, uint8 total, scoreData[]` | broadcast ram | S→C | Score and remaining boards update |

#### Upstream Behaviour & Client Duties:
1. **Golf Spots**:
   - 4 tee-off spots: `(3, 13, 0)`, `(6, 13, 0)`, `(-3, 13, 0)`, `(-6, 13, 0)`.
   - Stepping onto a spot sends `requestJoin()`. Server accepts with `acceptJoin()`.
2. **Board Puzzle**:
   - 9 columns × 15 rows staggered hexagonal grid.
   - Colored spheres (Red, Blue, Green, Yellow, Wild, Bomb).
   - Clearing matches of 3 or more of the same color eliminates rows.
   - When board is cleared, sends `signalDone(boardIndex)`.
3. **Blocker Resolution**:
   - When all required boards are cleared (`boardsLeft == 0`), game is won and blocker opens.
   - If timer runs out or toons choose combat, battle blocker initiates Cog fight.

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
