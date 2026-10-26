# Trolley minigames: cross-play contract (3DS and web clients)

Status: specification only. Written from the upstream Python and `etc/toon.dc`; nothing here has been run against a server.
Scope: the 16 trolley games (ToontownGlobals ids 1-16), the shared `DistributedMinigame` base, and the PurchaseManager exit.
Upstream paths below are relative to `ref/open-toontown/toontown/minigame/` unless noted. `DC` means `ref/open-toontown/etc/toon.dc`.

Two clients that play together must agree on:
1. **Protocol**: every clsend/airecv field name, argument order, and type from the DC (hash 547155168).
2. **Shared math**: any value one client computes that another client shows, or that the AI trusts, must match upstream on every client.
3. **Shared randomness**: the same RNG algorithm, the same seed, and the same draw order.
4. **Time**: the same clock conversion (`globalClockDelta`) for every AI timestamp.

Unclear items are collected in the last section. Nothing here is verified in Azahar or on hardware.

---

## 0. Shared rules

### 0.1 Base class (DistributedMinigame)

| Item | Upstream | Contract |
|---|---|---|
| DC | `DC` `dclass DistributedMinigame` | `setParticipants(uint32[])`, `setTrolleyZone(uint32)`, `setStartingVotes(uint16[])`, `setMetagameRound(int8)`, `setDifficultyOverrides(int32,int32)` are broadcast ram required. `setAvatarJoined/Ready/Exited()`, `requestExit()` are airecv clsend. `setGameReady()`, `setGameStart(int16)`, `setGameExit()`, `setGameAbort()` are broadcast. |
| setParticipants | `DistributedMinigame.py:204-215` | Stores the list; the local toon must be in it, otherwise the client ignores the game. Remote ids = list minus local. |
| setDifficultyOverrides | `DistributedMinigame.py:223-229`, `MinigameGlobals.py:8-11` | Non-sentinel `difficultyOverride / 65536.0`. Sentinel is `2147483647` (`MinigameGlobals.py:14`); trolley override `-1`. |
| Join | client `DistributedMinigame.py:102` sends `setAvatarJoined`; AI `DistributedMinigameAI.py:184-215` barrier with `JOIN_TIMEOUT` = 40+10 s | Every client sends once after generate. |
| Ready | `setGameReady` `DistributedMinigame.py:231-268`; AI `DistributedMinigameAI.py:217-236`, `READY_TIMEOUT` = 40+16+10 s | Client sends `setAvatarReady()` after its rules panel (`handleRulesDone`, `DistributedMinigame.py:341-344`, rules last 16 s, `MinigameGlobals.py:6`). If a remote toon is missing from `cr.doId2do`, the client returns 1 and waits for the server to abort (`:244-246`). |
| Start | `setGameStart(int16)` `DistributedMinigame.py:271-276`; AI `:251-256` | `gameStartTime = globalClockDelta.networkToLocalTime(timestamp)`. The AI stamps with `localToNetworkTime(globalClock.getRealTime())`. Every game's local clock starts here. |
| Local game time | `DistributedMinigame.py:404-410` | `local2GameTime(t) = t - gameStartTime`. Timings that go on the wire are in game-time seconds unless the field is a network timestamp. |
| Exit | client `setGameExit` `:369-381` and `requestExit` `:316-318`; AI `:259-288`, `EXIT_TIMEOUT` = 20+10 s | `requestExit()` (clsend) during play calls `setGameAbort` on the AI (`DistributedMinigameAI.py:398-400`). |
| Abort | `setGameAbort` `DistributedMinigame.py:278-283`; AI `:163-167` | Client sets `normalExit = 0` and cleans up. Scores then use a random reward (see 0.3). |
| Game over | `gameOver` `DistributedMinigame.py:285-289`; AI `enterFrameworkCleanup` `:293-330` | Client waits for `setGameExit`. |
| Cleanup | `DistributedMinigame.py:390-402` | Runs cleanup actions, then `messenger.send(hood.minigameDoneEvent)`. |
| Random source | `DistributedMinigame.py:79-88` | `self.randomNumGen = RandomNumGen.RandomNumGen(self.doId)`. Every client seeds with the same doId, so draws match if the algorithm matches (see 0.2). |

### 0.2 Shared randomness (applies to many games)

Three RNG sources appear. Each one needs an exact copy on the 3DS:

- **Panda `RandomNumGen(seed)`** (`direct.showbase.RandomNumGen`): used via `self.randomNumGen` by Maze, Catch, CogThief, Tug-of-War, and `RandomNumGen(self.randomNumGen)` child streams. Seed is the minigame object's doId. The source is in Panda3D, not in `ref/`, so **the algorithm is not verified here**. The 3DS must reproduce it, including `shuffle`, `random`, `choice`, `randrange`, and the child-stream constructor from an RNG.
- **Python `random` global** (Mersenne Twister, MT19937): `random.seed(int)` then `random.random()`, `randint`, `randrange`, `choice`, `shuffle`. Used by Target (`DistributedTargetGame.py:409-412`, seed from AI, `DistributedTargetGameAI.py:56-59`), Photo (`PhotoGameBase.py:14`, `random.seed(self.doId)`), and Pairing via `random.Random(seed)` in `PlayingCardDeck.py:14-18`.
- **Unseeded random on the AI**: Diving spawns and crab values (`DistributedDivingGameAI.py:289-316, 394-410`). Clients do not regenerate these; they only receive the values.

