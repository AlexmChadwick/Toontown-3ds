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
};

struct Event {
    EventType type;
    uint32_t doId;
};

enum class State : uint8_t {
    Idle, Connecting, Hello, Login, Districts, AvatarList, CreateAvatar, PlayAvatar,
    EnterZone, InZone, Backoff, Failed,
};
const char* stateName(State s);

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
};

struct District {
    uint32_t doId;
    char name[kNameLen];
    bool available;
    bool active;
};

struct PotentialAvatar {
    uint32_t id;
    char name[kNameLen];
    uint8_t dna[kDnaSize];
    uint8_t slot;
};

class Client {
public:
    bool start(Transport& transport, const ClientConfig& cfg);
    void stop();                        // graceful CLIENT_DISCONNECT
    void update(uint32_t nowMs);        // pump messages, advance the state machine

    State state() const { return state_; }
    bool inZone() const { return state_ == State::InZone; }
    const char* lastError() const { return error_; }
    uint32_t avatarId() const { return avatarId_; }
    uint32_t shardId() const { return shardId_; }
    const char* avatarName() const { return ownName_; }
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
    // SpeedChat phrase (DistributedPlayer.setSC, ownsend broadcast).
    bool sendSpeedChat(uint16_t msgIndex);

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
    void sendSetLocation(uint32_t doId, uint32_t parent, uint32_t zone);
    void sendRequestLogin();
    void sendRequestAvatarList();
    void sendCreateAvatar();
    void sendRequestPlayAvatar(uint32_t avId);
    void sendRequestServerTime();
    void sendSetParent(uint32_t token);

    Transport* transport_ = nullptr;
    ClientConfig cfg_;
    char host_[64] = {};
    char token_[128] = {};
    State state_ = State::Idle;
    const char* error_ = "";
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
    uint8_t ownDna_[kDnaSize] = {};
    uint32_t timeManagerId_ = 0;
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
