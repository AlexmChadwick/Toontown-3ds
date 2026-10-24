#!/usr/bin/env python3
"""Generate source/net/dc_generated.h from the upstream Open Toontown .dc files.

The 3DS client does not parse .dc files at runtime. Instead this tool uses stock
Panda3D's DC parser (the same one the stock client uses, so the hash matches) to
emit class/field numbers, the DC hash sent in CLIENT_HELLO, and (Phase 3) a
compact field descriptor table so the C++ side can parse or skip any field of the
classes it receives.

    python -I tools/dcgen/dcgen.py ref/open-toontown/etc > source/net/dc_generated.h
    python -I tools/dcgen/dcgen.py ref/open-toontown/etc --vectors out.bin   # test vectors

Order matters: otp.dc must be read before toon.dc (typedefs live in otp.dc).

Type program encoding (one byte opcode, little-endian u16 arguments inline):
    I8 I16 I32 I64 U8 U16 U32 U64 F64 CHAR      fixed-size scalars
    STRING / BLOB                               u16 length + bytes
    BLOB32                                      u32 length + bytes
    FIXBYTES n:u16                              n raw bytes (string(5), struct of
                                                fixed size inside arrays, ...)
    VARARRAY sublen:u16 <sub-program>           u16 byte length + bytes; the
                                                sub-program describes one element
    FIXARRAY count:u16 sublen:u16 <sub-program> count elements, no prefix
Structs are flattened in place. Molecular fields are the concatenation of their
atomics. Required-field lists are taken from DCClass inherited-field order with
exactly the filters Panda's receiveUpdateBroadcastRequired(Owner) use.
"""
import os
import struct
import sys

from panda3d.direct import DCFile