The Python `random` and `random.Random` behavior is part of CPython, so it must be copied bit-for-bit (seeding from an int uses the init_by_array path, and `shuffle`/`randint` use `_randbelow`). The 3DS has no Python; a tested port is needed.

### 0.3 PurchaseManager exit (every game)

- Server flow: `DistributedMinigameAI.enterFrameworkCleanup` (`:293-330`) builds `scoreList` from `scoreDict` times `getScoreMult` (`MinigameGlobals.py:32-35`, 1.0 to 1.5 by safezone index) and clamps 0-255 (`:293-327`). Holiday multiplier 2 applies if active. On abort, the score is a random value in `[DEFAULT_POINTS, MAX_POINTS]`. It then creates the PurchaseManagerAI (regular or metagame branch, `:330-393`).
- The PM is `DC` `dclass PurchaseManager` (`toon.dc:1473-1486`). The client sends `requestExit()`, `requestPlayAgain()`, and `setInventory(blob, int16, uint8)` (all clsend). The AI sends `setPlayerIds`, `setMinigamePoints`, `setPlayerMoney`, `setPlayerStates`, `setCountdown(int16)`, `setMetagameRound`, `setVotesArray`, and `setPurchaseExit()` (broadcast).
- Client: `shtiker/PurchaseManager.py`. `setPurchaseExit` (`:96`) and `d_requestExit` (`:82`) handle the leave path.
- Cross-play rule: the PM is per-client. Both clients must send `requestExit`, `requestPlayAgain`, and `setInventory` with the upstream argument types and the inventory blob format (`shop.md` covers the blob). `setInventory` is validated by the AI (`PurchaseManagerAI.py:208-231`).
- Joining a game in progress: none. A late toon is refused by the AI barriers.

---

## 1. Race (id 1) - server-authoritative (protocol only)

- **Client sends**: `setAvatarChoice(uint8)` airecv clsend (`DC` `DistributedRaceGame`). Sent once per turn, from the dice button (`DistributedRaceGame.py:457,461`), or `0` on timeout (`handleChoiceTimeout`, `:457`).
- **AI sends**: `setTimerStartTime(int16)` = `getFrameNetworkTime()` (`DistributedRaceGameAI.py:117`); `setAvatarChose(uint32)` (`:100`); `setChancePositions(uint8[])` (`:94`); `setServerChoices(int8[], uint8[], int8[])` (`:198`, `:262`).
- **Client does**: `setTimerStartTime` starts the countdown (`DistributedRaceGame.py:437-443`, `networkToLocalTime`). `setServerChoices` (`:874`) applies the authoritative moves to all pieces. `setChancePositions` (`:861`) places chance squares.
- **Client-computed values others see**: none. The move is the AI's `choice`, validated by `checkChoice` (`DistributedRaceGameAI.py:71-76`: invalid becomes `ValidChoices[0]`). `RaceGameGlobals` has the valid set.
- **Timing**: `InputTimeout` (`RaceGameGlobals`) runs on the AI. The client timer must start from the AI timestamp.
- **Class**: server-authoritative (protocol only).

## 2. Cannon (id 2) - shared simulation

- **Client sends**:
  - `setCannonPosition(int32/100, uint32/100)` clsend. Sent by the local toon every `CANNON_MOVE_UPDATE_FREQ` = 0.5 s while moving (`DistributedCannonGame.py:660-671`).
  - `setCannonLit(int32/100, uint32/100)` clsend on fire (`:458`).
  - `setToonWillLandInWater(int32/100)` clsend, only when the local flight hits water (`:777`).
- **AI sends**: `updateCannonPosition(uint32, int32/100, uint32/100)` (`DistributedCannonGameAI.py:83`); `setCannonWillFire(uint32, int32/100, int32/100, uint32/100)` with `fireTime = getCurrentGameTime() + FUSE_TIME` (`:94`, `CannonGameGlobals.FUSE_TIME = 0.0`); `announceToonWillLandInWater(uint32, int32/100)` (`:112`).
- **Client does**: `updateCannonPosition` moves other cannons (`DistributedCannonGame.py:398-407`). `setCannonWillFire` stores angles and launches a local flight (`:409-421`, then `__fireCannonTask` with `launchTime = fireTime`, `:728-732`).
- **Client-computed values others see**: the flight (`__calcFlightResults`, `:703-726`) is computed on every client from the toon's cannon hpr. Start velocity: `horizVel = 94 cos(angle)`, `xVel = -horizVel sin(rot)`, `yVel = horizVel cos(rot)`, `zVel = 94 sin(angle)`, angles in radians from degrees (`INITIAL_VELOCITY = 94.0`, `:45`). Gravity is `-32` (`Trajectory.py:7`, `gravity = 32.0`, applied as `zAcc = -32`). Impact is `Trajectory.calcTimeOfImpactOnPlane` and `checkCollisionWithDisc` (`Trajectory.py:43-95`). Tower and water-bucket discs use `TOWER_RADIUS = 10.5`, `TOWER_HEIGHT = 43.85`, `BUCKET_HEIGHT = 36` (`DistributedCannonGame.py:40-42`).
- **Score**: the AI takes the reporter's `landTime` and computes `calcScore(t) = int(23 - 18 * t / 90 + 0.5)` (`CannonGameGlobals.py:1-17`; `MAX_SCORE 23`, `MIN_SCORE 5`, `GameTime 90`). It assigns that score to every avatar (`DistributedCannonGameAI.py:99-111`). **The AI trusts the client-computed landing time**, so a 3DS that computes it differently gets a different score.
- **Timing**: `launchTime` is in game-time. The AI's `gameTimer` and `toonLandedInWater` use `getCurrentGameTime()`.
- **Class**: shared simulation (needs identical math: angle to velocity, gravity 32, disc intersection, and the score formula).

