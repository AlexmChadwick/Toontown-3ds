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
    uint32_t parentToken;  // setParent: 3 = Donald's boat (pose is boat-local), 2 = render, 1 = hidden, 0 = unset
    char chat[kChatLen];   // last setTalk text (empty when the last chat was SpeedChat)
    int32_t scIndex;       // last setSC phrase index, or -1 when the last chat was typed
    uint32_t chatMs;
    bool active;
};

enum class EventType : uint8_t {
    Spawn,        // a remote toon entered our zone (fields already filled)
    Despawn,      // left; the RemoteToon slot is no longer valid
    Pose,         // new position sample (also flagged: RemoteToon::lastUpdateTick)
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
    BattleJoinDenied, // denyLocalToonJoin
    SuitDenied,       // DistributedSuit.denyBattle (doId = the cog we bumped into)
    SuitBrushOff,     // setBrushOff: the cog says no (doId = the cog)
};

struct Event {
    EventType type;
    uint32_t doId;
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
    uint32_t doId;
    uint32_t zoneId;
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

// Our toon's owner-view stats (DistributedToon setHp/setMaxHp/setMoney/...).
struct OwnStats {
    int16_t hp = 15, maxHp = 15;
    int16_t money = 0, maxMoney = 40, bankMoney = 0;
    // Gags (owner-only DistributedToon fields; tracks in ToontownBattleGlobals order).
    uint16_t experience[7] = {};
    uint8_t inventory[7][7] = {}; // count per track, level
    uint8_t trackAccess[7] = {0, 0, 0, 0, 1, 1, 0};
    uint8_t maxCarry = 20;
    bool hasGags = false; // setInventory received
};

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
    // DistributedTreasure.requestGrab (clsend): the AI answers with setGrab(avId) or setReject.
    bool sendRequestGrab(uint32_t treasureDoId);

    const NpcToon& npcSlot(int i) const { return npcs_[i]; }
    const NpcToon* findNpc(uint32_t doId) const;
    const CChar* findCChar(uint32_t doId) const;
    const Boat* boat() const { return boat_.active ? &boat_ : nullptr; }
    const Suit* findSuit(uint32_t doId) const;
    const Suit& suitSlot(int i) const { return suits_[i]; }
    // Branch zone of the suit planner with this doId (0 when unknown).
    uint32_t suitPlannerZone(uint32_t doId) const;
    const Treasure& treasureSlot(int i) const { return treasures_[i]; }
    const OwnStats& ownStats() const { return ownStats_; }

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
    bool applyTreasureField(Treasure& t, DatagramReader& r, uint16_t fieldId);
    Suit suits_[kMaxSuits] = {};
    SuitPlanner planners_[kMaxSuitPlanners] = {};
    bool handleSuitEnter(uint32_t doId, uint32_t zone, DatagramReader& r, const uint16_t* req, uint16_t reqCount,
                         bool other);
    bool applySuitField(Suit& s, DatagramReader& r, uint16_t fieldId);
    void clearSuits();
    ShopMovie shopMovie_{};
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
    uint32_t ownBattleId_ = 0;
    bool handleBattleEnter(uint32_t doId, uint32_t zone, DatagramReader& r, const uint16_t* req, uint16_t reqCount,
                           bool other);
    bool applyBattleField(Battle& b, DatagramReader& r, uint16_t fieldId, bool live);
    bool parseBattleMembers(Battle& b, DatagramReader& r);
    bool parseBattleMovie(Battle& b, DatagramReader& r);
    bool parseBattleExperience(Battle& b, DatagramReader& r);
    void clearBattles();
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

    Event events_[kEventQueue];
    uint32_t evHead_ = 0, evTail_ = 0;
    uint32_t droppedEvents_ = 0;

    uint32_t bytesIn_ = 0, bytesOut_ = 0, msgsIn_ = 0, msgsOut_ = 0;
};

} // namespace tt::net