# (class, field) pairs the client code refers to by name. Extend as phases add
# features. Each gets k_<field> (number), kw_<field> (keyword mask) and
# sig_<field> (type signature, so C++ can static_assert the layout it hand-parses).
WANTED = {
    'AstronLoginManager': ['requestLogin', 'loginResponse', 'requestAvatarList',
                           'avatarListResponse', 'createAvatar', 'createAvatarResponse',
                           'requestPlayAvatar'],
    'DistributedDistrict': ['setName', 'setAvailable'],
    'ToontownDistrict': ['setName', 'setAvailable'],
    'EstateManager': ['getEstateZone', 'setEstateZone', 'setAvHouseId', 'exitEstate'],
    'DistributedEstate': ['setEstateReady', 'setClientReady', 'setEstateType', 'setClosestHouse',
                          'setDecorData', 'setSlot0ToonId', 'setSlot1ToonId', 'setSlot2ToonId',
                          'setSlot3ToonId', 'setSlot4ToonId', 'setSlot5ToonId', 'setIdList'],
    'DistributedHouse': ['setHousePos', 'setHouseType', 'setGardenPos', 'setAvatarId', 'setName',
                         'setColor', 'setInteriorItems', 'setHouseReady'],
    'DistributedHouseInterior': ['setHouseId', 'setHouseIndex', 'setWallpaper', 'setWindows'],
    'DistributedHouseDoor': ['requestEnter', 'requestExit'],
    'DistributedFurnitureManager': ['setOwnerId', 'setInteriorId', 'setAtticItems', 'setDirector',
                                    'avatarEnter', 'avatarExit'],
    'DistributedFurnitureItem': ['setItem', 'requestPosHpr', 'setMode'],
    'DistributedMailbox': ['setHouseId', 'setHousePos', 'setName', 'setFullIndicator',
                           'avatarEnter', 'avatarExit', 'freeAvatar'],
    'DistributedBank': ['avatarEnter', 'freeAvatar', 'transferMoney'],
    'DistributedCloset': ['setOwnerId', 'enterAvatar', 'freeAvatar', 'setDNA', 'setState',
                          'setCustomerDNA'],
    'DistributedPet': ['setOwnerId', 'setPetName', 'setTraitSeed', 'setSafeZone', 'setHead',
                       'setEars', 'setNose', 'setTail', 'setBodyTexture', 'setColor',
                       'setColorScale', 'setEyeColor', 'setGender', 'setLastSeenTimestamp',
                       'setMood', 'setMovie', 'freeAvatar'],
    'TimeManager': ['requestServerTime', 'serverTime'],
    'TutorialManager': ['requestSkipTutorial', 'skipTutorialResponse'],
    'DistributedNode': ['setParent', 'setX', 'setY', 'setZ', 'setH', 'setP', 'setR', 'setPos', 'setHpr',
                        'setPosHpr', 'setXY', 'setXZ', 'setXYH', 'setXYZH'],
    'DistributedSmoothNode': ['setSmStop', 'setSmPosHpr', 'setSmXYH', 'setSmXYZH', 'setSmXY',
                              'setSmXZ', 'setSmPos', 'setSmHpr', 'setSmH', 'setSmZ',
                              'setSmPosHprL', 'setComponentL', 'setComponentX',
                              'setComponentY', 'setComponentZ', 'setComponentH',
                              'setComponentP', 'setComponentR', 'setComponentT',
                              'clearSmoothing'],
    'DistributedToon': ['setName', 'setDNAString', 'setAnimState', 'setEmoteState', 'setTalk', 'setSC',
                        # Owner-view stats for the HUD (source/net/zone_objects.*).
                        'setHp', 'setMaxHp', 'toonUp', 'takeDamage', 'setMoney', 'setMaxMoney',
                        'setBankMoney', 'setMaxBankMoney', 'setExperience', 'setMaxCarry',
                        'setInventory', 'setTrackAccess', 'setTrackProgress',
                        'setTrackBonusLevel', 'setBattleId', 'setTickets', 'setEmoteAccess',
                        'setSpeedChatStyleIndex', 'setQuestCarryLimit', 'setMaxClothes',
                        'setNPCFriendsDict', 'setZonesVisited', 'setHoodsVisited',
                        'setDefaultShard', 'setDefaultZone', 'setLastHood', 'setTutorialAck',
                        'setHat', 'setGlasses', 'setBackpack', 'setShoes', 'setCheesyEffect',
                        'setAccessLevel', 'setPinkSlips', 'setNametagStyle', 'setFishingRod',
                        'setMaxFishTank', 'setFishTank', 'setQuests', 'setEarnedExperience',
                        # ToonTasks (source/net quests section, docs/design/quests.md)
                        'setQuestHistory', 'setRewardHistory', 'requestDeleteQuest'],
    # ---- zone 2000 objects (source/net/zone_objects.*, docs/design/playground.md) ----
    'DistributedNPCToonBase': ['setName', 'setDNAString', 'setPositionIndex', 'setAnimState',
                               'setPageNumber', 'avatarEnter', 'freeAvatar'],
    'DistributedNPCToon': ['setMovie', 'setMovieDone', 'chooseQuest', 'chooseTrack'],
    'DistributedNPCSpecialQuestGiver': ['setMovie', 'setMovieDone', 'chooseQuest', 'chooseTrack'],
    'DistributedNPCClerk': ['setMovie', 'setInventory'],
    'DistributedNPCTailor': ['setMovie', 'setDNA', 'setCustomerDNA'],
    'DistributedNPCBlocker': ['setMovie'],
    'DistributedNPCFisherman': ['setMovie', 'completeSale'],
    'DistributedNPCPartyPerson': ['setMovie', 'answer'],
    'DistributedNPCPetclerk': ['setMovie'],
    'DistributedNPCKartClerk': ['setMovie', 'buyKart', 'buyAccessory', 'transactionDone'],
    'DistributedNPCScientist': ['setChat'],
    'DistributedTreasure': ['setPosition', 'requestGrab', 'setGrab', 'setReject'],
    'DistributedDoor': ['setZoneIdAndBlock', 'setSwing', 'setDoorType', 'setDoorIndex',
                        'setOtherZoneIdAndDoId', 'requestEnter', 'requestExit', 'rejectEnter',
                        'avatarEnter', 'avatarExit', 'setState', 'setExitDoorState'],
    'DistributedTrolley': ['setState', 'fillSlot0', 'fillSlot1', 'fillSlot2', 'fillSlot3',
                           'emptySlot0', 'emptySlot1', 'emptySlot2', 'emptySlot3',
                           'requestBoard', 'rejectBoard', 'requestExit', 'setMinigameZone'],
    'DistributedCCharBase': ['setChat', 'fadeAway', 'setWalk', 'avatarEnter', 'avatarExit'],
    # ---- street cogs (source/world/suits.*, docs/design/combat.md section 3) ----
    'DistributedSuitPlanner': ['setZoneId'],
    'DistributedSuit': ['setDNAString', 'setLevelDist', 'setBrushOff', 'setSkelecog',
                        'setSkeleRevives', 'setHP', 'setSPDoId', 'setPathEndpoints',
                        'setPathPosition', 'setPathState', 'denyBattle', 'requestBattle'],
    # ---- street battles (source/world/battle.*, docs/design/combat.md sections 4-7) ----
    'DistributedBattle': ['setLevelDoId', 'setBattleCellId', 'setInteractivePropTrackBonus', 'setPosition',
                          'setZoneId', 'setInitialSuitPos', 'setMembers', 'adjust', 'setMovie',
                          'setChosenToonAttacks', 'setBattleExperience', 'denyLocalToonJoin',
                          'setBossBattle', 'setState', 'faceOffDone', 'toonRequestJoin', 'toonRequestRun',
                          'toonDied', 'adjustDone', 'timeout', 'movieDone', 'rewardDone', 'joinDone',
                          'requestAttack'],
    'DistributedDale': ['setFollowChip', 'setChipId'],
    'DistributedJailbirdDale': ['setFollowChip', 'setChipId'],
    # ---- fishing (source/net/client.cpp "fishing" section, docs/design/fishing.md) ----
    'DistributedFishingPond': ['setArea', 'hitTarget'],
    'DistributedFishingSpot': ['setPondDoId', 'setPosHpr', 'requestEnter', 'rejectEnter', 'requestExit',
                               'setOccupied', 'doCast', 'sellFish', 'sellFishComplete', 'setMovie'],
    'DistributedBoat': ['setState'],
    # ---- minigames (source/game/minigame.*, docs/design/trolley.md) ----
    'DistributedMinigame': ['setParticipants', 'setTrolleyZone', 'setStartingVotes', 'setMetagameRound',
                            'setDifficultyOverrides', 'setAvatarJoined', 'setAvatarReady', 'setAvatarExited',
                            'requestExit', 'setGameReady', 'setGameStart', 'setGameExit', 'setGameAbort'],
    'DistributedCatchGame': ['claimCatch', 'setObjectCaught', 'hitBySuit', 'reportDone', 'setEveryoneDone'],
    'DistributedCannonGame': ['setCannonPosition', 'setCannonLit', 'updateCannonPosition',
                             'setCannonWillFire', 'setToonWillLandInWater', 'announceToonWillLandInWater'],
    'DistributedTugOfWarGame': ['reportPlayerReady', 'sendGoSignal', 'sendStopSignal', 'sendGameType',
                               'reportEndOfContest', 'sendNewAvIdList', 'reportCurrentKeyRate',
                               'sendCurrentPosition', 'sendSuitPosition', 'remoteKeyRateUpdate'],
    'DistributedRingGame': ['setTimeBase', 'setColorIndices', 'setToonGotRing', 'setRingGroupResults'],
    'DistributedMazeGame': ['claimTreasure', 'setTreasureGrabbed', 'allTreasuresTaken', 'hitBySuit'],
    'DistributedPairingGame': ['setDeckSeed', 'setMaxOpenCards', 'openCardRequest', 'openCardResult',
                               'reportDone', 'setEveryoneDone', 'setSignaling'],
    'DistributedDivingGame': ['pickupTreasure', 'setTreasureGrabbed', 'handleFishCollision',
                              'performFishCollision', 'handleCrabCollision', 'performCrabCollision',
                              'setTreasureDropped', 'fishSpawn', 'removeFish', 'getCrabMoving',
                              'setCrabMoving', 'treasureRecovered', 'incrementScore'],
    'DistributedVineGame': ['reachedEndVine', 'setNewVine', 'setNewVineT', 'setJumpingFromVine',
                            'claimTreasure', 'setTreasureGrabbed', 'setScore', 'allAtEndVine',
                            'setFallingFromVine', 'setFallingFromMidair', 'setVineSections'],
    'DistributedTargetGame': ['setTimeBase', 'setToonGotRing', 'setRingGroupResults',
                              'setPlayerDone', 'setScore', 'setTargetSeed', 'setRoundDone',
                              'setSingleScore', 'setGameDone'],
    'DistributedCogThiefGame': ['throwingPie', 'hitBySuit', 'pieHitSuit', 'cogHitBarrel',
                                'cogAtReturnPos', 'updateSuitGoal', 'makeCogCarryBarrel',
                                'makeCogDropBarrel', 'markBarrelStolen'],
    'DistributedTwoDGame': ['showShootGun', 'toonHitByEnemy', 'toonFellDown', 'toonSquished',
                            'toonVictory', 'claimTreasure', 'claimEnemyShot', 'reportDone',
                            'setSectionsSelected', 'setTreasureGrabbed', 'setEnemyShot',
                            'addVictoryScore', 'setEveryoneDone'],
    'DistributedTagGame': ['tag', 'setIt', 'setTreasureScore'],
    'DistributedPatternGame': ['reportPlayerReady', 'setPattern', 'reportPlayerPattern',
                               'setPlayerPatterns', 'reportButtonPress', 'remoteButtonPressed'],
    'DistributedIceGame': ['setForceArrowInfo', 'setAvatarChoice', 'endingPositions',
                           'reportScoringMovieDone', 'claimTreasure', 'claimPenalty',
                           'setTimerStartTime', 'setMatchAndRound', 'setNewState',
                           'setTreasureGrabbed', 'setPenaltyGrabbed'],
    'DistributedRaceGame': ['setTimerStartTime', 'setAvatarChoice', 'setAvatarChose',
                            'setChancePositions', 'setServerChoices'],
    # ---- golf (source/world/golf.*, docs/design/golf.md) ----
    'DistributedGolfKart': ['setGolfCourse', 'setPosHpr', 'setColor', 'setState',
                            'fillSlot0', 'fillSlot1', 'fillSlot2', 'fillSlot3',
                            'emptySlot0', 'emptySlot1', 'emptySlot2', 'emptySlot3',
                            'requestBoard', 'requestExit', 'rejectBoard', 'setGolfZone'],
    'DistributedGolfCourse': ['setGolferIds', 'setCourseId', 'setCurHoleIndex', 'setCurHoleDoId',
                              'setCourseReady', 'setPlayHole', 'setCourseExit', 'setCourseAbort',
                              'avExited', 'setScores', 'changeDrivePermission', 'setReward',
                              'setAvatarJoined', 'setAvatarReadyCourse', 'setAvatarReadyHole',
                              'setAvatarExited', 'setDoneReward'],
    'DistributedGolfHole': ['setHoleId', 'setTimingCycleLength', 'setGolfCourseDoId', 'setGolferIds',
                            'setAvatarReadyHole', 'setAvatarTee', 'setTempAimHeading', 'postSwingState',
                            'ballMovie2AI', 'turnDone', 'ballInHole', 'golfersTurn', 'golferChooseTee',
                            'ballMovie2Client'],
    'DistributedPhysicsWorld': ['setCommonObjects', 'clientCommonObject',
                                'upSetCommonObjects'],
    'DistributedPhotoGame': ['newClientPhotoScore', 'newAIPhotoScore', 'filmOut'],
    'DistributedTravelGame': ['setTimerStartTime', 'setAvatarChoice', 'setAvatarChose',
                              'setServerChoices', 'setMinigames', 'setBonuses', 'setBoardIndex'],
    'PurchaseManager': ['setPlayerIds', 'setNewbieIds', 'setMinigamePoints', 'setPlayerMoney',
                        'setPlayerStates', 'setCountdown', 'setMetagameRound', 'setVotesArray',
                        'requestExit', 'requestPlayAgain', 'setInventory', 'setPurchaseExit'],
    'DistributedBuilding': ['setBlock', 'setSuitData', 'setVictorList', 'setState'],
    'DistributedSuitInterior': ['setZoneId', 'setExtZoneId', 'setDistBldgDoId', 'setNumFloors',
                                'setToons', 'setSuits', 'setState', 'setAvatarJoined',
                                'elevatorDone', 'reserveJoinDone'],
    'DistributedElevator': ['setBldgDoId', 'setState', 'fillSlot0', 'fillSlot1', 'fillSlot2',
                             'fillSlot3', 'fillSlot4', 'fillSlot5', 'fillSlot6', 'fillSlot7',
                             'emptySlot0', 'emptySlot1', 'emptySlot2', 'emptySlot3', 'emptySlot4',
                             'emptySlot5', 'emptySlot6', 'emptySlot7', 'requestBoard', 'rejectBoard',
                             'requestExit', 'setElevatorTripId', 'setAntiShuffle', 'setMinLaff'],
    'DistributedElevatorExt': ['setFloor'],
    'DistributedElevatorInt': ['requestBuildingExit', 'forcedExit'],
    'DistributedBattleBldg': [],
    # ---- Sellbot Factory dynamic level (source/world/factory.*, docs/design/factory.md) ----
    'DistributedFactory': ['setFactoryId', 'setSuits', 'setForemanConfronted', 'setDefeated'],
    'DistributedLevel': ['setLevelZoneId', 'setPlayerIds', 'setEntranceId', 'setZoneIds',
                         'setStartTimestamp', 'setAttribChange', 'requestCurrentLevelSpec',
                         'setSpecDeny', 'setSpecSenderDoId', 'setOuch'],
    'DistributedEntity': ['setLevelDoId', 'setEntId'],
    'DistributedInteractiveEntity': ['setAvatarInteract', 'requestInteract', 'rejectInteract',
                                     'requestExit', 'avatarExit', 'setState'],
    'DistributedLift': ['setStateTransition', 'setAvatarEnter', 'setAvatarLeave'],
    'DistributedDoorEntity': ['setLocksState', 'setDoorState', 'requestOpen'],
    'DistributedButton': [],
    'DistributedCrushableEntity': ['setPosition', 'setCrushed'],
    'DistributedCrate': ['requestPush', 'setReject', 'setAccept', 'setMoveTo', 'setDone'],
    'DistributedGoon': ['requestBattle', 'requestStunned', 'requestResync',
                        'setParameterize', 'setMovie'],
    'DistributedStomper': ['setMovie'],
    'DistributedBattleFactory': [],
    'DistributedFactoryElevatorExt': ['setEntranceId', 'setFactoryInteriorZone',
                                      'setFactoryInteriorZoneForce'],
    'DistributedMint': ['setZoneId', 'setMintId', 'setFloorNum', 'setRoomDoIds'],
    'DistributedMintRoom': ['setMintId', 'setRoomId', 'setRoomNum', 'setSuits',
                            'setBossConfronted', 'setDefeated'],
    'DistributedStage': ['setZoneId', 'setStageId', 'setLayoutIndex', 'setFloorNum',
                          'setRoomDoIds', 'setStageZone', 'elevatorAlert'],
    'DistributedStageRoom': ['setStageId', 'setRoomId', 'setRoomNum', 'setSuits',
                             'setBossConfronted', 'setDefeated'],
    'DistributedMintElevatorExt': ['setMintId', 'setMintInteriorZone', 'setMintInteriorZoneForce'],
    'DistributedLawOfficeElevatorExt': ['setEntranceId', 'setLawOfficeInteriorZone', 'setLawOfficeInteriorZoneForce'],
    'DistributedLawOfficeElevatorInt': ['setLawOfficeInteriorZone'],
    'DistributedMintBattle': [],
    'DistributedStageBattle': [],
    'DistributedLaserField': ['setGrid', 'setField', 'setSuccess', 'setActiveLF', 'setGridGame',
                              'hit', 'trapFire'],
    'DistributedSecurityCamera': ['setTarget', 'trapFire'],
    'DistributedMaze': ['setRoomDoId', 'setGameStart', 'setGameOver', 'setClientTriggered',
                        'setFinishedMaze', 'toonFinished', 'damageMe'],
    'DistributedAnimatedProp': ['setPropId', 'setAvatarInteract', 'setState'],
    'DistributedButterfly': ['setArea', 'setState'],
    'DistributedToonInterior': ['setZoneIdAndBlock', 'setState'],
    'DistributedHQInterior': ['setZoneIdAndBlock', 'setTutorial'],
    'DistributedGagshopInterior': ['setZoneIdAndBlock'],
    'DistributedPetshopInterior': ['setZoneIdAndBlock'],
    # ---- Goofy Speedway racing (source/world/racing.*, docs/design/racing.md) ----
    'DistributedVehicle': ['setOwner', 'setState', 'setBodyType', 'setBodyColor',
                           'setAccessoryColor', 'setEngineBlockType', 'setSpoilerType',
                           'setFrontWheelWellType', 'setBackWheelWellType', 'setRimType',
                           'setDecalType', 'requestControl', 'requestParked', 'setInput'],
    'DistributedRace': ['setZoneId', 'setTrackId', 'setRaceType', 'setCircuitLoop',
                         'setAvatars', 'setStartingPlaces', 'setLapCount', 'waitingForJoin',
                         'setEnteredRacers', 'prepForRace', 'startTutorial', 'startRace',
                         'goToSpeedway', 'genGag', 'dropAnvilOn', 'shootPiejectile',
                         'racerDisconnected', 'setPlace', 'setCircuitPlace', 'endCircuitRace',
                         'setRaceZone', 'hasGag', 'racerLeft', 'heresMyT', 'requestThrow',
                         'requestKart'],
    'DistributedGag': ['setInitTime', 'setActivateTime', 'setPos', 'setRace', 'setOwnerId',
                       'setType', 'hitSomebody'],
    'DistributedProjectile': ['setInitTime', 'setPos', 'setRace', 'setOwnerId', 'setType',
                              'hitSomebody'],
    'DistributedKartPad': ['setArea'],
    'DistributedRacePad': ['setState', 'setRaceZone', 'setTrackInfo'],
    'DistributedViewPad': ['setLastEntered'],
    'DistributedStartingBlock': ['setPadDoId', 'setPosHpr', 'setPadLocationId',
                                 'requestEnter', 'requestExit', 'setOccupied', 'setMovie',
                                 'movieFinished'],
    'DistributedLeaderBoard': ['setPosHpr'],
}