## 3. Tag (id 3) - server-authoritative (protocol only), client collision trigger

- **Client sends**: `tag(uint32)` airecv clsend (`DC` `DistributedTagGame`). Sent by the IT toon when its collision sphere enters another toon's (`DistributedTagGame.py:270-278`, `sendTagIfIt`).
- **AI sends**: `setIt(uint32)` broadcast (`DistributedTagGameAI.py:124-131`); `setTreasureScore(uint16[])` (`:101`).
- **Client does**: `setIt` changes the IT toon (`DistributedTagGame.py:~250`); `setTreasureScore` updates the score panel.
- **Trust**: the AI accepts a tag only if the sender is the current IT and a 2 s `tagBack` cooldown has passed (`DistributedTagGameAI.py:107-122`). **No distance check is done on the AI**, so the collision that triggers `tag` is trusted.
- **Timing**: the 2 s cooldown is AI-side. The match timer is local (`DistributedTagGame.py:157-160`) and ends on `timerExpired`, while the AI runs its own `timerExpired` (`DistributedTagGameAI.py:70`).
- **Treasures**: `treasureGrabCallback` (`:90-101`) is driven by `DistributedTagTreasure`, which has its own DC class (not checked here).
- **Class**: server-authoritative (protocol only) for the tag and score. The collision volume is client-side; match its radius to upstream (`TagGameGlobals.py`), though the AI does not check it.

## 4. Pattern (id 4) - server-authoritative, with client-timed results

- **Client sends**:
  - `reportPlayerReady()` clsend (`DistributedPatternGame.py:579`).
  - `reportButtonPress(uint8, uint8)` clsend (`:718`).
  - `reportPlayerPattern(uint8[], uint16/1000)` clsend (`:727`). `totalTime = base.clock.getFrameTime() - currentStartTime` (`:726`). This is a **local frame clock**, not a network time.
- **AI sends**: `setPattern(uint8[])` (`DistributedPatternGameAI.py:103`); `remoteButtonPressed(uint32, uint8, uint8)` (`:129`); `setPlayerPatterns(uint8[]x4, uint32 fastestAvId)` (`:167`).
- **AI logic**: `__acceptPlayerPattern` (`:146-158`) takes the smallest `totalTime` whose pattern matches the target as fastest, and adds 2 points. Each correct round adds `self.round` (`:168-175`). For a solo game, `fastestAvId` is set to literal `1` (`:153-154`), which is a quirk.
- **Client does**: runs the dance via `getDanceStepDuration` (`:424`). Other clients show `remoteButtonPressed` and `setPlayerPatterns`.
- **Trust**: the AI trusts the client's `totalTime` for the fastest pick. Clients must measure time the same way (the 3DS must use the same frame-clock delta).
- **Class**: server-authoritative (protocol only), with a client-measured time input that must come from a local monotonic clock. The dance step duration itself is shared math (`getDanceStepDuration`, `:424`); unverified beyond that.

## 5. Ring (id 5) - shared simulation (ring positions), trusted catch result

- **Client sends**: `setToonGotRing(uint8)` airecv clsend (`DistributedRingGame.py:804`). Sent per ring group per local toon: `gotRing = 1` if the toon passed through it, 0 if missed.
- **AI sends**: `setTimeBase(int16)` broadcast ram required (`DC` `DistributedRingGame`), `setColorIndices(int8,int8,int8,int8)` broadcast ram required, `setRingGroupResults(uint8)` broadcast (`RingGameAI.py:125`, bitfield of misses).
- **AI logic**: `setToonGotRing` (`RingGameAI.py:99-130`) trusts the bit. Successes add 1. If nobody missed a group, +0.5 to everyone. Final group awards perfect bonuses (`:126-130`).
- **Client does**: ring positions are computed locally from `t = globalClock.getFrameTime() - timeBase` (`DistributedRingGame.py:705,720`), with `timeBase = networkToLocalTime(setTimeBase)` (`:816-819`). Clamps toon XZ to `MAX_TOONXZ = 10` (`:586-599`). Collision radius `CollisionRadius = 1.5` (`RingGameGlobals.py:7`), ring radius `MAX_TOONXZ/3 * 0.9` (`:9`).
- **Client-computed values others see**: the ring track (`RingTrack`, `RingGroup`) is computed on each client from `timeBase` and the color indices. Its positions must match to make the same ring-hit call.
- **Class**: shared simulation for ring motion (identical math and time base). The hit result is client-decided and AI-trusted.

## 6. Maze (id 6) - shared simulation (suits), protocol for treasures

- **Client sends**:
  - `claimTreasure(uint32)` airecv clsend (`DistributedMazeGame.py:743`).
  - `hitBySuit(uint32, int16)` clsend broadcast (`:762`). Timestamp is `localToNetworkTime(getFrameTime())`.
