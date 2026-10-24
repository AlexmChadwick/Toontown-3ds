// Astron/Open Toontown protocol client: the login -> avatar -> zone state
// machine and the remote-toon table the game reads (AGENTS.md section 2,
// "Network"; milestones M3.1-M3.4).
//
// Usage from the main loop (main thread only; nothing here is thread safe):
//
//     static tt::net::Connection gConn;     // owns the net thread + rings
//     static tt::net::Client gNet;
//     gConn.init();                         // once (socInit, thread, 144 KB rings)
//     gNet.start(gConn, cfg);               // once (allocates the 64 KB rx buffer)
//     every frame:  gNet.update(nowMs);  while (gNet.pollEvent(ev)) {...}
//                   if (gNet.inZone()) { gNet.sendPosHpr(...) at 5 Hz while moving,
//                                        gNet.sendStop() when stopping, ... }
//     exit:         gNet.stop(); gConn.shutdown();
//
// The client reconnects with exponential backoff (1 s .. 30 s) on connection
// loss, eject or timeout, and redoes the whole HELLO/login/zone sequence.
//
// Avatar selection has two modes (ClientConfig::manualAvatar):
//   automatic (default; the autotest and tools/nettest use it): play the toon in
//       cfg.slot, or create cfg.dna when the account is empty.
//   manual (the front end, source/game/front_end.*): the state machine stops in
//       State::ChooseAvatar with the account's toons exposed (numAvatars/avatarAt).
//       The UI then calls chooseAvatar(id) or createAvatar(dna, slot), or
//       disconnect() to back out to the title. After a reconnect the client
//       re-plays the toon chosen earlier without asking again.
// A toon created in this session gets TutorialManager.requestSkipTutorial once
// after requestPlayAvatar (docs/design/ui.md section 2.1, step 4), in both modes.
#pragma once

#include <cstdint>

#include "net/connection.h"
#include "net/datagram.h"
#include "net/quest_net.h"
#include "net/book_net.h"
#include "net/boss_state.h"