# Class families whose every subclass gets a table (and a kClassId namespace).
TABLE_FAMILIES = ['DistributedNPCToonBase', 'DistributedTreasure', 'DistributedDoor',
                  'DistributedCCharBase',                   'DistributedBuilding', 'DistributedAnimatedProp', 'DistributedElevator',
                  'DistributedEntity', 'DistributedInteractiveEntity', 'DistributedCrushableEntity',
                  # every minigame (the stub must see the DO of a game we cannot play) and both
                  # purchase managers (PurchaseManager, NewbiePurchaseManager)
                  'DistributedMinigame', 'PurchaseManager']

# Classes that get full descriptor tables (every inherited field) and
# required-field lists. Objects of any other class are dropped whole by the
# client (each Astron message is length framed, so nothing needs skipping).
# TABLE_FAMILIES (with all subclasses) are appended by expand_tables().
TABLE_CLASSES = ['DistributedDistrict', 'ToontownDistrict', 'ToontownDistrictStats',
                 'AstronLoginManager', 'TimeManager', 'TutorialManager', 'DistributedToon',
                 # (shard, 2) UberZone managers, as seen by tools/server/smoke_test.py
                 'CatalogManager', 'DistributedInGameNewsMgr', 'DistributedPartyManager',
                 'DistributedTrophyMgr', 'NewsManager', 'SafeZoneManager',
                 'ToontownMagicWordManager', 'WelcomeValleyManager',
                 # (shard, 2000) playground objects
                 'DistributedTrolley', 'DistributedBoat', 'DistributedButterfly', 'DistributedFishingPond',
                 'DistributedFishingSpot', 'DistributedFishingTarget',
                 'DistributedPondBingoManager', 'DistributedPartyGate',
                 # interiors (2513-2522), reached through DistributedDoor
                 'DistributedToonInterior', 'DistributedToonHallInterior',
                 'DistributedHQInterior', 'DistributedGagshopInterior',
                 'DistributedPetshopInterior',
                 # street branch zone / visgroup zones (cogs walking the street)
                 'DistributedSuitPlanner', 'DistributedSuit',
                 # street battle object (DistributedBattle: street cog fights)
                 'DistributedBattle', 'DistributedBattleBldg', 'DistributedSuitInterior',
                 'DistributedElevatorExt', 'DistributedElevatorInt']