- **AI sends**: `setTreasureGrabbed(uint32, uint32)` broadcast (`DistributedMazeGameAI.py:88`); `allTreasuresTaken()` (`:93`).
- **AI logic**: `claimTreasure` (`:71-96`) takes the first claim per treasure, bounds-checks, and adds 1. No position check. `timerExpired` (`:98`) ends the game (`GAME_DURATION = 60`, `MazeGameGlobals.py:12`).
- **Client does**:
  - `hitBySuit` from others is shown only in `play` or `showScores` (`DistributedMazeGame.py:765-773`).
  - Start positions use `self.randomNumGen.shuffle(self.startPosHTable)` (`:554`). Toon RNG child streams (`:568-570`). Suits: `MazeSuit(i, maze, self.randomNumGen, suitPeriods[i], difficulty)` with `suitPeriods` shuffled (`:1009-1011`).
- **Client-computed values others see**: suit positions are computed on every client from the shared RNG and local time. `MazeSuit.py:231` uses `curT = globalClock.getFrameTime() - startTime`, and `SUIT_TIC_FREQ = 256` (`MazeGameGlobals.py:14`) controls the tick. **If one client's draws happen at a different local time, its suits diverge.** Moves are driven by the RNG at each tick (`MazeSuit.py:33-39`, `walkSameDirectionProb`, `walkTurnAroundProb`).
- **Class**: shared simulation (suit walks depend on the RNG algorithm, draw order, tick, and local timing), plus protocol for treasures and hits.

## 7. TugOfWar (id 7) - shared formula (AI trusts the client force)

- **Client sends** (DC `DistributedTugOfWarGame`):
  - `reportPlayerReady(uint8)` clsend (`DistributedTugOfWarGame.py:789`), with side 0 or 1.
  - `sendNewAvIdList(uint32[])` airecv clsend (`:649`).
  - `reportCurrentKeyRate(uint32, int16/100)` airecv clsend, sent every `SEND_UPDATE = 0.2` s (`TugOfWarGameGlobals.py:5`, `:876`, the task at `:884`).
  - `reportEndOfContest(uint8)` airecv clsend (`:1160, :1172`).
- **AI sends**: `sendGameType(uint8, uint8)` (`DistributedTugOfWarGameAI.py:81`); `sendGoSignal(uint8[])` (`:159`); `sendStopSignal(uint32[], uint32[], uint32[])` (`:120, :320`); `remoteKeyRateUpdate(uint32, uint32)` (`:219`); `sendCurrentPosition(uint32[], int16/1000[])` (`:224`); `sendSuitPosition(int32/1000)` (`:226`).
- **AI logic**: `reportCurrentKeyRate` stores the client's `keyRate` and `force` (`:212-226`). Once all players reported, `calculateOffsets` (`:188-211`) sums the client forces per side (plus `curSuitForce` in cog mode), computes `deltaX = (f1 - f0) * kMovement` (`kMovement` 0.02 or 0.04, `:32, :77-80`), and updates the offsets. **The AI trusts the client-computed force.**
- **Client formula** (`DistributedTugOfWarGame.py:1204-1212`):
  - `keyRate` = number of key presses still alive in `keyTTL`. Each press inserts 1.0 (`:840`). Every 0.1 s tick subtracts 0.1 and drops expired entries (`:852-862`).
  - `force = advantage * (rateMatchAward + 4 + 0.4 * idealRate) * exp(-(keyRate - idealRate)^2 / (2 * (0.25 * idealRate)^2))`.
  - In `allOutMode`, `force = 0.75 * keyRate`.
  - `idealRate` starts at `2.0` (`:91`) and follows `targetRateList` (`:897-907`). `advantage` is `1.0` (`:79`) or `2.0` (`:1074, :1080`). `rateMatchAward` +0.3 per tick in range (`:863-866`).
- **Timing**: the key-rate tick is `0.1` s (`:889-891`). The client-report period is `SEND_UPDATE` (0.2 s).
- **Class**: shared simulation for the force formula and tick math. The AI trusts the reported value, so the formula must match.

## 8. Catch (id 8) - shared schedule (drop order), trusted claims

- **Client sends**: `claimCatch(uint32, uint32)` airecv clsend (`DistributedCatchGame.py:540`, `(objNum, objTypeId)`). `hitBySuit(uint32, int16)` clsend broadcast (`:796`). `reportDone()` airecv clsend (`:608`).
- **AI sends**: `setObjectCaught(uint32, uint32)` broadcast (`DistributedCatchGameAI.py:102`); `setEveryoneDone()` (`:73`).
- **AI logic**: `claimCatch` (`:88-108`): first claim per `objNum` wins, `objNum` bounded by `2 * len(caughtList)` and 5000. Good objects add 1 (`CatchGameGlobals`, `Name2DropObjectType[...].good`). No position check. `objTypeId` indexes `DOTypeId2Name`, which is `list(Name2DropObjectType.keys())` (`CatchGameGlobals.py:23-33`) and therefore follows the order of `DropObjectTypes` (`:16-22`).
- **Client does**: builds the drop schedule locally. `droppedObjNames = [fruit]*numFruits + ['anvil']*numAnvils`, then `randomNumGen.shuffle` (`DistributedCatchGame.py:583`), then `DropPlacer` fills `dropSchedule` (`:584-586`). Per drop, the watermelon and anvil angles use `randomNumGen.random()` with `absDelta` 12 / 15 (`:672-675`). Other fruits take `random() * 360`. Toon child RNG at `:736`.
- **Class**: shared simulation for the schedule (`objNum` mapping, object order, RNG draw order, `DropPlacer`/`DropScheduler`), plus trusted claims.

## 9. Diving (id 9) - mixed: server-authoritative scoring, shared crab motion