namespace tt::net {

constexpr int kDnaSize = 15;          // ToonDNA.makeNetString()
constexpr int kMaxRemoteToons = 48;   // table size; the game draws the nearest 8
constexpr int kMaxDistricts = 8;
constexpr int kMaxAvatars = 6;        // PotentialAvatar slots
constexpr int kNameLen = 32;
constexpr int kAnimLen = 24;
constexpr int kChatLen = 96;
constexpr int kEventQueue = 256;

constexpr uint32_t kTtcPlayground = 2000;
constexpr uint32_t kParentRender = 2; // OTPGlobals.SPRender (DistributedNode.setParent)
constexpr uint32_t kParentDonaldsBoat = 3; // ToontownGlobals.SPDonaldsBoat (DistributedBoat.generate)

struct RemoteToon {
    uint32_t doId;
    uint32_t parentId, zoneId;
    char name[kNameLen];
    uint8_t dna[kDnaSize]; // ToonDNA net string (valid when hasDna)
    bool hasDna;
    bool moving;           // false after setSmStop
    // Latest pose, Panda units/degrees (H in [0, 360)).
    float x, y, z, h, p, r;
    uint32_t sampleMs;     // local clock time the pose was sampled on the sender
    uint32_t lastUpdateTick; // local clock time we received it (nowMs passed to update)
    char animState[kAnimLen]; // "neutral", "run", "walk", "Happy", ...
    float animRate;
    int16_t emoteId, emoteStamp;
    uint32_t emoteMs, emoteSerial;
    uint32_t parentToken;  // setParent: 3 = Donald's boat (pose is boat-local), 2 = render, 1 = hidden, 0 = unset
    char chat[kChatLen];   // last setTalk text (empty when the last chat was SpeedChat)
    int32_t scIndex;       // last setSC phrase index, or -1 when the last chat was typed
    uint32_t chatMs;
    int16_t hp, maxHp;     // broadcast setHp/setMaxHp (friends detail panel); 0 until received
    bool active;
};

enum class EventType : uint8_t {
    Spawn,        // a remote toon entered our zone (fields already filled)
    Despawn,      // left; the RemoteToon slot is no longer valid
    Pose,         // new position sample (also flagged: RemoteToon::lastUpdateTick)
    Emote,        // timestamped setEmoteState (RemoteToon emote fields)
    Anim,         // animState/animRate changed
    Name,
    Dna,
    Chat,         // RemoteToon::chat updated
    EnteredZone,  // we are in the playground and may send movement
    LeftZone,     // connection lost or ejected; all remote toons were despawned
    NpcSpawn,     // an NPC toon generated (npcs(); doId)
    NpcDespawn,
    TreasureSpawn, // a treasure generated (treasures(); doId)
    TreasureGone,  // grabbed or deleted
    Stats,         // our laff/jellybeans changed (ownStats())
    CCharSpawn,    // a classic character generated (findCChar(doId))
    CCharWalk,     // setWalk: new path leg (CChar src/dst/walkStartMs)
    CCharChat,     // setChat: chatter message (category, msgIndex, avatarDoId)
    CCharGone,
    BoatSpawn,     // Donald's Dock DistributedBoat generated (boat())
    BoatState,     // setState: new BoatState + stateStartMs
    BoatGone,
    SuitSpawn,     // a street cog generated (findSuit(doId)); fields filled
    SuitUpdate,    // path endpoints / position / state / hp changed
    SuitGone,      // left our interest or deleted; the Suit slot is no longer valid
    SuitPlannerUp, // a DistributedSuitPlanner generated (suitPlannerZone(doId))
    ShopMovie,     // a gag shop clerk sent setMovie (shopMovie(); doId = clerk)
    TailorMovie,   // a clothing-store tailor sent setMovie (tailorMovie(); doId = tailor)
    // ---- fishing (docs/design/fishing.md) ----
    FishPondUp,     // a DistributedFishingPond generated (fishingPond())
    FishSpotSpawn,  // a DistributedFishingSpot generated (findFishSpot(doId))
    FishSpotGone,
    FishOccupied,   // setOccupied: FishingSpot::occupant changed (0 = free)
    FishMovie,      // setMovie on a spot: lastFishMovie()
    FishReject,     // rejectEnter: the AI refused our requestEnter
    FishSold,       // sellFishComplete (fishSellResult())
    FishStats,      // our rod / fish tank changed (ownFish())
    // ---- battle events (docs/design/combat.md sections 4-7); doId = the battle's doId ----
    BattleSpawn,      // a DistributedBattle generated (findBattle(doId)); members and state are filled
    BattleMembers,    // setMembers changed
    BattleState,      // setState (name, timestamp)
    BattleMovie,      // setMovie with movieHasBeenMade = 1 (the decoded round is in Battle::movie)
    BattleExperience, // setBattleExperience (Battle::exp)
    BattleAdjust,     // adjust(ts): move to the new slots, then adjustDone
    BattleGone,       // the battle object left our interest or was deleted
    CogBuildingUp,     // DistributedBuilding generated
    CogBuildingState,  // building state/data/victors changed
    CogElevatorUp,     // DistributedElevator generated or updated
    CogInteriorUp,     // DistributedSuitInterior generated or updated
    CogInteriorState,  // floor interior state/lists changed
    CogInteriorGone,
    CogBuildingGone,
    LevelUp,          // a DistributedLevel (factory, mint/stage/country club room) generated
    LevelState,       // ... a field of it changed (setZoneIds, setDefeated, ...)
    LevelGone,
    FacilityUp,       // DistributedMint / Stage / CountryClub (the floor) generated or changed
    FacilityGone,
    FacilityNextFloor, // setStageZone / setCountryClubZone (arg: the next floor's zone); the AI
                       // deletes the floor object right after, so the zone travels in the event
    LevelSuitUp,      // DistributedFactorySuit / MintSuit / StageSuit generated (client_level.cpp)
    LevelSuitState,   // setConfrontToon / setReturn / setHP / setReserve ...
    LevelSuitGone,
    LevelBlockerState, // BattleBlocker generated or changed
    EstateManagerUp, EstateZone, EstateUp, EstateState, HouseUp, HouseState,
    HouseInteriorUp, FurnitureUp, FurnitureState, PetUp, PetState, EstateGone, PetGone,
    ClosetUp, ClosetGone, MailboxUp, BankUp,
    BattleJoinDenied, // denyLocalToonJoin
    SuitDenied,       // DistributedSuit.denyBattle (doId = the cog we bumped into)
    SuitBrushOff,     // setBrushOff: the cog says no (doId = the cog)
    // ---- trolley and minigames (docs/design/trolley.md); Event::arg carries the extra value ----
    TrolleyUp,          // the playground's DistributedTrolley generated (trolley())
    TrolleyState,       // setState: trolley()->state / stateStartMs changed
    TrolleyFill,        // fillSlotN(avId != 0): doId = avId, arg = seat index 0..3
    TrolleyEmpty,       // emptySlotN(avId != 0): doId = avId, arg = seat index
    TrolleyReject,      // rejectBoard (we could not board)
    TrolleyMinigameZone, // setMinigameZone: doId = dynamic zone id, arg = minigame id
    TrolleyGone,
    GolfKartUp, GolfKartState, GolfZone,
    GolfCourseUp, GolfCourseState, GolfHoleUp, GolfHoleState,
    MinigameUp,         // a DistributedMinigame (any game) generated (minigame())
    MinigameReady,      // setGameReady
    MinigameStart,      // setGameStart (minigame()->gameStartMs)
    MinigameExit,       // setGameExit
    MinigameAbort,      // setGameAbort
    MinigameGone,
    CatchCaught,        // DistributedCatchGame.setObjectCaught: doId = avId, arg = objNum
    CatchEveryoneDone,  // DistributedCatchGame.setEveryoneDone
    CatchHitBySuit,     // DistributedCatchGame.hitBySuit from another player: doId = avId
    // ---- Cannon (DistributedCannonGame) ----
    CannonUpdatePosition,
    CannonWillFire,
    CannonLandedInWater,
    // ---- Tug of War (DistributedTugOfWarGame) ----
    TugGoSignal,
    TugStopSignal,
    TugGameType,
    TugPosition,
    TugRemoteKeyRate,
    // ---- Ring (DistributedRingGame) ----
    RingGroupResults,
    // ---- Maze (DistributedMazeGame) ----
    MazeTreasureGrabbed,
    MazeAllTreasuresTaken,
    MazeHitBySuit,
    // ---- Pairing (DistributedPairingGame) ----
    PairingOpenCardResult,
    PairingEveryoneDone,
    // ---- Diving (DistributedDivingGame) ----
    DivingTreasureGrabbed,
    DivingIncrementScore,
    DivingFishSpawn,
    DivingCrabMoving,
    DivingTreasureDropped,
    DivingPerformFishCollision,
    DivingPerformCrabCollision,
    // ---- Vine (DistributedVineGame) ----
    VineSetVineSections,
    VineSetScore,
    VineAllAtEnd,
    VineTreasureGrabbed,
    // ---- Target (DistributedTargetGame) ----
    TargetSetSeed,
    TargetRoundDone,
    TargetSingleScore,
    TargetGameDone,
    // ---- Cog Thief (DistributedCogThiefGame) ----
    CogThiefUpdateGoal,
    CogThiefCarryBarrel,
    CogThiefDropBarrel,
    CogThiefBarrelStolen,
    // ---- Two-D (DistributedTwoDGame) ----
    TwoDSetSections,
    TwoDTreasureGrabbed,
    TwoDEveryoneDone,
    // ---- Tag (DistributedTagGame) ----
    TagSetIt,
    TagTreasureScore,
    // ---- Pattern (DistributedPatternGame) ----
    PatternSetPattern,
    PatternSetPlayerPatterns,
    PatternRemoteButton,
    // ---- Ice (DistributedIceGame) ----
    IceMatchAndRound,
    IceNewState,
    IceTimerStart,
    IceAvatarChoice,
    IceTreasureGrabbed,
    IcePenaltyGrabbed,
    // ---- Race (DistributedRaceGame) ----
    RaceTimerStart,
    RaceAvatarChose,
    RaceChancePositions,
    RaceServerChoices,
    // ---- Photo (DistributedPhotoGame) ----
    PhotoNewAIScore,
    PurchaseUp,         // a PurchaseManager / NewbiePurchaseManager generated (purchase())
    PurchaseStates,     // setPlayerStates changed
    PurchaseExit,       // setPurchaseExit
    PurchaseGone,
    // ---- Goofy Speedway kart racing (docs/design/racing.md) ----
    RacingPadUp, RacingPadState, RacingBlockUp, RacingBlockState,
    RacingRaceUp, RacingRaceState, RacingVehicleUp, RacingVehicleState,
    RacingGone,
    // ---- quests (docs/design/quests.md) ----
    NpcMovie,      // an NPC setMovie arrived: pollNpcMovie() (doId = the NPC object)
    NpcFree,       // an NPC sent freeAvatar to us: leave the dialog (doId = the NPC object)
    Quests,        // ownQuests() changed
};

struct Event {
    EventType type;
    uint32_t doId;
    int32_t arg = 0;
};

enum class State : uint8_t {
    Idle, Connecting, Hello, Login, Districts, AvatarList, ChooseAvatar, CreateAvatar, PlayAvatar,
    EnterZone, InZone, ChangingZone, Backoff, Failed,
};
const char* stateName(State s);

// NPC toons (DistributedNPCToonBase family: shopkeepers, Fisherman, Party Planners...).
// Upstream parents them to the zone node npc_origin_<posIndex> (Fisherman:
// npc_fisherman_origin_<posIndex>; Party Planner: party_person_left/right by parity).
struct NpcToon {
    uint32_t doId;
    uint16_t classId;
    char name[kNameLen];
    uint8_t dna[kDnaSize];
    bool hasDna;
    uint8_t posIndex;
    bool questGiver;   // DistributedNPCToon family: avatarEnter starts a ToonTask conversation
    bool special;      // DistributedNPCSpecialQuestGiver (its own chooseQuest/setMovie field ids)
    bool active;
};
constexpr int kMaxNpcs = 16;

// Treasures (DistributedTreasure family): walk into one to grab it (+laff).
struct Treasure {
    uint32_t doId;
    float x, y, z;
    bool active;
};
constexpr int kMaxTreasures = 16;

// Classic characters (DistributedCCharBase family: Mickey, Minnie, ...). The class id
// picks the character; setWalk(src, dst, ts) names a leg of its walk graph.
struct CChar {
    uint32_t doId;
    uint16_t classId;
    char src[8], dst[8];
    uint32_t walkStartMs; // local plat::nowMs() at which the leg started (from ts)
    uint32_t chatCategory;
    uint32_t chatMsgIndex;
    uint32_t chatAvatarDoId;
    uint32_t chipId;
    float offsetX, offsetY;
    bool hasFollowChip;
    bool active;
};
constexpr int kMaxCChars = 4;

// Donald's Dock boat (DistributedBoat, zone 1000). setState(string, int16 ts): the AI
// cycles DockedEast -10s-> SailingWest -20s-> DockedWest -10s-> SailingEast -20s-> ...
enum class BoatState : uint8_t { Off, DockedEast, SailingWest, DockedWest, SailingEast };
struct Boat {
    uint32_t doId;
    BoatState state;
    uint32_t stateStartMs; // local plat::nowMs() at which the state began (from ts)
    bool active;
};

enum class CogBuildingState : uint8_t { Unknown, Toon, Suit, BecomingSuit, BecomingToon, Other };
struct CogBuilding {
    uint32_t doId = 0, block = 0, interiorZoneId = 0;
    int8_t track = -1, difficulty = -1, numFloors = -1;
    CogBuildingState state = CogBuildingState::Unknown;
    uint32_t stateStartMs = 0;
    uint32_t victors[8] = {};
    uint8_t numVictors = 0;
    bool active = false;
};
constexpr int kMaxCogBuildings = 16;

struct CogElevator {
    uint32_t doId = 0, buildingDoId = 0, tripId = 0;
    char state[20] = {};
    uint32_t stateStartMs = 0;
    uint32_t slots[8] = {};
    uint8_t slotFlags[8] = {};
    uint8_t minLaff = 0;
    // Cog HQ facility elevators (factory / mint / DA office): which anchor they stand at
    // (setEntranceId, or setMintId mapped to its origin) and where riding them leads
    // (set*InteriorZone, sent to the riders when it leaves; 0 until then).
    uint16_t classId = 0;
    int8_t origin = -1;
    uint32_t destZone = 0;
    // DistributedCogKart (Bossbot HQ golf karts to the country clubs): placed by setPosHpr.
    float x = 0, y = 0, z = 0, h = 0;
    bool hasPose = false;
    bool forcedBossDestination = false;
    bool active = false;
};
constexpr int kMaxCogElevators = 8;

struct CogInterior {
    uint32_t doId = 0, zoneId = 0, extZoneId = 0, buildingDoId = 0;
    int8_t numFloors = 0;
    uint32_t toons[8] = {};
    uint8_t numToons = 0;
    uint32_t suits[8] = {};
    uint32_t suitLevels[8] = {};
    uint16_t suitHp[8] = {};
    uint8_t numSuits = 0;
    char state[24] = {};
    uint32_t stateStartMs = 0;
    bool active = false;
};
constexpr int kMaxCogInteriors = 2;

// ---- facility levels (client_level.cpp, docs/design/levels.md) ----
// A DistributedLevel: the Sellbot factory, or one room of a mint / stage / country club floor.
struct LevelObj {
    uint32_t doId = 0, levelZoneId = 0, startMs = 0;
    uint16_t classId = 0;
    uint16_t levelId = 0;    // DistributedFactory.setFactoryId, or a room's setRoomId
    uint16_t facilityId = 0; // a room's setMintId / setStageId / setCountryClubId
    uint8_t roomNum = 0;     // a room's setRoomNum: its index among the floor's rooms and hallways
    uint32_t zoneIds[128] = {};
    uint16_t numZones = 0;
    uint32_t suits[32] = {};
    uint8_t numSuits = 0;
    uint32_t reserveSuits[32] = {};
    uint8_t numReserveSuits = 0;
    uint8_t entranceId = 0; // DistributedLevel.setEntranceId (entrancePoint entities' entranceId)
    uint32_t playerIds[4] = {}; // DistributedLevel.setPlayerIds: entrance placement order
    uint8_t numPlayers = 0;
    uint32_t confronted = 0; // setForemanConfronted / setBossConfronted: the toon facing the boss
    bool defeated = false, active = false;
    bool isRoom() const { return facilityId != 0; }
};
constexpr int kMaxLevels = 16;
// The floor object of a multi-room facility; its rooms are LevelObjs (setRoomDoIds order).
enum class FacilityKind : uint8_t { Mint, Stage, CountryClub };
struct Facility {
    uint32_t doId = 0, zoneId = 0;
    FacilityKind kind = FacilityKind::Mint;
    uint16_t facilityId = 0, layoutIndex = 0;
    uint8_t floorNum = 0;
    uint32_t roomDoIds[kMaxLevels] = {};
    uint8_t numRooms = 0;
    bool active = false;
};
// DistributedElevatorFloor (DA offices) / DistributedClubElevator (country clubs): the elevator to
// the next floor, latched onto a room's elevatorMarker entity (setLatch: the marker's doId).
struct LevelElevator {
    uint32_t doId = 0, latch = 0;
    bool club = false;       // DistributedClubElevator (ELEVATOR_COUNTRY_CLUB timings)
    bool locked = true;      // setLocked: closed until the floor's last battle is won
    char state[16] = {};     // DistributedElevatorFSM.setState: Opening, WaitEmpty, Closing, ...
    uint32_t stateMs = 0;
    uint32_t slots[8] = {};
    bool active = false;
};
constexpr int kMaxLevelElevators = 4;

// ---- facility level Cogs and battle blockers (client_level.cpp, docs/design/levels.md) ----
// DistributedFactorySuit (and its Mint/Stage subclasses): the spec cogId names its spawn pose,
// path and battle cell in the level's TLV (world/level.*); the client walks it locally.
struct LevelSuit {
    uint32_t doId = 0, levelDoId = 0, cogId = 0;
    char name[4] = {};      // SuitDNA head code
    char dept = 0;
    int16_t levelDist = 0, hp = 0;
    uint8_t skelecog = 0;
    bool reserve = false;   // setReserve: waits off stage until it joins a battle
    bool hasDna = false;
    uint32_t confrontToon = 0; // setConfrontToon: chase this toon (0 = none)
    uint32_t returnSerial = 0; // bumped by setReturn
    uint32_t chaseSerial = 0;  // bumped by setConfrontToon
    bool active = false;
};
constexpr int kMaxLevelSuits = 64; // a whole mint / stage / country club floor
// BattleBlocker (and DistributedLaserField): a sphere of spec 'radius' at its entity; walking
// in starts the battle of its cell's cogs, or joins the running one (BattleBlocker.startBattle).
struct LevelBlocker {
    uint32_t doId = 0, levelDoId = 0, entId = 0, battleId = 0;
    uint32_t suits[8] = {};
    uint8_t numSuits = 0;
    bool blockerActive = false, finished = false, active = false;
};
constexpr int kMaxLevelBlockers = 32;
// Distributed level entities the level runtime animates (doors now; see client_level.cpp).
struct LevelEntityObj {
    uint32_t doId = 0, levelDoId = 0, entId = 0;
    uint16_t classId = 0;
    uint8_t state = 0;     // DistributedDoorEntity.setDoorState: FourState index
    uint32_t stateMs = 0;  // local time the state began (its network timestamp)
    uint16_t locks = 0;    // setLocksState: 4 bits per lock (LockBase FourState index)
    uint32_t locksMs[3] = {}; // local time each lock's state last changed
    uint8_t fromState = 0;   // DistributedLift.setStateTransition(toState -> state, fromState, ...)
    uint32_t arrivalMs = 0;  // ... arrival time (local)
    uint8_t movie = 0;       // DistributedStomper.setMovie mode (StomperGlobals STOMPER_*)
    uint32_t movieMs = 0;    // ... and its local start time
    char stateName[12] = {}; // DistributedInteractiveEntity.setState: "off" / "attract" / "playing"
    uint32_t avatarId = 0;   // setAvatarInteract: the toon on the switch; DistributedBarrelBase.setGrab
    uint32_t grabMs = 0;     // DistributedBarrelBase.setGrab: when (local)
    bool grabbedByUs = false;
    float pauseTime = 0;     // DistributedGoon.setMovie(mode -> movie, avId -> avatarId, pauseTime, ts -> movieMs)
    bool crushed = false;    // DistributedCrushableEntity.setCrushed
    bool active = false;
};
constexpr int kMaxLevelEntities = 128;
// ---- end facility levels ----

struct EstateHouse {
    uint32_t doId = 0, avatarId = 0;
    char name[kNameLen] = {};
    uint8_t pos = 0, type = 0, gardenPos = 0, color = 0;
    bool active = false;
};
constexpr int kMaxEstateHouses = 6;
struct Estate {
    uint32_t doId = 0, zoneId = 0;
    uint8_t estateType = 0, closestHouse = 0xff;
    uint32_t slotToons[kMaxEstateHouses] = {};
    EstateHouse houses[kMaxEstateHouses] = {};
    bool ready = false, active = false;
};
struct HouseInterior {
    uint32_t doId = 0, houseId = 0;
    uint8_t houseIndex = 0;
    bool active = false;
};
struct EstateFurniture {
    uint32_t doId = 0, itemId = 0;
    float x = 0, y = 0, z = 0, h = 0, p = 0, r = 0;
    uint8_t mode = 0;
    bool active = false;
};
constexpr int kMaxEstateFurniture = 48;
struct Pet {
    uint32_t doId = 0, ownerId = 0, traitSeed = 0, safeZone = 0;
    char name[kNameLen] = {};
    int8_t head = -1, ears = -1, nose = -1, tail = -1, body = 0, color = 0, colorScale = 4,
           eyeColor = 0, gender = 0;
    float x = 0, y = 0, z = 0, h = 0;
    bool active = false;
};
constexpr int kMaxPets = 8;
struct Closet {
    uint32_t doId = 0, ownerId = 0;
    bool active = false;
};
// Street cogs (DistributedSuit; docs/design/combat.md section 3). The AI never broadcasts a
// position: it sends the endpoints of a path through the street's suit graph, the leg the
// path was at and when, and a path state. source/world/suits.* rebuilds the walk.
struct Suit {
    uint32_t doId;
    uint32_t zoneId;        // visgroup zone it is in now (setLocation)
    char name[4];           // SuitDNA head code ("f", "tbc", ...); "" until setDNAString
    char dept;              // 'c' 'l' 'm' 's'
    int16_t levelDist;      // setLevelDist: level index into the suit's hp/level tables
    int16_t hp;
    uint8_t skelecog, skeleRevives;
    uint32_t spDoId;        // the suit planner that owns it
    uint16_t pathStart, pathEnd, pathMinLen, pathMaxLen; // setPathEndpoints (DNA point indices)
    uint16_t pathIndex;     // setPathPosition: leg index ...
    uint32_t pathPosMs;     // ... that was current at this local plat::nowMs()
    int8_t pathState;       // 0 stop, 1 walk, 2 fly away, 3 battle, 4 victory dance then fly away
    bool hasDna;
    bool active;
};
constexpr int kMaxSuits = 16;
struct SuitPlanner {
    uint32_t doId;
    uint32_t zoneId;        // DistributedSuitPlanner.setZoneId: the street branch (2100, ...)
    bool active;
};
constexpr int kMaxSuitPlanners = 2;

// ---- shop (gag shop clerks; docs/design/shop.md) ----
// NPCToons.PURCHASE_MOVIE_* modes of DistributedNPCClerk.setMovie(mode, npcId, avId, timestamp).
constexpr uint8_t kPurchaseMovieClear = 0;
constexpr uint8_t kPurchaseMovieStart = 1;
constexpr uint8_t kPurchaseMovieComplete = 2;
constexpr uint8_t kPurchaseMovieNoMoney = 3;
constexpr uint8_t kPurchaseMovieTimeout = 8;
constexpr uint32_t kClerkCountdownMs = 120000; // NPCToons.CLERK_COUNTDOWN_TIME
// Latest setMovie of the clerk we are dealing with (only movies about us or CLEAR are kept).
struct ShopMovie {
    uint32_t clerkDoId = 0;
    uint8_t mode = kPurchaseMovieClear;
    uint32_t npcId = 0;
    uint32_t avId = 0;
    uint32_t deadlineMs = 0; // local clock: START time + kClerkCountdownMs (the AI times out then)
    uint32_t seq = 0;        // bumps on every setMovie
};
// NPCToons.PURCHASE_MOVIE_* modes of DistributedNPCTailor.setMovie(mode, npcId, avId, timestamp).
constexpr uint8_t kTailorMovieClear = 0;
constexpr uint8_t kTailorMovieStart = 1;
constexpr uint8_t kTailorMovieComplete = 2;
constexpr uint8_t kTailorMovieNoMoney = 3;
constexpr uint8_t kTailorMovieTimeout = 8;
constexpr uint8_t kTailorMovieStartBrowse = 9;
constexpr uint8_t kTailorMovieStartNoRoom = 10;
constexpr uint32_t kTailorCountdownMs = 300000; // NPCToons.TAILOR_COUNTDOWN_TIME
constexpr uint8_t kTailorWhichShirt = 1;  // ClosetGlobals.SHIRT
constexpr uint8_t kTailorWhichShorts = 2; // ClosetGlobals.SHORTS
struct TailorMovie {
    uint32_t tailorDoId = 0;
    uint8_t mode = kTailorMovieClear;
    uint32_t npcId = 0;
    uint32_t avId = 0;
    uint32_t deadlineMs = 0;
    uint32_t seq = 0;
};
// ---- fishing (DistributedFishingPond / DistributedFishingSpot; docs/design/fishing.md) ----
// The ponds and spots are generated by the AI in the playground zone. Stock Open Toontown
// (sv1.0.47.38) generates ponds only: findFishingSpots() is a TODO and DistributedFishingSpotAI
// is an empty class, so against it no spot ever arrives and source/world/fishing.* runs its
// local mode. Everything below is exercised only when a server provides spots.
struct FishingPond {
    uint32_t doId;
    uint32_t area; // setArea: the canonical zone id of the pond (2000 for the TTC playground)
    bool active;
};
struct FishingSpot {
    uint32_t doId;
    uint32_t pondDoId;
    float x, y, z, h, p, r; // setPosHpr (zone space, Panda degrees)
    uint32_t occupant;      // avatar doId, 0 = free (setOccupied)
    bool active;
};
constexpr int kMaxFishSpots = 8;
// FishGlobals movie modes / item codes (toontown/fishing/FishGlobals.py:6-20).
enum FishMode : uint8_t { kFishNoMovie = 0, kFishEnterMovie, kFishExitMovie, kFishCastMovie, kFishPullInMovie };
enum FishCode : uint8_t {
    kFishNothing = 0, kFishQuestItem = 1, kFishItem = 2, kFishJellybean = 3, kFishBoot = 4, kFishGag = 5,
    kFishOverTankLimit = 8, kFishItemNewEntry = 9, kFishItemNewRecord = 10,
};
struct FishMovieMsg {
    uint32_t spotDoId;
    uint8_t mode, code;
    uint16_t item1, item2, item3; // PullIn: fish (genus, species, weight in ounces) / jellybean amount in item1
    float power;                  // 0..1 (CastMovie)
    float heading;                // degrees, local to the spot (CastMovie)
    uint32_t seq;                 // increments with every message
};
// Owner-view fishing data of our toon (setFishingRod / setMaxFishTank / setFishTank).
constexpr int kMaxFishTank = 40;
struct OwnFish {
    uint8_t rod = 0;
    uint8_t maxTank = 20;
    uint8_t tankCount = 0;
    uint8_t genus[kMaxFishTank] = {};
    uint8_t species[kMaxFishTank] = {};
    uint16_t weight[kMaxFishTank] = {}; // ounces
};
// ---- street battles: DistributedBattle (docs/design/combat.md sections 4 to 7) ----
// Members are stored by index into suits[] / toons[] (doIds); the role lists are bit masks
// over those indices (setMembers sends each as a string of digits).
constexpr int kBattleSuits = 4;
constexpr int kBattleToons = 4;
constexpr int kMaxBattles = 4;     // our fight plus whatever is in sight
constexpr int kMaxDeathGroups = 8; // setBattleExperience deathList: 4 bytes each

enum class BattleStateId : uint8_t {
    None, Off, FaceOff, WaitForJoin, WaitForInput, MakeMovie, PlayMovie, Reward, Resume, Other,
};

// One toon attack of setMovie: toonIdx (index into BattleMovie::toons), track/level
// (ToontownBattleGlobals; -1 = no attack, -3 = pass), target (suit index, toon index for heals,
// -1 = group), the damage per suit (index = suit index into BattleMovie::suits) and the suits
// (bit per index) that died from it.
struct BattleToonAttack {
    int8_t toonIdx, track, level;
    int32_t target;
    int16_t hp[kBattleSuits];
    uint8_t numHp;
    int16_t accBonus, hpBonus;
    int8_t died, revive;
};
// One suit attack of setMovie: suitIdx, attack (index into that cog's attack list, -1 = none), the
// target toon index (-1 = all), damage per toon index, bit per toon index that died.
struct BattleSuitAttack {
    int8_t suitIdx, attack, target;
    int16_t hp[kBattleToons];
    uint8_t numHp;
    int8_t toonDied, beforeToons, taunt;
};
struct BattleMovie {
    bool active;
    uint8_t numToons, numSuits;
    uint32_t toons[kBattleToons], suits[kBattleSuits];
    BattleToonAttack toonAttacks[4];
    BattleSuitAttack suitAttacks[4];
};
struct BattleExpToon {
    int32_t id;                // -1 = empty block
    int16_t origExp[7], earnedExp[7];
};
struct BattleExperience {
    BattleExpToon toons[4];
    uint8_t numDeathGroups;
    uint8_t deaths[kMaxDeathGroups][4]; // suitHeadTypes index, level, toon bits, flags
};
struct Battle {
    // DistributedBattleFinal adds boss ownership and an independent reserve barrier.
    uint32_t bossId;
    uint8_t bossRound;
    uint8_t bossSide;
    BossBarrier bossBarriers[BossState::kMaxBarriers];
    int numBossBarriers;
    uint32_t doId;
    uint32_t zoneId;
    uint32_t levelDoId;        // setLevelDoId: facility battles (DistributedLevelBattle), else 0
    uint32_t battleCellId;     // setBattleCellId: the level spec battle cell it sits on
    float x, y, z;             // setPosition: the battle cell
    float sx, sy, sz;          // setInitialSuitPos: where the first cog touched us
    BattleStateId state;
    char stateName[20];
    uint32_t stateMs;          // local plat::nowMs() the state began (from its timestamp)
    uint32_t stateSeq;         // bumped by every setState
    uint8_t numSuits, numToons;
    uint32_t suits[kBattleSuits];
    uint32_t toons[kBattleToons];
    uint8_t suitsJoining, suitsPending, suitsActive, suitsLured; // masks over suits[]
    int8_t suitTrap[kBattleSuits];                               // trap level, -1 = none
    uint8_t toonsJoining, toonsPending, toonsActive, toonsRunning; // masks over toons[]
    uint32_t membersSeq;
    BattleMovie movie;
    uint32_t movieSeq;         // bumped whenever setMovie arrives with an active movie
    BattleExperience exp;
    uint32_t expSeq;
    uint32_t adjustSeq;
    uint32_t adjustMs;         // local time of the last adjust(ts)
    bool active;
};
// ---- trolley and minigame objects (docs/design/trolley.md) ----
// DistributedTrolley (class 86): one per playground, in the hood zone. setState(string, ts)
// moves it through entering -> waitEmpty -> waitCountdown -> leaving; fillSlotN/emptySlotN
// seat up to four toons. `slot` holds the avatar sitting (or still climbing out) in each seat.
enum class TrolleyState : uint8_t { Off, Entering, WaitEmpty, WaitCountdown, Leaving };
constexpr int kTrolleySeats = 4;
struct Trolley {
    uint32_t doId;
    TrolleyState state;
    uint32_t stateStartMs; // local plat::nowMs() at which the state began (from ts)
    uint32_t slot[kTrolleySeats];
    bool active;
};
constexpr int kGolfSeats = 4;
constexpr int kGolfMaxGolfers = 4;
struct GolfKart {
    uint32_t doId = 0, golfZone = 0;
    int8_t courseId = -1;
    float x = 0, y = 0, z = 0, h = 0, p = 0, r = 0;
    uint32_t slots[kGolfSeats] = {};
    char state[20] = {};
    uint32_t stateMs = 0;
    bool active = false;
};
struct GolfCourse {
    uint32_t doId = 0, currentHoleDoId = 0;
    int8_t courseId = -1, currentHole = -1;
    uint32_t golfers[kGolfMaxGolfers] = {};
    int numGolfers = 0;
    int16_t scores[kGolfMaxGolfers] = {};
    int numScores = 0;
    int8_t numHoles = 0, par = 0;
    bool reward = false, exiting = false, active = false;
};
struct GolfHole {
    uint32_t doId = 0, courseDoId = 0;
    int8_t holeId = -1;
    float cycleLength = 10.0f;
    uint32_t golfers[kGolfMaxGolfers] = {};
    int numGolfers = 0, currentGolfer = 0;
    uint8_t tee = 0;
    float ballX = 0, ballY = 0, ballZ = 0;
    uint32_t swingSeq = 0;
    bool active = false;
};
// DistributedMinigame (any of the 17 games; the DO lives in the dynamic zone the trolley
// handed us). Game-specific required fields are skipped through the generated tables.
constexpr int kMinigamePlayers = 4;
constexpr int32_t kNoDifficultyOverride = 2147483647; // MinigameGlobals.NoDifficultyOverride
struct Minigame {
    uint32_t doId;
    uint16_t classId;
    uint32_t participants[kMinigamePlayers];
    int numParticipants;
    uint32_t trolleyZone;
    int32_t difficultyOverride; // raw int32 (value / 65536), kNoDifficultyOverride when unset
    int32_t zoneOverride;       // -1 = none
    bool gameReady;
    bool started;
    uint32_t gameStartMs;       // local plat::nowMs() of setGameStart's timestamp
    bool exitMsg;
    bool aborted;
    bool active;