TABLE_CLASSES += ['DistributedFactory', 'DistributedLevel', 'DistributedLift', 'DistributedDoorEntity',
                  'DistributedButton', 'DistributedCrate', 'DistributedGoon', 'DistributedStomper',
                  'DistributedBattleFactory', 'DistributedFactoryElevatorExt', 'DistributedMint',
                  'DistributedMintRoom', 'DistributedStage', 'DistributedStageRoom',
                  'DistributedMintElevatorExt', 'DistributedLawOfficeElevatorExt', 'DistributedLawOfficeElevatorInt',
                  'DistributedMintBattle', 'DistributedStageBattle', 'DistributedLaserField',
                  'DistributedSecurityCamera', 'DistributedMaze', 'EstateManager', 'DistributedEstate',
                  'DistributedHouse', 'DistributedHouseInterior', 'DistributedHouseDoor',
                  'DistributedFurnitureManager', 'DistributedFurnitureItem', 'DistributedMailbox',
                  'DistributedBank', 'DistributedCloset', 'DistributedPet',
                  'DistributedVehicle', 'DistributedRace', 'DistributedGag',
                  'DistributedProjectile', 'DistributedKartPad', 'DistributedRacePad',
                  'DistributedViewPad', 'DistributedStartingBlock', 'DistributedLeaderBoard']