- **Client sends** (DC `DistributedDivingGame`):
  - `pickupTreasure(uint32)` airecv clsend (`DistributedDivingGame.py:658`).
  - `handleFishCollision(uint32, uint32, uint32, char[0-256])` airecv clsend (`:172`): `status` is a string, e.g. `'normal'` or `'treasure'`.
  - `handleCrabCollision(uint32, char[0-256])` airecv clsend (`:167`).
  - `getCrabMoving(uint32, int16, int8)` airecv clsend (`:809`): `(crabId, crab.getX(), crab.direction)`. Sent each time a crab's 1 s `Wait` finishes (`__crabTask`, `:795-811`).
  - `removeFish(uint32)`, `treasureRecovered()` airecv clsend (`:650`).
- **AI sends**: `setTreasureGrabbed`, `setTreasureDropped(uint32, int16)`, `performCrabCollision`, `performFishCollision`, `fishSpawn(int16, uint32, uint32, uint16)`, `setCrabMoving(uint32, int16, int8, int8, int16, int8)`, `incrementScore(uint32, uint32, int16)`.
- **AI logic**:
  - `getCrabMoving` (`DistributedDivingGameAI.py:289-299`): timestamp and two random values `rand1`, `rand2` in `[0, 9]` come from Python `random` on the AI and are echoed.
  - `treasureRecovered` (`:300-316`): `newSpot = int(random()*30)`; score changes. `spawnFish` (`:394-410`): AI random picks the spawn type and offset.
  - `handleCrabCollision` / `handleFishCollision` (`:410-440`): the AI trusts the client's `status`.
- **Client-computed values others see**:
  - Crab goal (`DistributedDivingGame.py:814-836`): `x` = average X of treasure nodes (**client-local positions**), `goalX = int(x + dir * (rand1/10) * 12 + 4)`, `goalZ = -35 + 5*(rand2/10)`, `xTime = 1 + rand1/10*2`, `zTime = 0.5 + rand2/10`, `wait = rand1/10 + rand2/10 + 1`. Clamped to ±20 with a quirk: when `goalX < -20`, it becomes `20` (`:833-836`). Port this exactly.
  - Fish: `fishSpawn` uses `localElapsedTime(timestamp)` and dedupes within 150 ms (`DivingFishSpawn.py:177-187`).
- **Timing**: `getFrameNetworkTime()` on the AI; `localElapsedTime` on the client (`DistributedDivingGame.py:821`).
- **Class**: mixed. Scoring and collisions are protocol only. Crab and fish motion are shared math, and the crab goal uses client-local treasure positions, so a divergence there changes what each client shows.

## 10. Target (id 10) - shared simulation (target layout), trusted position

- **Client sends**:
  - `setScore(int32, int32)` airecv clsend (`DistributedTargetGame.py:978`). The client passes **float** `getX()` and `getY()`, but the DC type is `int32`. See the unclear list.
  - `setPlayerDone()` airecv clsend (`:1045`).
  - `setToonGotRing(uint8)` airecv clsend (DC present; the client path was not traced).
- **AI sends**: `setTrolleyZone(uint32)` (`DistributedTargetGameAI.py:53`); `setTargetSeed(uint32)` broadcast ram (`:58`); `setSingleScore(uint16, uint32)` (`:126`); `setRoundDone()` (`:186`); `setGameDone()` (`:225`).
- **AI logic**: `setScore` (`:103-127`) compares the client's position to every placed target (`dx, dy`, distance < `targetSize[type]`). It takes the highest `targetValue`, and sends `setSingleScore` only if the new value is higher. **The AI trusts the client position.**
- **Seed and layout**: AI sets `seed = int(random.random()*4000)` after `random.seed(time.time())` (`:56-59`). Clients seed `random.seed(targetSeed)` then `setupTargets()` (`DistributedTargetGame.py:406-412`). Both sides then run the same loop: `placeX = random()*(fieldWidth*0.6) - fieldWidth*0.6*0.5`, `placeY = (random()*0.6 + 0.4*placeValue/highestValue) * fieldLength`, with `checkPlace` rejection (`DistributedTargetGameAI.py:79-95`; `DistributedTargetGame.py:440-460`). `fieldWidth = ENVIRON_WIDTH*3`, `fieldLength = ENVIRON_LENGTH*3.7` (`TargetGameGlobals.py:19-20`).
- **Class**: shared simulation for target placement (MT19937, draw order, `checkPlace`), plus a trusted client position for scoring.

## 11. Pairing (id 11) - server-authoritative, shared deck shuffle

- **Client sends**: `openCardRequest(int16, int16)` airecv clsend (`DistributedPairingGame.py:377`; `(deckOrderIndex, bonusGlowCard)`). `reportDone()` airecv clsend (`:353`, `:396`). `setSignaling(uint32)` clsend broadcast (`:481`).
- **AI sends**: `setDeckSeed(uint32)` broadcast ram required (`DistributedPairingGameAI.py:25`, `randint(0, 4000000)`); `setMaxOpenCards(uint8)` (`:48`); `openCardResult(int16, uint32, int16, int8, int16[])` (`:218`); `setEveryoneDone()` (`:110`).
- **AI logic**: `openCardRequest` (`:178-223`): checks the card is not face-up, bounds the index, and manages the face-up list. `bonusGlowCard` is **client-supplied**: if it is in the matched pair, +1 point (`:202-208`). The client picks it; the upstream value is the bonus glow card (`DistributedPairingGame.py:22` `bonusGlowTime`, and the glow logic near `:105-150`, not fully traced).
- **Client does**: `setDeckSeed` (`:398-401`). The deck is built with `PlayingCardDeck.shuffleWithSeed(seed)` (`PlayingCardDeck.py:14-18`, `random.Random().seed(seed)` then `shuffle`), using `PairingGameGlobals.py:13`. The AI uses the same function, so index-to-card mapping must match.
- **Class**: server-authoritative (protocol only) for matches and points, with a shared deck shuffle that must match the MT19937 `shuffle` exactly.