    // ---- game-specific minigame state ----
    // Cannon:
    float cannonZRot[kMinigamePlayers] = {};
    float cannonAngle[kMinigamePlayers] = {};
    float cannonFireTime[kMinigamePlayers] = {};
    float cannonLandTime = 0.0f;
    uint32_t cannonLandedAv = 0;

    // Tug of War:
    uint8_t tugGameType = 0;
    uint8_t tugSuitType = 1;
    float tugOffsets[kMinigamePlayers] = {};
    float tugSuitOffset = 0.0f;
    bool tugGo = false;
    bool tugStopped = false;

    // Ring:
    int16_t ringTimeBase = 0;
    int8_t ringColorIndices[4] = {-1, -1, -1, -1};
    uint8_t ringGroupResult = 0;

    // Maze:
    uint32_t mazeGrabbedAv = 0;
    uint32_t mazeGrabbedTreasure = 0;
    bool mazeAllTaken = false;

    // Pairing:
    uint32_t pairingDeckSeed = 0;
    uint8_t pairingMaxOpenCards = 2;
    int16_t pairingCardToTurnUp = -1;
    uint32_t pairingOpenAvId = 0;
    int16_t pairingMatchingCard = -1;
    int8_t pairingPoints = 0;
    int16_t pairingCardsToTurnDown[4] = {-1, -1, -1, -1};
    int pairingTurnDownCount = 0;
    bool pairingEveryoneDone = false;