# ---- friends / whispers (source/net/client_friends.cpp, docs/design/friends.md) ----
# Additive block: extends the tables above without editing their literals.
WANTED['DistributedToon'] += ['setFriendsList', 'friendsNotify', 'setWhisperSCFrom',
                              'setWhisperSCCustomFrom', 'setWhisperSCEmoteFrom', 'setTalkWhisper',
                              'setSystemMessage', 'teleportQuery', 'teleportResponse', 'teleportGiveup']
WANTED['FriendManager'] = ['friendQuery', 'cancelFriendQuery', 'inviteeFriendConsidering',
                           'inviteeFriendResponse', 'inviteeAcknowledgeCancel', 'friendConsidering',
                           'friendResponse', 'inviteeFriendQuery', 'inviteeCancelFriendQuery']
TABLE_CLASSES.append('FriendManager')
# ---- end friends ----

# DCSubatomicType values (panda/src/dcparser/dcSubatomicType.h).
ST = dict(int8=0, int16=1, int32=2, int64=3, uint8=4, uint16=5, uint32=6, uint64=7,
          float64=8, string=9, blob=10, blob32=11, int16array=12, int32array=13,
          uint16array=14, uint32array=15, int8array=16, uint8array=17,
          uint32uint8array=18, char=19, invalid=20)

OPS = ['END', 'I8', 'I16', 'I32', 'I64', 'U8', 'U16', 'U32', 'U64', 'F64', 'CHAR',
       'STRING', 'BLOB', 'BLOB32', 'FIXBYTES', 'VARARRAY', 'FIXARRAY']