## 12. Vine (id 12) - client-reported state, AI range checks only

- **Client sends** (DC `DistributedVineGame`):
  - `reachedEndVine(int8)` clsend airecv (`DistributedVineGame.py` callers; AI handler `DistributedVineGameAI.py:157-165` returns immediately, so it is inert).
  - `setNewVine(uint32, int8, uint32/10000, int8)` airecv clsend broadcast (`:1105`).
  - `setNewVineT(uint32, uint32/10000, int8)` clsend broadcast (`:1125`).
  - `setJumpingFromVine(uint32, int8, int8, int32/100, int16/100, int16/100, int16)` clsend broadcast (`:1137`).
  - `setFallingFromVine(uint32, int8, int8, int32/100, int16/100, int16/100, int16, int8)` clsend broadcast (`:1153`).
  - `setFallingFromMidair(uint32, int8, int32/100, int16/100, int16/100, int16, int8)` clsend broadcast (`:1170`).
  - `claimTreasure(uint32)` airecv clsend (`:1190`).
  - `d_setFallingPos` (`:1181-1182`) sends `setFallingPos`, which is **not in the DC** (see unclear list).
- **AI sends**: `setTreasureGrabbed(uint32, uint32)` (`DistributedVineGameAI.py:134`); `allAtEndVine()` (`:194`); `setScore(uint32, uint32)` (`:216, :320`); `setVineSections(uint8[])` required broadcast ram (DC).
- **AI logic**: `updateToonInfo` (`:218-270`) only range-checks positions (`x` in [-1000, 2000], `z` in [-100, 1000], velocities in ±1000). It does not check the vine geometry. `setNewVine` (`:167-183`) and `checkForEndVine` (`:197`) use the client's `vineIndex`. **The AI trusts the reported vine index and position.**
- **Client does**: swing and fall physics are local. Other toons are drawn from the reported state, so their motion depends on what each client sends.
- **Class**: server trusts client-reported state (protocol plus range checks). The local swing and jump math was not traced, so the shared-math status is unclear.

## 13. CogThief (id 14) - AI-driven cogs, client reports hits

- **Client sends** (DC `DistributedCogThiefGame`):
  - `throwingPie(uint32, int32, int32/100, int32/100, int32/100, int32/100)` clsend broadcast (`DistributedCogThiefGame.py:638`).
  - `hitBySuit(uint32, int32, int8, int32/100, int32/100, int32/100)` clsend broadcast airecv (`:389`).
  - `pieHitSuit(...)` same signature, clsend broadcast airecv (`:732`).
  - `cogHitBarrel(int32, int8, int8, int32/100, int32/100, int32/100)` clsend airecv (`:583`).
  - `cogAtReturnPos(int32, int8, int8)` clsend airecv (`:758`).
- **AI sends**: `updateSuitGoal(int32, int32, int8, int8, int64, int32/100, int32/100, int32/100)` (`DistributedCogThiefGameAI.py:182, 212, 249, 302`); `makeCogCarryBarrel(...)` (`:123`); `makeCogDropBarrel(...)` (`:148`); `markBarrelStolen(int32, int32, int8)` (`:358`).
- **AI logic**: `hitBySuit` / `pieHitSuit` (`:191-205, :311-325`) reset the cog to its start position and wait `ExplodeWaitTime` before choosing a new goal. `cogHitBarrel` (`:261-278`) checks goal and carrying state, then computes a return point (`chooseReturnPos`, `:279`). `cogAtReturnPos` (`:369`). **The AI trusts the reported positions** for barrel drops.
- **Client does**: cog movement follows the AI's `updateSuitGoal` with `timestamp`/`clientStamp`, so clients must interpolate from those values. `randomNumGen.choice(suitTypes)` (`DistributedCogThiefGame.py:367`) runs on the client as well (AI-side it chooses suit types too; unverified which side draws).
- **Class**: mostly server-authoritative (protocol only, AI drives cog goals). Clients need the same timestamp handling as the AI. The cog walk math, if the client runs it, is unverified.

## 14. TwoD (id 15) - shared simulation (platform timing), trusted events

- **Client sends** (DC `DistributedTwoDGame`):
  - `showShootGun(uint32, int16)` clsend broadcast (`DistributedTwoDGame.py:379`).
  - `toonHitByEnemy`, `toonFellDown`, `toonSquished`, `toonVictory` (uint32, int16) clsend broadcast airecv (`:392, :407, :422, :437`).
  - `claimTreasure(uint8, uint8)` airecv clsend (`:457`).
  - `claimEnemyShot(uint8, uint8)` airecv clsend (`:482`).
  - `reportDone()` airecv clsend (`:517`).