    // Diving:
    uint32_t divingGrabbedAv = 0;
    uint32_t divingGrabbedChest = 0;
    uint32_t divingScoreAv = 0;
    uint32_t divingScoreNewSpot = 0;
    int16_t divingFishSpawnTs = 0;
    uint32_t divingFishSpawnCode = 0;
    uint32_t divingFishSpawnerId = 0;
    uint16_t divingFishSpawnOffset = 0;
    uint32_t divingCrabId = 0;
    int8_t divingCrabRand1 = 0;
    int8_t divingCrabRand2 = 0;
    int16_t divingCrabX = 0;
    int8_t divingCrabDir = 0;

    // Vine:
    uint8_t vineSections[8] = {};
    int vineSectionCount = 0;
    uint32_t vineScoreAv = 0;
    uint32_t vineScoreVal = 0;
    bool vineAllAtEnd = false;
    uint32_t vineGrabbedAv = 0;
    uint32_t vineGrabbedTreasure = 0;

    // Target:
    uint32_t targetSeed = 0;
    bool targetRoundDone = false;
    uint16_t targetSingleScore = 0;
    uint32_t targetSingleAvId = 0;
    bool targetGameDone = false;

    // Cog Thief:
    int8_t cogThiefGoalCog = -1;
    int8_t cogThiefGoalType = 0;
    int64_t cogThiefGoalId = -1;
    float cogThiefGoalPos[3] = {};
    int8_t cogThiefCarryCog = -1;
    int8_t cogThiefCarryBarrel = -1;
    int8_t cogThiefDropCog = -1;
    int8_t cogThiefDropBarrel = -1;
    int8_t cogThiefStolenBarrel = -1;

    // Two-D:
    uint8_t twoDSections[16] = {};
    int twoDSectionCount = 0;
    uint32_t twoDGrabbedAv = 0;
    uint8_t twoDGrabbedSection = 0;
    uint8_t twoDGrabbedTreasure = 0;
    bool twoDEveryoneDone = false;

    // Tag:
    uint32_t tagItAvId = 0;
    uint16_t tagTreasureScores[kMinigamePlayers] = {};

    // Pattern:
    uint8_t patternSeq[16] = {};
    int patternSeqLen = 0;
    uint32_t patternFastestAvId = 0;
    uint32_t patternRemoteButtonAv = 0;
    uint8_t patternRemoteButtonIdx = 0;
    uint8_t patternRemoteButtonWrong = 0;

    // Ice Slide:
    int8_t iceMatch = 0;
    int8_t iceRound = 0;
    char iceState[32] = {};
    int16_t iceTimerStart = 0;
    float iceChoicesForce[kMinigamePlayers] = {};
    float iceChoicesDir[kMinigamePlayers] = {};
    uint32_t iceGrabbedAv = 0;
    uint32_t iceGrabbedItem = 0;

    // Race:
    int16_t raceTimerStart = 0;
    uint32_t raceAvatarChose = 0;
    uint8_t raceChancePositions[kMinigamePlayers] = {};
    int8_t raceServerChoices[kMinigamePlayers * 4] = {};
    uint8_t raceServerPositions[kMinigamePlayers * 4] = {};
    int8_t raceServerRewards[kMinigamePlayers * 4] = {};
    int raceServerChoiceCount = 0;

    // Photo Fun:
    uint32_t photoScoreAv = 0;
    uint8_t photoScoreAssignment = 0;
    float photoScoreStars = 0.0f;
};
// PurchaseManager (176) / NewbiePurchaseManager (177): generated in the minigame's zone when
// the game ends. minigamePoints[i] is the payout of playerIds[i] (already added to the toon).
struct Purchase {
    uint32_t doId;
    uint16_t classId;
    uint32_t playerIds[kMinigamePlayers];
    uint8_t minigamePoints[kMinigamePlayers];
    uint8_t playerMoney[kMinigamePlayers]; // before the payout
    uint8_t playerStates[kMinigamePlayers];
    bool exitMsg;
    bool active;
};

struct RacingPad {
    uint32_t doId = 0, raceZone = 0;
    char state[20] = {};
    uint16_t trackIds[8] = {};
    uint8_t trackCount = 0;
    bool active = false;
};
struct RacingBlock {
    uint32_t doId = 0, padDoId = 0, occupant = 0;
    float x = 0, y = 0, z = 0, h = 0, p = 0, r = 0;
    uint8_t location = 0;
    bool active = false;
};
struct RacingVehicle {
    uint32_t doId = 0, owner = 0, driver = 0;
    char state = 'P';
    int8_t body = 0, bodyColor = 0, accessoryColor = 0;
    int8_t engine = 0, spoiler = 0, frontWheels = 0, backWheels = 0, rims = 0, decal = 0;
    int8_t input = 0;
    float x = 0, y = 0, z = 0, h = 0;
    bool active = false;
};
struct RacingRace {
    uint32_t doId = 0, zone = 0;
    uint16_t track = 0, raceType = 0, laps = 3;
    uint32_t racers[8] = {};
    uint8_t numRacers = 0, localPlace = 0, localLap = 0;
    int16_t startTs = 0;
    bool started = false, finished = false, active = false;
};
constexpr int kMaxRacingPads = 8;
constexpr int kMaxRacingBlocks = 8;
constexpr int kMaxRacingVehicles = 16;

// Our toon's owner-view stats (DistributedToon setHp/setMaxHp/setMoney/...).
struct OwnStats {
    int16_t hp = 15, maxHp = 15;
    int16_t money = 0, maxMoney = 40, bankMoney = 0;
    uint32_t defaultZone = 2000; // setDefaultZone: the playground a sad toon returns to
    // Gags (owner-only DistributedToon fields; tracks in ToontownBattleGlobals order).
    uint16_t experience[7] = {};
    uint8_t inventory[7][7] = {}; // count per track, level
    uint8_t trackAccess[7] = {0, 0, 0, 0, 1, 1, 0};
    uint8_t maxCarry = 20;
    uint8_t emoteAccess[25] = {};
    bool hasEmotes = false;
    bool hasGags = false; // setInventory received
};

// ===================== friends / whispers (docs/design/friends.md) =====================
// Implemented in client_friends.cpp. Self-contained: own types, own event queue.
constexpr int kMaxFriends = 50;        // OTPGlobals.MaxFriends
constexpr int kWhisperLogLen = 16;
constexpr int kWhisperTextLen = 96;

struct FriendInfo {
    uint32_t doId;
    uint8_t code;          // FriendEntry.friendCode (bit 0 = open chat)
    bool nearby;           // visible in our interest right now (so definitely online)
    bool hasName, hasDna;  // learned from the friend's own DistributedToon this session
    char name[kNameLen];
    uint8_t dna[kDnaSize];
};

enum class RequestState : uint8_t {
    Idle,
    Checking,     // friendQuery sent, waiting for friendConsidering
    Asking,       // invitee is considering; waiting for friendResponse
    Yes, No,      // final answers
    NotAvailable, // friendConsidering 0 / no answer
    Already, Self, Ignored, NotAccepting, OtherTooMany, TooMany, Maybe,
    WentAway,     // target left our zone first
    NoManager,    // this server has no FriendManager object
};
struct FriendRequest {
    RequestState state = RequestState::Idle;
    uint32_t targetId = 0;
    char targetName[kNameLen] = {};
    int32_t context = 0;
    uint32_t sinceMs = 0;
};
// An invitation someone sent us (FriendManager.inviteeFriendQuery).
struct FriendInvite {
    bool active = false;
    uint32_t inviterId = 0;
    char name[kNameLen] = {};
    uint8_t dna[kDnaSize] = {};
    bool hasDna = false;
    int32_t context = 0;
    uint32_t sinceMs = 0;
};
enum class WhisperKind : uint8_t { SpeedChat, Custom, Emote, Typed, System, Outgoing };
struct Whisper {
    uint32_t fromId;       // sender (ourselves for Outgoing)
    uint32_t toId;         // recipient (ourselves for incoming)
    WhisperKind kind;
    uint16_t index;        // SpeedChat/custom/emote index (the UI decodes it); 0 for Typed/System
    char name[kNameLen];   // the other party
    char text[kWhisperTextLen]; // Typed and System text; empty for index-only kinds
    uint32_t ms;
};
struct TeleportResult {
    bool valid = false;
    uint32_t avId = 0;
    int8_t available = 0;  // 0 busy, 1 ok, 2 ignored
    uint32_t shardId = 0, hoodId = 0, zoneId = 0;
    uint32_t ms = 0;
};
enum class FriendEventType : uint8_t {
    ListChanged,      // setFriendsList arrived, or a friend's name/dna/nearby state changed
    InviteIn,         // pendingInvite() is set
    InviteCancelled,  // the inviter withdrew (pendingInvite() cleared)
    RequestUpdate,    // friendRequest().state changed
    Whisper,          // a new whisper (last entry of the log); doId = sender
    TeleportResult,   // teleportResult() updated
    TeleportAsked,    // someone sent us teleportQuery (we answered automatically); doId = requester
    Notify,           // friendsNotify: doId = who, see notifyStatus()
};
struct FriendEvent {
    FriendEventType type;
    uint32_t doId;
};
// State (kept in Client::friends_).
struct FriendsState {
    uint32_t managerId = 0;                 // a FriendManager object we have seen (0 = none)
    FriendInfo list[kMaxFriends] = {};
    int numFriends = 0;
    FriendRequest request;
    FriendInvite invite;
    Whisper log[kWhisperLogLen] = {};
    int logHead = 0, logCount = 0;          // ring; logHead = oldest
    TeleportResult teleport;
    int8_t notifyStatus = 0;
    FriendEvent events[32] = {};
    uint32_t evHead = 0, evTail = 0;
    uint32_t teleportAskId = 0;             // the toon our last teleportQuery went to
};
// ===================== end friends types =====================

struct ClientConfig {
    const char* host = "127.0.0.1";
    uint16_t port = 7198;
    const char* playToken = "dev";      // dev servers accept any token
    // ToonDNA net string: 't', head dss, torso ms, legs m, boy, shirt/sleeve/
    // shorts 0/0, arm/glove/leg/head colours 1/0/1/1. Colours must be in
    // ToonDNA.defaultBoyColorList or the AI rewrites them ("AvatarHackWarning").
    uint8_t dna[kDnaSize] = {'t', 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1};
    uint8_t slot = 0;                   // createAvatar position when the list is empty
    uint32_t zoneId = kTtcPlayground;
    // false: pick/create the toon automatically (dna/slot above). true: stop in
    // State::ChooseAvatar and wait for chooseAvatar()/createAvatar().
    bool manualAvatar = false;
};

struct District {
    uint32_t doId;
    char name[kNameLen];
    bool available;
    bool active;
};

// AstronLoginManagerUD nameState values (docs/design/ui.md section 2.1).
enum class NameState : uint8_t { Locked = 0, Open = 1, Pending = 2, Approved = 3, Rejected = 4 };

struct PotentialAvatar {
    uint32_t id;
    char name[kNameLen];
    uint8_t dna[kDnaSize];
    bool hasDna;           // dna holds a full 15-byte net string
    uint8_t slot;          // avPosition, 0..kMaxAvatars-1
    uint8_t nameState;     // NameState
};

class Client {
public:
    bool start(Transport& transport, const ClientConfig& cfg);
    void stop();                        // graceful CLIENT_DISCONNECT
    void update(uint32_t nowMs);        // pump messages, advance the state machine