OP = {n: i for i, n in enumerate(OPS)}
SCALAR = {ST['int8']: ('I8', 1, 'i8'), ST['int16']: ('I16', 2, 'i16'),
          ST['int32']: ('I32', 4, 'i32'), ST['int64']: ('I64', 8, 'i64'),
          ST['uint8']: ('U8', 1, 'u8'), ST['uint16']: ('U16', 2, 'u16'),
          ST['uint32']: ('U32', 4, 'u32'), ST['uint64']: ('U64', 8, 'u64'),
          ST['float64']: ('F64', 8, 'f64'), ST['char']: ('CHAR', 1, 'char')}
# Built-in array types: u16 byte length + elements.
BUILTIN_ARRAY = {ST['int8array']: ('I8', 'i8'), ST['uint8array']: ('U8', 'u8'),
                 ST['int16array']: ('I16', 'i16'), ST['uint16array']: ('U16', 'u16'),
                 ST['int32array']: ('I32', 'i32'), ST['uint32array']: ('U32', 'u32'),
                 ST['uint32uint8array']: (None, 'u32u8')}

KEYWORDS = [('required', 'isRequired'), ('broadcast', 'isBroadcast'), ('ram', 'isRam'),
            ('db', 'isDb'), ('clsend', 'isClsend'), ('clrecv', 'isClrecv'),
            ('ownsend', 'isOwnsend'), ('ownrecv', 'isOwnrecv'), ('airecv', 'isAirecv')]


def ident(s):
    return ''.join(c if c.isalnum() else '_' for c in s)


def u16(n):
    assert 0 <= n <= 0xffff, n
    return [n & 0xff, n >> 8]


class TypeInfo:
    """Program bytes, fixed byte size (or None) and a readable signature."""

    def __init__(self, code, size, sig):
        self.code, self.size, self.sig = code, size, sig


def concat(parts):
    code, size, sigs = [], 0, []
    for p in parts:
        code += p.code
        sigs.append(p.sig)
        size = None if size is None or p.size is None else size + p.size
    return TypeInfo(code, size, ','.join(s for s in sigs if s))


def fixed_string_len(text):
    # str(param) looks like "string(5);" for fixed-length strings/blobs.
    head = text.strip().split(';')[0].split()[0]
    if '(' in head and head.endswith(')'):
        rng = head[head.index('(') + 1:-1]
        if '-' not in rng and ',' not in rng and rng.isdigit():
            return int(rng)
    return None


def param_type(p):
    s = p.asSimpleParameter()
    if s is not None:
        t = s.getType()
        if t in SCALAR:
            op, size, sig = SCALAR[t]
            if s.hasModulus():
                sig += f'%{s.getModulus():g}'
            if s.getDivisor() != 1:
                sig += f'/{s.getDivisor()}'
            return TypeInfo([OP[op]], size, sig)
        if t in (ST['string'], ST['blob']):
            n = fixed_string_len(str(s))
            name = 'string' if t == ST['string'] else 'blob'
            if n is not None:
                return TypeInfo([OP['FIXBYTES']] + u16(n), n, f'{name}[{n}]')
            return TypeInfo([OP['STRING' if t == ST['string'] else 'BLOB']], None, name)
        if t == ST['blob32']:
            return TypeInfo([OP['BLOB32']], None, 'blob32')
        if t in BUILTIN_ARRAY:
            op, sig = BUILTIN_ARRAY[t]
            sub = [OP['U32'], OP['U8']] if op is None else [OP[op]]
            return TypeInfo([OP['VARARRAY']] + u16(len(sub)) + sub, None, sig + '[]')
        sys.exit(f'unsupported simple type {t}: {s}')
    a = p.asArrayParameter()
    if a is not None:
        el = param_type(a.getElementType())
        n = a.getArraySize()
        if n >= 0:
            if el.size is not None:
                # Fixed array of fixed-size elements: plain bytes. Keep the element
                # program so readers can still decode it.
                return TypeInfo([OP['FIXARRAY']] + u16(n) + u16(len(el.code)) + el.code,
                                el.size * n, f'{el.sig}[{n}]')
            return TypeInfo([OP['FIXARRAY']] + u16(n) + u16(len(el.code)) + el.code,
                            None, f'{el.sig}[{n}]')
        if el.sig == 'char':  # char[] is wire-identical to string
            return TypeInfo([OP['STRING']], None, 'string')
        return TypeInfo([OP['VARARRAY']] + u16(len(el.code)) + el.code, None,
                        f'{{{el.sig}}}[]' if ',' in el.sig and not el.sig.startswith('{') else f'{el.sig}[]')
    c = p.asClassParameter()
    if c is not None:
        cls = c.getClass()
        parts = []
        for i in range(cls.getNumInheritedFields()):
            f = cls.getInheritedField(i)
            parts.append(field_type(f))
        t = concat(parts)
        return TypeInfo(t.code, t.size, '{' + t.sig + '}')
    if p.asSwitchParameter() is not None:
        sys.exit(f'switch parameters are not supported: {p}')
    sys.exit(f'unknown parameter kind: {p}')


def field_type(f):
    m = f.asMolecularField()
    if m is not None:
        return concat([field_type(m.getAtomic(i)) for i in range(m.getNumAtomics())])
    a = f.asAtomicField()
    if a is not None:
        return concat([param_type(a.getElement(i)) for i in range(a.getNumElements())])
    p = f.asParameter()
    if p is not None:
        return param_type(p)
    sys.exit(f'unknown field kind: {f}')


def keyword_mask(f):
    mask = 0
    for bit, (_, meth) in enumerate(KEYWORDS):
        if getattr(f, meth)():
            mask |= 1 << bit
    return mask


def load(etc):
    dc = DCFile()
    for name in ('otp.dc', 'toon.dc'):
        if not dc.read(os.path.join(etc, name)):
            sys.exit(f'failed to parse {name}')
    expand_tables(dc)
    return dc