- **AI sends**: `setSectionsSelected(twoDSectionInfo[])` required broadcast ram (`DistributedTwoDGame.py:49`); `setTreasureGrabbed(uint32, uint8, uint8)` (`DistributedTwoDGameAI.py:190`); `setEnemyShot(uint32, uint8, uint8, uint32)` (`:205`); `addVictoryScore(uint32, uint8)` (`:239`); `setEveryoneDone()` (`:147`).
- **AI logic**: `claimTreasure` (`:169-190`) and `claimEnemyShot` (`:192-209`) trust the indices, check bounds and whether taken. `toonVictory` (`:218-241`) computes a bonus from `getCurrentGameTime()`, `GameDuration`, `BaseBonusOnCompletion`, `BonusPerSecondLeft` (`ToonBlitzGlobals`).
- **Client does**: the section layout is server-driven (`setSectionsSelected`). Enemies, stompers, and platform timing are local (`TwoDEnemy.py`, `TwoDStomper.py`, `TwoDSection*.py`); the `TwoDTreasureMgr.py` `random.choice` calls are in the client module list (see unclear list).
- **Class**: shared simulation for enemy and stomper motion (time-based), plus trusted client events. Confirm the enemy and stomper timing math before claiming parity.

## 15. Photo (id 16) - client-judged scores, shared assignments

- **Client sends** (DC `DistributedPhotoGame`):
  - `newClientPhotoScore(uint8, char[0-256], uint32/100)` airecv clsend (`DistributedPhotoGame.py:528`): `(subjectIndex, pose, score)`.
  - `filmOut()` airecv clsend (`:546`).
- **AI sends**: `newAIPhotoScore(uint32, uint8, uint32/100)` broadcast (`DistributedPhotoGameAI.py:149`).
- **AI logic**: `newClientPhotoScore` (`:116-158`): if `score > NUMSTARS (5)`, score becomes 0 (`PhotoGameGlobals.py:7`). It matches the assignment by `(subjectIndex, pose)`, keeps the best score per player, and sends `newAIPhotoScore`. `calculateScores` (`:159`) computes bonuses. **The AI trusts the client's `score` (within 0-5).**
- **Client-computed score** (`DistributedPhotoGame.py:568-600`, `judgePhoto`): from camera heading vs subject heading (`angle`), `centering` values, a `quality` term, `tooClose` and `portrait`, and `interest` from the subject's track state. Facing: `angle >= 135` adds 2, `>= 90` adds 1, `<= 60` subtracts 1. This must match upstream for the same photo.
- **Shared assignments**: `PhotoGameBase.generateAssignmentTemplates` (`PhotoGameBase.py:12-24`) runs `random.seed(self.doId)` and `random.choice` on every client. The MT19937 path (section 0.2) must match, or clients show different assignments.
- **Class**: shared simulation for the assignment list (MT19937 with the doId seed), and client-judged scoring that the AI accepts up to the cap.

## 16. Ice (id 13) - shared simulation (ODE physics), averaged by the AI

- **Client sends** (DC `DistributedIceGame`):
  - `setForceArrowInfo(uint32, int32/100, int32/100)` broadcast clsend (`DistributedIceGame.py:908`; rate limited by `d_setForceArrowInfo`, `:900-910`).
  - `setAvatarChoice(int32/100, int32/100)` airecv clsend (`:636`: `curForce, curHeading`; timeout sends `(0, 0)` at `:656`).
  - `endingPositions(Coord3[])` airecv clsend (`:826`).
  - `reportScoringMovieDone()` airecv clsend (`:469`).
  - `claimTreasure(uint8)` airecv clsend (`:1009`); `claimPenalty(uint8)` (`:1023`).
- **AI sends**: `setTireInputs(TireInput[])` broadcast (`DistributedIceGameAI.py:108`); `setTimerStartTime(int16)` (`:94`); `setFinalPositions(Coord3[])` (`:148`); `setMatchAndRound(int8, int8)` (`:91, :156`); `setScores(int8, int8, int16[])` (`:202`); `setNewState(string)` (`:92, :203, :230`); `setTreasureGrabbed(uint32, uint32)` (`:324`); `setPenaltyGrabbed(uint32, uint32)` (`:345`).
- **AI logic**:
  - `setAvatarChoice` clamps force to `[0, 100]` (`checkChoice`, `:264-271`).
  - `enterProcessEndingPositions` (`~:140-150`) averages each tire's position over the clients that reported. `FarthestLength` and points are computed from `pos.length()` in `enterScoreMatch` (`~:165-200`). Scores use `IceGameGlobals.PointsDeadCenter`, `PointsInCorner`, `BonusPointsForPlace`, and `FarthestLength` (`IceGameGlobals.py`).
- **Client physics**:
  - `DistributedIceWorld.setupSimulation` (`DistributedIceWorld.py:45-88`): gravity `(0, 0, -32.174)`, auto-disable with `setAutoDisableSteps(10)`, surface table, plane walls at `MinWall/MaxWall` (`IceGameGlobals.py`), `TireRadius = 1.5`.
  - `MinigamePhysicsWorldBase.py:38-39`: `FPS = 60.0`, `DTAStep = 1/60`. `__simulationTask` (`:110-122`) runs whole steps from accumulated time (`while DTA >= DTAStep`).
  - `enterMoveTires` (`DistributedIceGame.py:373-401`): force = `dirVector * (force / MaxLocalForce) * MaxPhysicsForce`, `MaxPhysicsForce = 25000` (`:19`), where `degs = heading + 90`.
  - `sendTirePositions` (`:812-826`): fires when all tire bodies are disabled (`__moveTiresTask`, `:805-811`), sends one `[x,y,z]` per avatar, then fills dummy slots.