    State state() const { return state_; }
    bool inZone() const { return state_ == State::InZone; }
    // Human-readable reason for the last failure/eject/retry ("" when none). Stays
    // set until we reach the zone or clearError(); the UI shows it as a notice.
    const char* lastError() const { return error_; }
    void clearError() { error_ = ""; }
    uint16_t lastEjectCode() const { return ejectCode_; } // 0 when the last failure was not an eject
    // Failed and will not retry (logged in elsewhere, version mismatch, ...). connect() starts over.
    bool fatal() const { return state_ == State::Failed; }
    // One-line description of what the client is doing, for a loading screen.
    const char* progressText() const;
    // Milliseconds until the next reconnect attempt (State::Backoff), else 0.
    uint32_t retryInMs() const;

    // ---- manual avatar selection (ClientConfig::manualAvatar) ----
    // The account's toons; valid from State::ChooseAvatar on (until the next reconnect).
    bool avatarListReady() const { return state_ == State::ChooseAvatar; }
    int numAvatars() const { return numAvatars_; }
    const PotentialAvatar& avatarAt(int i) const { return avatars_[i]; }
    const PotentialAvatar* avatarInSlot(uint8_t slot) const;
    // Plays one of the listed toons. Only in State::ChooseAvatar; false otherwise.
    bool chooseAvatar(uint32_t avId);
    // Creates a toon in an empty slot (< kMaxAvatars) and plays it. dna must pass
    // ToonDNA.isValidNetString (checked here: the server ejects bad DNA). Only in
    // State::ChooseAvatar; false (nothing sent) otherwise.
    bool createAvatar(const uint8_t dna[kDnaSize], uint8_t slot);
    // Graceful CLIENT_DISCONNECT and back to State::Idle (the "back to title"
    // path). Keeps the buffers; connect() starts a fresh session with the same config.
    void disconnect();
    bool connect();
    // Drop the connection and go through the normal retry path, keeping the chosen toon
    // and zone (after sleep, when the socket is known to be dead).
    void forceReconnect(const char* why);
    // True when the toon being played was created in this session.
    bool playingNewAvatar() const { return newAvatarId_ != 0 && newAvatarId_ == avatarId_; }
    bool tutorialSkipSent() const { return skipTutorialSent_; }
    // skipTutorialResponse: -1 not answered yet, 0 refused, 1 ok.
    int tutorialSkipResult() const { return skipTutorialResult_; }
    uint32_t avatarId() const { return avatarId_; }
    uint32_t shardId() const { return shardId_; }
    const char* avatarName() const { return ownName_; }
    const char* avatarName(uint32_t doId) const;
    bool avatarPos(uint32_t doId, float* x, float* y, float* z) const;
    const uint8_t* avatarDna() const { return ownDna_; }
    const char* districtName() const;
    bool timeSynced() const { return timeSynced_; }

    // Remote toons (doId != ours). Iterate 0..kMaxRemoteToons and test active.
    const RemoteToon& remoteToonSlot(int i) const { return toons_[i]; }
    const RemoteToon* findToon(uint32_t doId) const;
    int numRemoteToons() const { return numToons_; }
    bool pollEvent(Event& out);
    uint32_t droppedEvents() const { return droppedEvents_; }

    // Local avatar updates (InZone only; return false otherwise). Angles in
    // degrees, positions in Panda units.
    bool sendPosHpr(float x, float y, float z, float h, float p, float r);
    bool sendStop();
    // DistributedNode.setParent on our avatar: kParentRender or kParentDonaldsBoat. The pose sent
    // afterwards must be in that parent's frame (DistributedSmoothNode).
    void sendSetParent(uint32_t token);
    // Our world position for chat targeting when the pose we send is parent-local.
    void setOwnWorldPos(float x, float y, float z) { ownX_ = x, ownY_ = y, ownZ_ = z; }
    // name must be a ToontownGlobals.ToonAnimStates state (e.g. "run", "jumpAirborne"), not a clip.
    bool sendAnimState(const char* name, float rate);
    // Zone change through the quiet zone (QuietZoneState): location + interest to zone 1,
    // drop the old zone's objects, then location + interest to `zoneId` (+ `visZones` for
    // streets: [branch] + visgroup vis list). Load the new zone's geometry BEFORE calling
    // (DNA-anchored objects generate as soon as the interest opens). InZone again (and an
    // EnteredZone event) when the server confirms. Only valid while inZone().
    static constexpr int kMaxVisZones = 24;
    bool changeZone(uint32_t zoneId, const uint32_t* visZones = nullptr, int numVisZones = 0);
    bool changingZone() const { return state_ == State::ChangingZone; }
    // Moving between visgroups of a street (Street.py doEnterZone): location + interest on
    // [cell] + vis zones directly, no quiet zone. Only while inZone().
    bool changeStreetCell(uint32_t cellZone, const uint16_t* visZones, int numVisZones);
    uint32_t zoneId() const { return cfg_.zoneId; }

    // SpeedChat phrase (DistributedPlayer.setSC, ownsend broadcast).
    bool sendSpeedChat(uint16_t msgIndex);
    bool sendEmote(int id, float multiplier=1);
    // DistributedTreasure.requestGrab (clsend): the AI answers with setGrab(avId) or setReject.
    bool sendRequestGrab(uint32_t treasureDoId);