def ancestors(cls):
    out = set()
    for i in range(cls.getNumParents()):
        p = cls.getParent(i)
        out.add(p.getName())
        out |= ancestors(p)
    return out


def expand_tables(dc):
    """Append every TABLE_FAMILIES class and its subclasses (dc order) to
    TABLE_CLASSES, and give every table class a kClassId namespace."""
    for i in range(dc.getNumClasses()):
        cls = dc.getClass(i)
        name = cls.getName()
        if name in TABLE_CLASSES:
            continue
        if name in TABLE_FAMILIES or ancestors(cls) & set(TABLE_FAMILIES):
            TABLE_CLASSES.append(name)
    for name in TABLE_CLASSES:
        WANTED.setdefault(name, [])


def inherited(cls):
    return [cls.getInheritedField(i) for i in range(cls.getNumInheritedFields())]


def required_lists(cls):
    """Mirror DCClass::receive_update_broadcast_required(_owner)."""
    other, owner = [], []
    for f in inherited(cls):
        if f.asMolecularField() is not None or not f.isRequired():
            continue
        if f.isBroadcast():
            other.append(f)
        if f.isBroadcast() or f.isOwnrecv():
            owner.append(f)
        # Astron's client agent also forwards required clrecv fields; Panda does
        # not read them. They would desync the stream, so refuse to generate.
        if f.isClrecv() and not f.isBroadcast():
            sys.exit(f'{cls.getName()}.{f.getName()}: required clrecv field breaks parity')
    return other, owner


def collect(dc):
    fields = {}  # number -> DCField
    for cname in TABLE_CLASSES:
        cls = dc.getClassByName(cname)
        if cls is None:
            sys.exit(f'class {cname} not found')
        for f in inherited(cls):
            fields.setdefault(f.getNumber(), f)
    return fields


def emit_header(dc):
    out = ['// GENERATED by tools/dcgen/dcgen.py from open-toontown etc/*.dc. Do not edit.',
           '#pragma once', '#include <cstdint>', '', 'namespace tt::dc {', '',
           f'constexpr uint32_t kHash = {dc.getHash()}u;',
           'constexpr const char* kServerVersion = "sv1.0.47.38";', '',
           '// Field keyword bits (FieldDesc::keywords, kw_<field>).']
    for bit, (name, _) in enumerate(KEYWORDS):
        out.append(f'constexpr uint16_t kKw{name.capitalize()} = 1u << {bit};')
    out += ['', '// Type program opcodes (see tools/dcgen/dcgen.py for the encoding).',
            'enum TypeOp : uint8_t {']
    out += [f'    kOp{n.capitalize() if n != "END" else "End"} = {i},' for i, n in enumerate(OPS)]
    out += ['};', '']
    out += ['// constexpr string compare for static_assert on field signatures.',
            'constexpr bool sigEq(const char* a, const char* b) {',
            '    return *a == *b && (*a == 0 || sigEq(a + 1, b + 1));', '}', '']

    for cname, names in WANTED.items():
        cls = dc.getClassByName(cname)
        out.append(f'namespace {ident(cname)} {{')
        out.append(f'constexpr uint16_t kClassId = {cls.getNumber()};')
        for fname in names:
            f = cls.getFieldByName(fname)
            if f is None:
                sys.exit(f'{cname}.{fname} not found')
            out.append(f'constexpr uint16_t k_{fname} = {f.getNumber()};')
        for fname in names:
            f = cls.getFieldByName(fname)
            out.append(f'constexpr uint16_t kw_{fname} = 0x{keyword_mask(f):03x};')
        for fname in names:
            f = cls.getFieldByName(fname)
            out.append(f'constexpr const char* sig_{fname} = "{field_type(f).sig}";')
        out.append(f'}} // namespace {ident(cname)}')
        out.append('')

    # ---- descriptor tables ----
    fields = collect(dc)
    code = []
    descs = []
    for num in sorted(fields):
        f = fields[num]
        t = field_type(f)
        prog = t.code + [OP['END']]
        off = len(code)
        code += prog
        fixed = t.size if t.size is not None and t.size < 0xffff else 0xffff
        descs.append((num, keyword_mask(f), off, fixed, f.getName(), t.sig))
    assert len(code) < 0xffff

    out += ['// One entry per field of every class in kClasses, sorted by id.',
            '// typeOffset indexes kTypeCode (program ends with kOpEnd).',
            '// fixedSize = wire size when constant, else 0xffff.',
            'struct FieldDesc {', '    uint16_t id;', '    uint16_t keywords;',
            '    uint16_t typeOffset;', '    uint16_t fixedSize;', '};', '']
    out.append(f'constexpr int kTypeCodeSize = {len(code)};')
    out.append('constexpr uint8_t kTypeCode[kTypeCodeSize] = {')
    for i in range(0, len(code), 24):
        out.append('    ' + ', '.join(str(b) for b in code[i:i + 24]) + ',')
    out += ['};', '']
    out.append(f'constexpr int kNumFields = {len(descs)};')
    out.append('constexpr FieldDesc kFields[kNumFields] = {')
    for num, kw, off, fixed, name, sig in descs:
        out.append(f'    {{{num}, 0x{kw:03x}, {off}, {fixed}}},  // {name}({sig})'[:160])
    out += ['};', '', '// Parallel to kFields (debug logging only).',
            'constexpr const char* kFieldNames[kNumFields] = {']
    for d in descs:
        out.append(f'    "{d[4]}",')
    out += ['};', '']

    # ---- classes ----
    lists = []
    cls_rows = []
    for cname in TABLE_CLASSES:
        cls = dc.getClassByName(cname)
        other, owner = required_lists(cls)
        members = sorted({f.getNumber() for f in inherited(cls)})
        row = [cls.getNumber(), cname]
        for group in (other, owner):
            row += [len(lists), len(group)]
            lists += [f.getNumber() for f in group]
        row += [len(lists), len(members)]
        lists += members
        cls_rows.append(row)
    out += ['// Per class: required fields as sent in CLIENT_ENTER_OBJECT_REQUIRED_OTHER',
            '// (broadcast) and ..._OWNER (broadcast|ownrecv), in DCClass inherited order;',
            '// then every field id of the class, sorted (validates OTHER fields).',
            'struct ClassDesc {', '    uint16_t classId;', '    uint16_t otherOffset;',
            '    uint16_t otherCount;', '    uint16_t ownerOffset;', '    uint16_t ownerCount;',
            '    uint16_t memberOffset;', '    uint16_t memberCount;', '    const char* name;',
            '};', '']
    out.append(f'constexpr int kFieldListSize = {len(lists)};')
    out.append('constexpr uint16_t kFieldLists[kFieldListSize] = {')
    for i in range(0, len(lists), 16):
        out.append('    ' + ', '.join(str(n) for n in lists[i:i + 16]) + ',')
    out += ['};', '']
    out.append(f'constexpr int kNumClasses = {len(cls_rows)};')
    out.append('constexpr ClassDesc kClasses[kNumClasses] = {')
    for r in cls_rows:
        out.append(f'    {{{r[0]}, {r[2]}, {r[3]}, {r[4]}, {r[5]}, {r[6]}, {r[7]}, "{r[1]}"}},')
    out += ['};', '', '} // namespace tt::dc']
    return '\n'.join(out)