- **Class**: shared simulation (needs identical ODE-equivalent physics: fixed 1/60 step, same gravity, friction and restitution surface table, and the same stop rule). The AI averages the reported end positions, so a different result on one client changes the score. Exact ODE parity on the 3DS is a large open question; it should be a dedicated spike.

---

## 17. For each client

- [ ] Use the DC field names, order, and types from `toon.dc` (hash 547155168). Never hand-edit IDs; use `dc_generated.h`.
- [ ] Send only clsend fields, or ownsend on the local toon. Send `setAvatarJoined` once, `setAvatarReady` after the rules panel, and `setAvatarExited` on leave.
- [ ] Convert every AI timestamp with `globalClockDelta.networkToLocalTime` before use. Send network timestamps with `localToNetworkTime`.
- [ ] Seed the shared RNGs from the object doId (`RandomNumGen(doId)`) and from the AI seed (Target, Pairing). Use the same algorithm and draw order as upstream.
- [ ] Use upstream constants from each `*GameGlobals.py` (speed, gravity 32, `INITIAL_VELOCITY 94`, ODE step 1/60, tug `kMovement`, catch drop tables, diving crab math).
- [ ] Compute client-reported values exactly as upstream: Cannon `landTime`, Tug `keyRate` and `force`, Ring `gotRing`, Photo `score`, Ice end positions, Pattern `totalTime` (from a monotonic local frame clock).
- [ ] Handle `setGameAbort` and `setGameExit` the way the upstream framework does, and send `requestExit` only on the user's leave action.
- [ ] Keep the PurchaseManager flow: `requestExit`, `requestPlayAgain`, `setInventory` with the upstream types.
- [ ] Bound all pools and lists from AI data (`setSectionsSelected`, `setVineSections`, `setTireInputs`, `setPlayerPatterns`), and reject out-of-range values rather than crashing.

## 18. Testing cross-play

Run one 3DS and one web client against the same server (the 3DS through `tools/server/`, the web client through `npm run bridge`). Use `AI_MINIGAME_ID=<id> tools/server/start.sh` to force a game, and check that the score and state match on both sides. Suggested two-client test per game:

| Game | Check |
|---|---|
| Race | Both clients show the same board after each `setServerChoices`. |
| Cannon | Same flight path and impact time for the other toon's shot; same final score. |
| Tag | Tag accepted once per 2 s; `setIt` moves IT on both. |
| Pattern | Same target pattern; fastest pick matches the faster client. |
| Ring | Ring groups in the same place on both; same `setRingGroupResults`. |
| Maze | Suits in the same place at the same tick; `claimTreasure` first-come. |
| TugOfWar | Same rope position after `sendCurrentPosition`; same end result. |
| Catch | Same drop order; `objNum` claims accepted once. |
| Diving | Crab goals match `setCrabMoving`; scores match `incrementScore`. |
| Target | Same target layout (compare a dump of `targetsPlaced`); same score on a fixed position. |
| Pairing | Same deck order (compare the first 8 card faces); points match. |
| Vine | Other toon swings from the reported state; end-vine detection matches. |
| CogThief | Cog goals and barrel drops match `updateSuitGoal` timestamps. |
| TwoD | Same enemy and stomper timing; `toonVictory` bonus matches. |
| Photo | Same five assignments; judged score matches for the same photo. |
| Ice | Same final tire positions for the same inputs (dump positions after `sendTirePositions`). |

Each run must log the numbers on both sides (`docs/test-log.md` format).

---

## 19. Unclear or inconsistent items (from the upstream code)

1. **Panda `RandomNumGen` algorithm** is not in `ref/`. Maze, Catch, CogThief, Tug, and Pairing child streams depend on it. Verify against the Panda3D source before porting.
2. **Target `setScore(int32, int32)`**: the client passes float coordinates (`DistributedTargetGame.py:978`). How Panda packs floats into `int32` (truncate or round) was not verified, and it determines the AI's distance math.
3. **Vine `setFallingPos`** (`DistributedVineGame.py:1181-1182`) is sent, but no `setFallingPos` exists in `toon.dc`, so the send path is likely dead. The AI-side handler was not found.
4. **Vine `reachedEndVine`** AI handler returns before its body (`DistributedVineGameAI.py:157-158`), so it is inert.
5. **Diving `setCrabMoving`** clamps `goalX < -20` to `20` (`DistributedDivingGame.py:833-836`). This may be a typo upstream. Port it as written and flag it.
6. **Pattern solo** sets `fastestAvId = 1` (`DistributedPatternGameAI.py:153-154`), a literal, not an avId.
7. **Ring, Vine, Tag, and TwoD local physics** (swing, collision volumes, enemy and stomper timing) were not traced line by line. Shared-math status for those parts is unconfirmed.
8. **TwoD `TwoDTreasureMgr.py`** calls `random.choice` (`:293-403`). It is imported by the client (`DistributedTwoDGame.py`). Whether it runs on the AI or the client, and which RNG it uses, was not confirmed.
9. **CogThief**: which side draws `random.choice(suitTypes)` (`DistributedCogThiefGame.py:367`) was not confirmed. The AI also chooses suits.
10. **Pairing bonus glow**: the client supplies `bonusGlowCard`, and the AI trusts it for +1 point. The exact client rule was not fully traced.
11. **Ice ODE parity**: the step count depends on the frame-time accumulator (`MinigamePhysicsWorldBase.py:110-122`) until all tires sleep. Exact equality with ODE across clients is not guaranteed even upstream, so the tolerance must be measured.
