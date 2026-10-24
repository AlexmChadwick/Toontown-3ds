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
constexpr int kEventQueue = 128;

constexpr uint32_t kTtcPlayground = 2000;
constexpr uint32_t kParentRender = 2; // OTPGlobals.SPRender (DistributedNode.setParent)

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
    uint32_t parentToken;  // setParent: 2 = render, 1 = hidden, 0 = unset
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
    void sendSetParent(uint32_t token);

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