# ---------------------------------------------------------------- test vectors
def sample_value(t, depth=0):
    """A formatted value (DCPacker text syntax) with non-empty arrays."""
    s = t.asSimpleParameter()
    if s is not None:
        k = s.getType()
        if k in SCALAR:
            if k == ST['char']:
                return "'c'"
            div = s.getDivisor()
            base = 3 + depth
            return str(base / div) if div != 1 else str(base)
        if k in (ST['string'], ST['blob'], ST['blob32']):
            n = fixed_string_len(str(s))
            if k == ST['string']:
                return '"' + ('x' * (n if n is not None else 5)) + '"'
            # <hex> is a literal of the *packed* bytes, length prefix included.
            if n is not None:
                return '<' + 'ab' * n + '>'
            prefix = struct.pack('<I' if k == ST['blob32'] else '<H', 3).hex()
            return '<' + prefix + 'ab' * 3 + '>'
        if k in BUILTIN_ARRAY:
            if k == ST['uint32uint8array']:
                return '[(1, 2), (3, 4)]'
            return '[1, 2, 3]'
    a = t.asArrayParameter()
    if a is not None:
        n = a.getArraySize()
        el = a.getElementType()
        cnt = n if n >= 0 else 2
        if el.asSimpleParameter() is not None and el.asSimpleParameter().getType() == ST['char']:
            return '"' + 'y' * cnt + '"'
        return '[' + ', '.join(sample_value(el, depth + 1) for _ in range(cnt)) + ']'
    c = t.asClassParameter()
    if c is not None:
        cls = c.getClass()
        return '{' + ', '.join(sample_field(f, depth + 1, bare=True)
                               for f in inherited(cls)) + '}'
    sys.exit(f'no sample for {t}')


def sample_field(f, depth=0, bare=False):
    a = f.asAtomicField()
    if a is not None:
        inner = ', '.join(sample_value(a.getElement(i), depth) for i in range(a.getNumElements()))
        return inner if bare and a.getNumElements() == 1 else '(' + inner + ')'
    p = f.asParameter()
    if p is not None:
        return sample_value(p, depth)
    m = f.asMolecularField()
    inner = []
    for i in range(m.getNumAtomics()):
        at = m.getAtomic(i)
        inner += [sample_value(at.getElement(j), depth) for j in range(at.getNumElements())]
    return '(' + ', '.join(inner) + ')'


def emit_vectors(dc, path):
    """Binary file: repeated {u16 fieldId, u16 len, bytes} for default + sample
    values of every table field. The host harness skips each and checks len."""
    fields = collect(dc)
    blob = bytearray()
    count = 0
    for num in sorted(fields):
        f = fields[num]
        values = []
        if f.hasDefaultValue():
            values.append(f.getDefaultValue())
        text = sample_field(f)
        packed = f.parseString(text)
        if not packed and text not in ('()',):
            sys.exit(f'could not pack sample for {f.getName()}: {text}')
        values.append(packed)
        for v in values:
            blob += struct.pack('<HH', num, len(v)) + v
            count += 1
    with open(path, 'wb') as fp:
        fp.write(blob)
    print(f'wrote {count} vectors ({len(blob)} bytes) to {path}', file=sys.stderr)


def main(argv):
    etc = argv[1] if len(argv) > 1 and not argv[1].startswith('-') else 'ref/open-toontown/etc'
    if not os.path.exists(os.path.join(etc, 'otp.dc')):
        for candidate in ['ref/ref/open-toontown/etc', '/Users/alexchadwick/toontown-3ds/ref/open-toontown/etc']:
            if os.path.exists(os.path.join(candidate, 'otp.dc')):
                etc = candidate
                break
    dc = load(etc)
    if len(argv) > 3 and argv[2] == '--vectors':
        emit_vectors(dc, argv[3])
        return
    print(emit_header(dc))


if __name__ == '__main__':
    main(sys.argv)