    const NpcToon& npcSlot(int i) const { return npcs_[i]; }
    const NpcToon* findNpc(uint32_t doId) const;
    const CChar* findCChar(uint32_t doId) const;
    const Boat* boat() const { return boat_.active ? &boat_ : nullptr; }
    const CogBuilding& cogBuildingSlot(int i) const { return cogBuildings_[i]; }
    const CogElevator& cogElevatorSlot(int i) const { return cogElevators_[i]; }
    const CogInterior* cogInterior() const { return cogInterior_.active ? &cogInterior_ : nullptr; }
    // ---- facility levels (client_level.cpp) ----
    const LevelObj* levelObj(uint32_t doId) const;
    const LevelObj& levelSlot(int n) const { return levels_[n]; }
    const Facility* facility() const { return facility_.active ? &facility_ : nullptr; }
    const LevelElevator& levelElevatorSlot(int n) const { return levelElevators_[n]; }
    bool sendLevelElevatorBoard(uint32_t doId); // DistributedElevatorFSM.requestBoard
    const LevelSuit& levelSuitSlot(int n) const { return levelSuits_[n]; }
    const LevelSuit* findLevelSuit(uint32_t doId) const;
    // Name / level / HP of any cog we know: a street DistributedSuit or a level cog.
    struct CogIdentity { char name[4]; int16_t levelDist, hp; bool hasDna; };
    bool cogIdentity(uint32_t doId, CogIdentity* out) const;
    const LevelBlocker& levelBlockerSlot(int n) const { return levelBlockers_[n]; }
    const LevelEntityObj& levelEntitySlot(int n) const { return levelEntities_[n]; }
    bool sendLevelDoorOpen(uint32_t doId); // DistributedDoorEntity.requestOpen (we bumped the door)
    bool sendLevelInteract(uint32_t doId, bool enter); // DistributedSwitch requestInteract / requestExit
    bool sendLevelOuch(uint32_t levelDoId, uint8_t penalty); // DistributedLevel.setOuch (hazard damage)
    bool sendLevelLiftRider(uint32_t doId, bool enter); // DistributedLift setAvatarEnter / setAvatarLeave
    bool sendLevelBarrelGrab(uint32_t doId);            // DistributedBarrelBase.requestGrab
    // DistributedGoon: our toon walked into its radar / landed on its head (walk-track time).
    bool sendLevelGoonBattle(uint32_t doId, float pauseTime);
    bool sendLevelGoonStunned(uint32_t doId, float pauseTime);
    // DistributedLevel.setVisibility: location stays the level zone, interest is the level zone,
    // the UberZone and the visible zones (ids from setZoneIds), like sendSetZoneMsg.
    bool setLevelVisibility(uint32_t levelZone, const uint32_t* zones, int count);
    // DistributedFactorySuit.requestBattle: pos/hpr in the cog's parent-entity space (the AI
    // only keeps them as confrontPos; level battles sit on the cog's battle cell).
    bool sendLevelSuitBattle(uint32_t doId, float x, float y, float z, float h);
    bool sendLevelSuitAlert(uint32_t doId);   // setAlert(our avId): a chase cog saw us
    bool sendLevelSuitStrayed(uint32_t doId); // setStrayed: chased more than 10 from its spawn
    // World pose of a level battle cell (world/level.*): applied to level battles on generate.
    using LevelBattlePlacer = bool (*)(void* user, uint32_t levelDoId, uint32_t cellId, float* x, float* y,
                                       float* z, float* h);
    void setLevelBattlePlacer(LevelBattlePlacer fn, void* user) { levelBattlePlacer_ = fn; levelBattlePlacerUser_ = user; }
    // ---- end facility levels ----
    bool requestHome();
    bool sendEstateClientReady(uint32_t doId);
    bool sendHouseEnter(uint32_t doId);
    bool sendHouseExit(uint32_t doId);
    bool sendClosetEnter(uint32_t doId);
    bool sendClosetDNA(uint32_t doId, const uint8_t dna[kDnaSize], int8_t finished, uint8_t which);
    bool sendClosetExit(uint32_t doId);
    bool sendMailboxEnter(uint32_t doId);
    bool sendMailboxExit(uint32_t doId);
    bool sendBankEnter(uint32_t doId);
    bool sendBankExit(uint32_t doId);
    const Estate* estate() const { return estate_.active ? &estate_ : nullptr; }
    const HouseInterior* houseInterior() const { return houseInterior_.active ? &houseInterior_ : nullptr; }
    const EstateFurniture& estateFurnitureSlot(int i) const { return furniture_[i]; }
    const Pet& petSlot(int i) const { return pets_[i]; }
    const Closet* closet() const { return closet_.active ? &closet_ : nullptr; }
    uint32_t houseDoorDoId() const { return houseDoorDoId_; }
    const CogBuilding* findCogBuilding(uint32_t doId) const;
    const CogElevator* findCogElevator(uint32_t doId) const;
    bool sendCogElevatorBoard(uint32_t doId);
    bool sendCogElevatorExit(uint32_t doId);
    bool sendCogInteriorAvatarJoined(uint32_t doId);
    bool sendCogInteriorElevatorDone(uint32_t doId);
    bool sendCogInteriorReserveJoinDone(uint32_t doId);
    const Suit* findSuit(uint32_t doId) const;
    const Suit& suitSlot(int i) const { return suits_[i]; }
    // Branch zone of the suit planner with this doId (0 when unknown).
    uint32_t suitPlannerZone(uint32_t doId) const;
    const Treasure& treasureSlot(int i) const { return treasures_[i]; }
    const OwnStats& ownStats() const { return ownStats_; }
    const OwnBookStats& ownBook() const { return ownBook_; }
    const District* districts() const { return districts_; }
    int numDistricts() const { return kMaxDistricts; }

    // ---- shop (additive section; keep together) ----
    // DistributedNPCToonBase.avatarEnter (clsend) at a clerk: the AI answers setMovie START (we
    // may shop for kClerkCountdownMs), NO_MONEY, or freeAvatar when it is busy.
    bool sendClerkAvatarEnter(uint32_t clerkDoId);
    // DistributedNPCClerk.setInventory(blob, money, done) with the 49-byte InventoryBase net
    // string. Only while a START movie for us is open on that clerk. done=1 completes the
    // purchase (the AI validates, then resends our setInventory/setMoney); done=0 is the
    // upstream per-purchase sync and is not needed by this client.
    bool sendClerkSetInventory(uint32_t clerkDoId, const uint8_t inv[7][7], int16_t money, bool done);
    const ShopMovie& shopMovie() const { return shopMovie_; }
    bool shopOpenFor(uint32_t clerkDoId) const {
        return shopMovie_.mode == kPurchaseMovieStart && shopMovie_.avId == avatarId_ && avatarId_ != 0 &&
               shopMovie_.clerkDoId == clerkDoId;
    }
    // DistributedNPCToonBase.avatarEnter at a tailor and DistributedNPCTailor.setDNA.
    bool sendTailorAvatarEnter(uint32_t tailorDoId);
    bool sendTailorDNA(uint32_t tailorDoId, const uint8_t dna[kDnaSize], int8_t finished, uint8_t which);
    const TailorMovie& tailorMovie() const { return tailorMovie_; }
    bool tailorOpenFor(uint32_t tailorDoId) const {
        return tailorMovie_.mode != kTailorMovieClear && tailorMovie_.mode != kTailorMovieComplete &&
               tailorMovie_.mode != kTailorMovieTimeout && tailorMovie_.mode != kTailorMovieNoMoney &&
               tailorMovie_.avId == avatarId_ && avatarId_ != 0 && tailorMovie_.tailorDoId == tailorDoId;
    }
    // ---- fishing (docs/design/fishing.md; implemented in the "fishing" section of client.cpp) ----
    const FishingPond* fishingPond() const { return pond_.active ? &pond_ : nullptr; }
    const FishingSpot& fishSpotSlot(int i) const { return fishSpots_[i]; }
    const FishingSpot* findFishSpot(uint32_t doId) const;
    const FishMovieMsg& lastFishMovie() const { return fishMovie_; }
    // sellFishComplete(trophyResult, numFishCaught) of the last answer.
    bool fishSellTrophy() const { return fishSellTrophy_; }
    uint16_t fishSellCount() const { return fishSellCount_; }
    const OwnFish& ownFish() const { return ownFish_; }
    // clsend fields of DistributedFishingSpot / Pond. False (nothing sent) unless InZone.
    bool sendFishRequestEnter(uint32_t spotDoId);
    bool sendFishRequestExit(uint32_t spotDoId);
    bool sendFishCast(uint32_t spotDoId, float power01, float headingDeg);
    bool sendFishSell(uint32_t spotDoId);
    bool sendFishHitTarget(uint32_t pondDoId, uint32_t targetDoId);
    // ---- battle section (docs/design/combat.md sections 3 to 7) ----
    // DistributedSuit.requestBattle(x, y, z, h, p, r): our toon touched this cog; the arguments are
    // the cog's position and heading. The AI answers with a DistributedBattle or denyBattle.
    bool sendRequestBattle(uint32_t suitDoId, float x, float y, float z, float h, float p, float r);
    const Battle* findBattle(uint32_t doId) const;
    const Battle& battleSlot(int i) const { return battles_[i]; }
    // The battle whose member list holds our toon (nullptr when we are not fighting).
    const Battle* localBattle() const;
    // DistributedToon.setBattleId of our toon (the AI also sets it to our own doId briefly).
    uint32_t ownBattleId() const { return ownBattleId_; }
    // clsend fields of DistributedBattle. All need InZone and return false otherwise.
    bool sendBattleFaceOffDone(uint32_t battleId);
    bool sendBattleRequestRun(uint32_t battleId);
    bool sendBattleTimeout(uint32_t battleId);
    bool sendBattleAdjustDone(uint32_t battleId);
    bool sendBattleMovieDone(uint32_t battleId);
    bool sendBattleRewardDone(uint32_t battleId);
    bool sendBattleRequestJoin(uint32_t battleId, float x, float y, float z); // relative to the battle
    bool sendBattleJoinDone(uint32_t battleId, uint32_t avId);
    // track/level from ToontownBattleGlobals; av = target cog doId (-1 for group attacks).
    // PASS is (98, -1, -1).
    bool sendBattleRequestAttack(uint32_t battleId, int8_t track, int8_t level, int32_t av);
    // ---- trolley and minigames (docs/design/trolley.md) ----
    const Trolley* trolley() const { return trolley_.active ? &trolley_ : nullptr; }
    const Minigame* minigame() const { return minigame_.active ? &minigame_ : nullptr; }
    const Purchase* purchase() const { return purchase_.active ? &purchase_ : nullptr; }
    // DistributedTrolley.requestBoard / requestExit (clsend). False when there is no trolley.
    bool sendTrolleyBoard();
    bool sendTrolleyExit();
    // ---- golf (docs/design/golf.md) ----
    const GolfKart* golfKart() const { return golfKart_.active ? &golfKart_ : nullptr; }
    const GolfCourse* golfCourse() const { return golfCourse_.active ? &golfCourse_ : nullptr; }
    const GolfHole* golfHole() const { return golfHole_.active ? &golfHole_ : nullptr; }
    bool sendGolfKartBoard();
    bool sendGolfKartExit();
    bool sendGolfReadyCourse();
    bool sendGolfReadyHole();
    bool sendGolfExitCourse();
    bool sendGolfDoneReward();
    bool sendGolfTee(uint8_t tee);
    bool sendGolfSwing(float cycleTime, int32_t power, float x, float y, float z, float dirX, float dirY);
    bool sendGolfTurnDone();
    bool sendGolfBallInHole();
    // DistributedMinigame base protocol (all clsend; docs/design/trolley.md 2.2).
    bool sendMinigameJoined();   // as soon as the DO exists
    bool sendMinigameReady();    // after the rules screen
    bool sendMinigameExited();   // after setGameExit, normal exits only
    bool sendMinigameExit();     // requestExit: abort the game now (pays 1..7 beans)
    // DistributedCatchGame.
    bool sendCatchClaim(uint32_t objNum, uint32_t typeId);
    bool sendCatchDone();
    bool sendCatchHitBySuit(uint32_t avId);
    // DistributedCannonGame.
    bool sendCannonPosition(float zRot, float angle);
    bool sendCannonLit(float zRot, float angle);
    bool sendCannonToonWillLandInWater(float landTime);
    // DistributedTugOfWarGame.
    bool sendTugPlayerReady(uint8_t side);
    bool sendTugKeyRate(uint32_t rate, float force);
    bool sendTugEndOfContest(uint8_t losingSide);
    bool sendTugNewAvIdList(const uint32_t* avIds, int count);
    // DistributedRingGame.
    bool sendRingToonGotRing(uint8_t success);
    // DistributedMazeGame.
    bool sendMazeClaimTreasure(uint32_t treasureNum);
    bool sendMazeHitBySuit(uint32_t avId, int16_t ts);
    // DistributedPairingGame.
    bool sendPairingOpenCard(int16_t deckIndex, int16_t bonusGlowCard);
    bool sendPairingDone();
    // DistributedDivingGame.
    bool sendDivingPickupTreasure(uint32_t chestId);
    bool sendDivingTreasureRecovered();
    bool sendDivingFishCollision(uint32_t avId, uint32_t spawnId, uint32_t spawnerId, const char* status);
    bool sendDivingCrabCollision(uint32_t avId, const char* status);
    bool sendDivingGetCrabMoving(uint32_t crabId, int16_t crabX, int8_t dir);
    // DistributedVineGame.
    bool sendVineNewVine(uint32_t avId, int8_t vineIndex, float vineT, int8_t facingRight);
    bool sendVineReachedEndVine(int8_t vineIndex);
    bool sendVineClaimTreasure(uint32_t treasureNum);
    // DistributedTargetGame.
    bool sendTargetScore(int32_t scoreX, int32_t scoreY);
    bool sendTargetPlayerDone();
    // DistributedCogThiefGame.
    bool sendCogThiefPie(int32_t timestamp, float x, float y, float z, float h);
    bool sendCogThiefHitBySuit(uint32_t avId, int32_t timestamp, int8_t suitNum, float x, float y, float z);
    bool sendCogThiefPieHitSuit(uint32_t avId, int32_t timestamp, int8_t suitNum, float x, float y, float z);
    bool sendCogThiefHitBarrel(int32_t clientStamp, int8_t cogIndex, int8_t barrelIndex, float x, float y, float z);
    bool sendCogThiefAtReturnPos(int32_t clientStamp, int8_t cogIndex, int8_t barrelIndex);
    // DistributedTwoDGame.
    bool sendTwoDClaimTreasure(uint8_t sectionIndex, uint8_t treasureIndex);
    bool sendTwoDClaimEnemyShot(uint8_t sectionIndex, uint8_t enemyIndex);
    bool sendTwoDReportDone();
    bool sendTwoDHitByEnemy(uint32_t avId, int16_t ts);
    bool sendTwoDFellDown(uint32_t avId, int16_t ts);
    bool sendTwoDSquished(uint32_t avId, int16_t ts);
    bool sendTwoDVictory(uint32_t avId, int16_t ts);
    // DistributedTagGame.
    bool sendTag(uint32_t taggedAvId);
    // DistributedPatternGame.
    bool sendPatternReady();
    bool sendPatternButton(uint8_t index, uint8_t wrong);
    bool sendPatternResult(const uint8_t* pattern, uint8_t len, uint16_t totalTimeMs);
    // DistributedIceGame.
    bool sendIceChoice(float force, float direction);
    bool sendIceEndingPositions(const float* coords, int count);
    bool sendIceScoringMovieDone();
    bool sendIceClaimTreasure(uint8_t n);
    bool sendIceClaimPenalty(uint8_t n);
    // DistributedRaceGame.
    bool sendRaceChoice(uint8_t choice);
    const RacingPad& racingPadSlot(int i) const { return racingPads_[i]; }
    const RacingBlock& racingBlockSlot(int i) const { return racingBlocks_[i]; }
    const RacingVehicle& racingVehicleSlot(int i) const { return racingVehicles_[i]; }
    const RacingRace* racingRace() const { return racingRace_.active ? &racingRace_ : nullptr; }
    bool sendRacingEnter(uint32_t blockDoId, bool paid = false);
    bool sendRacingExit(uint32_t blockDoId);
    bool sendRacingMovieDone(uint32_t blockDoId);
    bool sendRacingControl(uint32_t vehicleDoId);
    bool sendRacingPark(uint32_t vehicleDoId);
    bool sendRacingInput(uint32_t vehicleDoId, int8_t input);
    bool sendRacingThrow(uint32_t raceDoId, float x, float y, float z);
    bool sendRacingGag(uint32_t raceDoId, uint8_t track, uint8_t level, uint8_t count);
    // DistributedPhotoGame.
    bool sendPhotoScore(uint8_t subjectIndex, const char* pose, float score);
    bool sendPhotoFilmOut();
    // PurchaseManager: requestExit ("Back to Playground"), then setInventory(done=1) after
    // setPurchaseExit with the inventory and money the server last told us (the payout is
    // already in ownStats().money). False when there is no purchase manager.
    bool sendPurchaseExit();
    bool sendPurchaseInventory();
    bool sendPurchaseInventory(const uint8_t inv[7][7], int16_t money);
    // ==== quests (docs/design/quests.md) begin ====
    const OwnQuests& ownQuests() const { return ownQuests_; }
    // Oldest unread setMovie addressed to anyone in the zone (the game filters on avId).
    bool pollNpcMovie(NpcMovie& out);
    // DistributedNPCToonBase.avatarEnter (clsend): the toon walked into the NPC's sphere. Quest
    // givers only; the AI answers with setMovie. There is no avatarExit on NPC toons.
    bool sendNpcAvatarEnter(uint32_t npcDoId);
    // chooseQuest(uint16): a questId from the offered choices, 0 = cancel.
    bool sendChooseQuest(uint32_t npcDoId, uint16_t questId);
    // chooseTrack(int8): a track from the offered tracks, -1 = cancel.
    bool sendChooseTrack(uint32_t npcDoId, int8_t track);
    // setMovieDone (clsend): the dialog finished; the AI frees the NPC.
    bool sendNpcMovieDone(uint32_t npcDoId);
    // DistributedToon.requestDeleteQuest (ownsend): the AI only honors "just for fun" quests.
    bool sendDeleteQuest(int index);
    // ==== quests end ====
    // ===================== friends / whispers (docs/design/friends.md) =====================
    // Whispers and teleport queries only reach toons we currently see (Astron ejects updates to
    // unseen objects), so every send below checks findToon(). friendsAvailable() is false on
    // the stock server (no FriendManager is ever generated): requestFriend() then sets
    // RequestState::NoManager and sends nothing.
    bool friendsAvailable() const { return friends_.managerId != 0; }
    int numFriends() const { return friends_.numFriends; }
    const FriendInfo& friendAt(int i) const { return friends_.list[i]; }
    const FriendInfo* findFriend(uint32_t doId) const;
    bool isFriend(uint32_t doId) const { return findFriend(doId) != nullptr; }
    bool sendWhisperSC(uint32_t toDoId, uint16_t msgIndex);
    bool requestFriend(uint32_t avId);
    void cancelFriendRequest();          // cancelFriendQuery when in flight, then back to Idle
    void clearFriendRequest() { friends_.request = FriendRequest(); }
    const FriendRequest& friendRequest() const { return friends_.request; }
    const FriendInvite* pendingInvite() const { return friends_.invite.active ? &friends_.invite : nullptr; }
    bool answerInvite(bool accept);
    int numWhispers() const { return friends_.logCount; }
    const Whisper& whisperAt(int i) const { return friends_.log[(friends_.logHead + i) % kWhisperLogLen]; } // 0 = oldest
    bool sendTeleportQuery(uint32_t avId);
    void sendTeleportGiveup(uint32_t avId);
    const TeleportResult& teleportResult() const { return friends_.teleport; }
    void clearTeleportResult() { friends_.teleport = TeleportResult(); }
    int8_t notifyStatus() const { return friends_.notifyStatus; }
    bool pollFriendEvent(FriendEvent& out);
    // ===================== end friends API =====================

    // Clock: 16-bit centisecond network time (direct ClockDelta, bits=16).
    int16_t networkTime(uint32_t nowMs) const;
    uint32_t networkToLocal(int16_t networkTime, uint32_t nowMs) const;

    // Traffic counters (message bodies, excluding the u16 length prefix).
    uint32_t bytesIn() const { return bytesIn_; }
    uint32_t bytesOut() const { return bytesOut_; }
    uint32_t messagesIn() const { return msgsIn_; }
    uint32_t messagesOut() const { return msgsOut_; }

private:
    void setState(State s);
    void fail(const char* why, bool retry);
    void beginConnect();
    void onSessionStart();
    void resetWorld();
    void handleMessage(DatagramReader& r);
    void handleEnter(DatagramReader& r, uint16_t type);
    void handleSetField(DatagramReader& r, uint32_t doId, uint16_t fieldId);
    void handleLeaving(uint32_t doId);
    void handleInterestDone(uint32_t context, uint16_t handle);
    void handleLoginManagerField(DatagramReader& r, uint16_t fieldId);
    void onAvatarList();
    void playAvatar(const PotentialAvatar& pa);
    void maybeSkipTutorial();
    bool applyToonField(RemoteToon& t, DatagramReader& r, uint16_t fieldId);
    bool applyOwnField(DatagramReader& r, uint16_t fieldId);
    bool applyDistrictField(District& d, DatagramReader& r, uint16_t fieldId);
    void maybePickDistrict();
    void enterZone();
    void maybeFinishEnterZone();
    void pushEvent(EventType type, uint32_t doId);
    bool handleRacingEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                           uint16_t reqCount, bool other);
    bool handleRacingField(DatagramReader& r, uint32_t doId, uint16_t fieldId);
    void clearRacing();
    RemoteToon* toonById(uint32_t doId);
    District* districtById(uint32_t doId);

    // Outgoing. sendUpdate() is reachable only via the TT_NET_SEND_* macros in
    // client.cpp, which static_assert the clsend/ownsend whitelist.
    DatagramWriter beginMessage(uint16_t type);
    void beginUpdate(DatagramWriter& w, uint32_t doId, uint16_t fieldId);
    bool finish(DatagramWriter& w);
    void sendHello();
    void sendAddInterest(uint16_t handle, uint32_t parent, uint32_t zone);
    void sendAddInterestMultiple(uint16_t handle, uint32_t parent, const uint32_t* zones, int n);
    void dropZoneObjects();
    uint32_t targetZone_ = 0;
    uint32_t visZones_[kMaxVisZones] = {};
    int numVisZones_ = 0;
    uint32_t quietCtx_ = 0;
    bool quietDone_ = false;
    void sendSetLocation(uint32_t doId, uint32_t parent, uint32_t zone);
    void sendRequestLogin();
    void sendRequestAvatarList();
    void sendCreateAvatar(const uint8_t* dna, uint8_t slot);
    void sendRequestSkipTutorial();
    void sendRequestPlayAvatar(uint32_t avId);
    void sendRequestServerTime();

    Transport* transport_ = nullptr;
    ClientConfig cfg_;
    char host_[64] = {};
    char token_[128] = {};
    State state_ = State::Idle;
    const char* error_ = "";
    char errorBuf_[160] = {};
    uint16_t ejectCode_ = 0;
    uint32_t now_ = 0;
    uint32_t stateSince_ = 0;
    uint32_t session_ = 0;
    uint32_t backoffUntil_ = 0;
    uint32_t backoffMs_ = 0;
    bool fatal_ = false;

    RacingPad racingPads_[kMaxRacingPads] = {};
    RacingBlock racingBlocks_[kMaxRacingBlocks] = {};
    RacingVehicle racingVehicles_[kMaxRacingVehicles] = {};
    RacingRace racingRace_{};

    uint8_t* rx_ = nullptr;             // 64 KB message scratch (heap, once)
    uint8_t tx_[1024];

    // Session state.
    uint32_t nextContext_ = 1;
    uint32_t districtCtx_ = 0, uberCtx_ = 0, zoneCtx_ = 0;
    bool districtsDone_ = false, uberDone_ = false, zoneDone_ = false;
    uint32_t zoneReadyMs_ = 0;
    District districts_[kMaxDistricts];
    PotentialAvatar avatars_[kMaxAvatars];
    int numAvatars_ = 0;
    uint32_t avatarId_ = 0;
    bool ownerReceived_ = false;
    uint32_t shardId_ = 0;
    char ownName_[kNameLen] = {};
    float ownX_ = 0.0f, ownY_ = 0.0f, ownZ_ = 0.0f;
    OwnStats ownStats_{};
    NpcToon npcs_[kMaxNpcs] = {};
    Treasure treasures_[kMaxTreasures] = {};
    CChar cchars_[kMaxCChars] = {};
    bool handleCCharEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req, uint16_t reqCount,
                          bool other);
    bool applyCCharField(CChar& c, DatagramReader& r, uint16_t fieldId);
    Boat boat_ = {};
    bool applyBoatField(Boat& b, DatagramReader& r, uint16_t fieldId);
    void handleBoatEnter(uint32_t doId, DatagramReader& r, const uint16_t* req, uint16_t reqCount, bool other);
    bool handleNpcEnter(const dc::ClassDesc& cls, uint32_t doId, uint16_t classId, DatagramReader& r,
                        const uint16_t* req, uint16_t reqCount, bool other);
    bool handleTreasureEnter(uint32_t doId, DatagramReader& r, const uint16_t* req, uint16_t reqCount, bool other);
    void handleClerkMovie(DatagramReader& r, uint32_t doId);
    bool applyNpcField(NpcToon& n, DatagramReader& r, uint16_t fieldId);
    // quests section: see client.cpp
    static constexpr int kMovieQueue = 8;
    OwnQuests ownQuests_{};
    OwnBookStats ownBook_{};
    NpcMovie movies_[kMovieQueue];
    uint32_t movieHead_ = 0, movieTail_ = 0;
    bool handleNpcField(NpcToon& n, DatagramReader& r, uint16_t fieldId);
    bool applyTreasureField(Treasure& t, DatagramReader& r, uint16_t fieldId);
    Suit suits_[kMaxSuits] = {};
    SuitPlanner planners_[kMaxSuitPlanners] = {};
    bool handleSuitEnter(uint32_t doId, uint32_t zone, DatagramReader& r, const uint16_t* req, uint16_t reqCount,
                         bool other);
    bool applySuitField(Suit& s, DatagramReader& r, uint16_t fieldId);
    void clearSuits();
    ShopMovie shopMovie_{};
    TailorMovie tailorMovie_{};
    void handleTailorMovie(DatagramReader& r, uint32_t doId);
    // ---- Cog buildings (docs/design/cogbuildings.md; additive to the battle parser) ----
    bool handleCogBuildingEnter(uint32_t doId, DatagramReader& r, const uint16_t* req, uint16_t reqCount,
                                bool other);
    bool handleCogElevatorEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                                uint16_t reqCount, bool other);
    bool handleCogInteriorEnter(uint32_t doId, DatagramReader& r, const uint16_t* req, uint16_t reqCount,
                                bool other);
    bool applyCogBuildingField(CogBuilding& b, DatagramReader& r, uint16_t fieldId);
    bool applyCogElevatorField(CogElevator& e, DatagramReader& r, uint16_t fieldId);
    bool applyCogInteriorField(CogInterior& i, DatagramReader& r, uint16_t fieldId);
    bool handleCogField(DatagramReader& r, uint32_t doId, uint16_t fieldId);
    void clearCogObjects();
    bool handleEstateEnter(uint32_t doId, uint16_t classId, DatagramReader& r,
                           const uint16_t* req, uint16_t reqCount, bool other);
    bool handleEstateField(DatagramReader& r, uint32_t doId, uint16_t fieldId);
    bool applyEstateField(Estate& e, DatagramReader& r, uint16_t fieldId);
    bool applyHouseField(EstateHouse& h, DatagramReader& r, uint16_t fieldId);
    bool applyHouseInteriorField(HouseInterior& h, DatagramReader& r, uint16_t fieldId);
    bool applyFurnitureField(EstateFurniture& f, DatagramReader& r, uint16_t fieldId);
    bool applyPetField(Pet& p, DatagramReader& r, uint16_t fieldId);
    void clearEstateObjects();
    // ---- fishing (see the "fishing" section of client.cpp) ----
    FishingPond pond_ = {};
    FishingSpot fishSpots_[kMaxFishSpots] = {};
    FishMovieMsg fishMovie_ = {};
    bool fishSellTrophy_ = false;
    uint16_t fishSellCount_ = 0;
    OwnFish ownFish_;
    bool handleFishingEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                            uint16_t reqCount, bool other);
    bool applyFishSpotField(FishingSpot& s, DatagramReader& r, uint16_t fieldId);
    bool handleFishingField(DatagramReader& r, uint32_t doId, uint16_t fieldId);
    bool applyOwnFishField(DatagramReader& r, uint16_t fieldId);
    void handleFishingLeaving(uint32_t doId);
    void clearFishing();
    // ---- battle section: DistributedBattle parse state ----
    Battle battles_[kMaxBattles] = {};
    CogBuilding cogBuildings_[kMaxCogBuildings] = {};
    CogElevator cogElevators_[kMaxCogElevators] = {};
    CogInterior cogInterior_{};
    LevelObj levels_[kMaxLevels] = {};
    Facility facility_{};
    LevelElevator levelElevators_[kMaxLevelElevators] = {};
    LevelSuit levelSuits_[kMaxLevelSuits] = {};
    LevelBattlePlacer levelBattlePlacer_ = nullptr;
    void* levelBattlePlacerUser_ = nullptr;
    LevelBlocker levelBlockers_[kMaxLevelBlockers] = {};
    LevelEntityObj levelEntities_[kMaxLevelEntities] = {};
    uint32_t estateManagerDoId_ = 0, estateZone_ = 0;
    Estate estate_{};
    HouseInterior houseInterior_{};
    Closet closet_{};
    uint32_t mailboxDoId_ = 0, bankDoId_ = 0;
    uint32_t houseDoorDoId_ = 0;
    EstateFurniture furniture_[kMaxEstateFurniture] = {};
    Pet pets_[kMaxPets] = {};
    uint32_t ownBattleId_ = 0;
    bool handleBattleEnter(uint32_t doId, uint32_t zone, DatagramReader& r, const uint16_t* req, uint16_t reqCount,
                           bool other);
    bool applyBattleField(Battle& b, DatagramReader& r, uint16_t fieldId, bool live);
    bool parseBattleMembers(Battle& b, DatagramReader& r);
    bool parseBattleMovie(Battle& b, DatagramReader& r);
    bool parseBattleExperience(Battle& b, DatagramReader& r);
    void clearBattles();
    // ---- trolley and minigames ----
    Trolley trolley_ = {};
    GolfKart golfKart_ = {};
    GolfCourse golfCourse_ = {};
    GolfHole golfHole_ = {};
    Minigame minigame_ = {};
    Purchase purchase_ = {};
    bool handleTrolleyEnter(uint32_t doId, DatagramReader& r, const uint16_t* req, uint16_t reqCount, bool other);
    bool handleMinigameEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                             uint16_t reqCount, bool other);
    bool handlePurchaseEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                             uint16_t reqCount, bool other);
    bool applyTrolleyField(Trolley& t, DatagramReader& r, uint16_t fieldId, bool announce);
    bool applyMinigameField(Minigame& m, DatagramReader& r, uint16_t fieldId, bool announce);
    bool applyPurchaseField(Purchase& p, DatagramReader& r, uint16_t fieldId, bool announce);
    // Field update for the trolley / minigame / purchase DO `doId`; false when it is none of them.
    bool handleTrolleyField(DatagramReader& r, uint32_t doId, uint16_t fieldId);
    bool handleTrolleyLeaving(uint32_t doId);
    void clearTrolleyObjects();
    bool handleGolfEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                         uint16_t reqCount, bool other);
    bool applyGolfKartField(GolfKart& k, DatagramReader& r, uint16_t fieldId, bool announce);
    bool applyGolfCourseField(GolfCourse& c, DatagramReader& r, uint16_t fieldId, bool announce);
    bool applyGolfHoleField(GolfHole& h, DatagramReader& r, uint16_t fieldId, bool announce);
    bool handleGolfField(DatagramReader& r, uint32_t doId, uint16_t fieldId);
    bool handleGolfLeaving(uint32_t doId);
    bool handleLevelEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req, uint16_t reqCount,
                          bool other);
    bool handleLevelField(DatagramReader& r, uint32_t doId, uint16_t fieldId);
    bool handleLevelLeaving(uint32_t doId);
    void clearLevelObjects();
    bool applyLevelSuitField(LevelSuit& s, DatagramReader& r, uint16_t fieldId);
    bool applyLevelBlockerField(LevelBlocker& b, DatagramReader& r, uint16_t fieldId);
    bool applyLevelEntityField(LevelEntityObj& e, DatagramReader& r, uint16_t fieldId);
    bool applyLevelObjField(LevelObj& l, DatagramReader& r, uint16_t fieldId);
    bool applyFacilityField(Facility& f, DatagramReader& r, uint16_t fieldId);
    bool applyLevelElevatorField(LevelElevator& e, DatagramReader& r, uint16_t fieldId);
    void clearGolfObjects();
    void pushEventArg(EventType type, uint32_t doId, int32_t arg);
    uint8_t ownDna_[kDnaSize] = {};
    uint32_t timeManagerId_ = 0;
    uint32_t tutorialManagerId_ = 0;
    // Survive reconnects (not reset by resetWorld): which toon to re-play, and the
    // once-per-new-toon tutorial skip.
    uint32_t rejoinAvatarId_ = 0;
    uint32_t newAvatarId_ = 0;
    uint8_t pendingDna_[kDnaSize] = {};
    bool skipTutorialSent_ = false;
    int skipTutorialResult_ = -1;
    uint8_t timeContext_ = 0;
    uint32_t timeRequestMs_ = 0;
    int64_t clockDeltaMs_ = 0;          // local - network, in ms
    bool timeSynced_ = false;

    RemoteToon toons_[kMaxRemoteToons];
    int numToons_ = 0;
    uint32_t toonOverflow_ = 0;

    // ===================== friends (client_friends.cpp) =====================
    FriendsState friends_;
    bool friendsHandleEnter(const dc::ClassDesc& cls, DatagramReader& r, uint32_t doId, const uint16_t* req,
                            uint16_t reqCount, bool other);
    bool friendsHandleSetField(DatagramReader& r, uint32_t doId, uint16_t fieldId);
    bool friendsOwnField(DatagramReader& r, uint16_t fieldId);
    void friendsHandleLeaving(uint32_t doId);
    void friendsUpdate();
    void friendsReset();
    void friendsPush(FriendEventType type, uint32_t doId);
    void friendsLogWhisper(uint32_t otherId, WhisperKind kind, uint16_t index, const char* text,
                           const char* nameIfUnknown = nullptr);
    void friendsSetRequest(RequestState s);
    const char* friendsNameOf(uint32_t doId) const;
    void friendsAnswerTeleportQuery(uint32_t requesterId);
    // ===================== end friends (private) =====================

    Event events_[kEventQueue];
    uint32_t evHead_ = 0, evTail_ = 0;
    uint32_t droppedEvents_ = 0;

    uint32_t bytesIn_ = 0, bytesOut_ = 0, msgsIn_ = 0, msgsOut_ = 0;
    // ===================== boss protocol (client_boss.cpp) =====================
public:
    const BossState* boss() const { return boss_.active ? &boss_ : nullptr; }
    uint16_t bossPies() const { return bossPies_; }
    uint8_t bossPieType() const { return bossPieType_; }
    bool spendBossPie();
    bool sendBossEnter();
    bool sendBossExit();
    bool sendBossNear(bool near);
    bool sendBossBarrier(uint16_t context);
    bool sendBossBattleBarrier(uint32_t battleId, uint16_t context);
    bool sendVpTouchCage();
    bool sendVpHitInsides();
    bool sendVpHit();
    bool sendVpFinalPie();
    bool sendVpHitToon(uint32_t toonId);
    bool sendBossZap(float x, float y, float z, float heading, float directionX,
                     float directionY, uint8_t attack);
private:
    bool handleBossEnter(uint32_t doId, uint16_t classId, DatagramReader& reader,
                         const uint16_t* required, uint16_t count, bool other);
    bool handleBossField(DatagramReader& reader, uint32_t doId, uint16_t fieldId);
    bool bossOwnField(DatagramReader& reader, uint16_t fieldId);
    bool bossBattleField(Battle& battle, DatagramReader& reader, uint16_t fieldId);
    void positionBossBattle(Battle& battle);
    bool bossParticipant() const;
    BossState boss_;
    uint16_t bossPies_ = 0;
    uint8_t bossPieType_ = 0;
    // ===================== end boss protocol =====================

};

} // namespace tt::net
