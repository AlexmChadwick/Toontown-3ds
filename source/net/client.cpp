#include "net/client.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "net/msg_types.h"
#include "world/toon_dna.h"

namespace tt::net {

namespace {

constexpr uint32_t kLoginManagerId = 4670; // OTP_DO_ID_ASTRON_LOGIN_MANAGER
constexpr uint32_t kGameRootId = 4618;     // OTP_DO_ID_TOONTOWN
constexpr uint32_t kDistrictsZone = 3;     // OTP_ZONE_ID_DISTRICTS
constexpr uint32_t kManagementZone = 2;    // OTP_ZONE_ID_MANAGEMENT / OTPGlobals.UberZone
constexpr uint16_t kDistrictInterest = 1;
constexpr uint16_t kUberInterest = 2;
constexpr uint16_t kZoneInterest = 3;
constexpr uint32_t kQuietZone = 1; // OTPGlobals.QuietZone
constexpr uint32_t kStepTimeoutMs = 20000;
constexpr uint32_t kBackoffMinMs = 1000;
constexpr uint32_t kBackoffMaxMs = 30000;
constexpr uint32_t kRxBufferSize = 0x10000;
constexpr int kMaxMessagesPerUpdate = 256;
constexpr uint16_t kEjectLoggedInElsewhere = 100;
constexpr uint16_t kEjectBadDcHash = 124;       // CLIENT_DISCONNECT_BAD_DCHASH
constexpr uint16_t kEjectBadVersion = 125;      // CLIENT_DISCONNECT_BAD_VERSION
constexpr uint16_t kEjectHeartbeat = 345;       // CLIENT_DISCONNECT_NO_HEARTBEAT
constexpr uint32_t kTimeSyncWaitMs = 3000;

// ---- compile-time layout checks ------------------------------------------
// Every field parsed or built by hand below is pinned to the signature dcgen
// extracted from the .dc files; a .dc change that moves a type fails the build.
#define TT_SIG(cls, field, expect) \
    static_assert(dc::sigEq(dc::cls::sig_##field, expect), #cls "." #field " layout changed")
TT_SIG(AstronLoginManager, requestLogin, "string");
TT_SIG(AstronLoginManager, loginResponse, "blob");
TT_SIG(AstronLoginManager, requestAvatarList, "");
TT_SIG(AstronLoginManager, avatarListResponse, "{u32,string,blob,u8,u8}[]");
TT_SIG(AstronLoginManager, createAvatar, "blob,u8");
TT_SIG(AstronLoginManager, createAvatarResponse, "u32");
TT_SIG(AstronLoginManager, requestPlayAvatar, "u32");
TT_SIG(TutorialManager, requestSkipTutorial, "");
TT_SIG(TutorialManager, skipTutorialResponse, "u8");
TT_SIG(DistributedDistrict, setName, "string");
TT_SIG(DistributedDistrict, setAvailable, "u8");
TT_SIG(TimeManager, requestServerTime, "u8");
TT_SIG(TimeManager, serverTime, "u8,i32,u32");
TT_SIG(DistributedNode, setParent, "u32");
TT_SIG(DistributedToon, setName, "string");
TT_SIG(DistributedToon, setDNAString, "blob");
TT_SIG(DistributedToon, setAnimState, "string,i16/1000,i16");
TT_SIG(DistributedToon, setTalk, "u32,u32,string,string,{u16,u16}[],u8");
TT_SIG(DistributedToon, setHp, "i16");
TT_SIG(DistributedToon, setMaxHp, "i16");
TT_SIG(DistributedToon, setMoney, "i16");
TT_SIG(DistributedToon, setMaxMoney, "i16");
TT_SIG(DistributedToon, setBankMoney, "i16");
TT_SIG(DistributedToon, setExperience, "blob");
TT_SIG(DistributedToon, setMaxCarry, "u8");
TT_SIG(DistributedToon, setInventory, "blob");
TT_SIG(DistributedToon, setTrackAccess, "u16[]");
TT_SIG(DistributedNPCToonBase, setName, "string");
TT_SIG(DistributedNPCToonBase, setDNAString, "blob");
TT_SIG(DistributedNPCToonBase, setPositionIndex, "u8");
TT_SIG(DistributedTreasure, setPosition, "i16/10,i16/10,i16/10");
TT_SIG(DistributedTreasure, requestGrab, "");
TT_SIG(DistributedTreasure, setGrab, "u32");
TT_SIG(DistributedCCharBase, setWalk, "string,string,i16");
static_assert(dc::ToontownDistrict::k_setName == dc::DistributedDistrict::k_setName, "district fields");
static_assert(dc::ToontownDistrict::k_setAvailable == dc::DistributedDistrict::k_setAvailable, "district fields");
static_assert(kDnaSize == int(world::kDnaNetStringSize), "DNA net string size");
#undef TT_SIG

// Outgoing whitelist (AGENTS.md "Anti-cheat"): the only way to build a field
// update is through these macros. Updates to objects we don't own must be
// clsend; updates to our own avatar must be clsend or ownsend.
#define TT_NET_UPDATE(w, doId, cls, field)                                                  \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " is not clsend"); \
    beginUpdate(w, doId, dc::cls::k_##field)
#define TT_NET_OWN_UPDATE(w, cls, field)                                                    \
    static_assert((dc::cls::kw_##field & (dc::kKwClsend | dc::kKwOwnsend)) != 0,             \
                  #cls "." #field " is not clsend/ownsend");                                 \
    beginUpdate(w, avatarId_, dc::cls::k_##field)

// Pose fields: one letter per component. X/Y/Z = int16/10, H/P/R =
// int16%360/10, T = int16 timestamp, L = uint64 (ignored).
struct PoseField {
    uint16_t id;
    const char* comps;
    const char* sig;
};

#define TT_POSE(cls, field, comps) {dc::cls::k_##field, comps, dc::cls::sig_##field}
constexpr PoseField kPoseFields[] = {
    TT_POSE(DistributedSmoothNode, setSmStop, "T"),
    TT_POSE(DistributedSmoothNode, setSmH, "HT"),
    TT_POSE(DistributedSmoothNode, setSmZ, "ZT"),
    TT_POSE(DistributedSmoothNode, setSmXY, "XYT"),
    TT_POSE(DistributedSmoothNode, setSmXZ, "XZT"),
    TT_POSE(DistributedSmoothNode, setSmPos, "XYZT"),
    TT_POSE(DistributedSmoothNode, setSmHpr, "HPRT"),
    TT_POSE(DistributedSmoothNode, setSmXYH, "XYHT"),
    TT_POSE(DistributedSmoothNode, setSmXYZH, "XYZHT"),
    TT_POSE(DistributedSmoothNode, setSmPosHpr, "XYZHPRT"),
    TT_POSE(DistributedSmoothNode, setSmPosHprL, "LXYZHPRT"),
    TT_POSE(DistributedSmoothNode, setComponentL, "L"),
    TT_POSE(DistributedSmoothNode, setComponentX, "X"),
    TT_POSE(DistributedSmoothNode, setComponentY, "Y"),
    TT_POSE(DistributedSmoothNode, setComponentZ, "Z"),
    TT_POSE(DistributedSmoothNode, setComponentH, "H"),
    TT_POSE(DistributedSmoothNode, setComponentP, "P"),
    TT_POSE(DistributedSmoothNode, setComponentR, "R"),
    TT_POSE(DistributedSmoothNode, setComponentT, "T"),
    TT_POSE(DistributedNode, setX, "X"),
    TT_POSE(DistributedNode, setY, "Y"),
    TT_POSE(DistributedNode, setZ, "Z"),
    TT_POSE(DistributedNode, setH, "H"),
    TT_POSE(DistributedNode, setP, "P"),
    TT_POSE(DistributedNode, setR, "R"),
    TT_POSE(DistributedNode, setPos, "XYZ"),
    TT_POSE(DistributedNode, setHpr, "HPR"),
    TT_POSE(DistributedNode, setPosHpr, "XYZHPR"),
    TT_POSE(DistributedNode, setXY, "XY"),
    TT_POSE(DistributedNode, setXZ, "XZ"),
    TT_POSE(DistributedNode, setXYH, "XYH"),
    TT_POSE(DistributedNode, setXYZH, "XYZH"),
};
#undef TT_POSE

constexpr const char* compSig(char c) {
    return (c == 'X' || c == 'Y' || c == 'Z') ? "i16/10"
         : (c == 'H' || c == 'P' || c == 'R') ? "i16%360/10"
         : c == 'T' ? "i16"
         : c == 'L' ? "u64" : "?";
}

// Does sig equal the comma-joined compSig() of every letter in comps?
constexpr bool poseSigOk(const char* sig, const char* comps) {
    for (; *comps; ++comps) {
        for (const char* p = compSig(*comps); *p; ++p, ++sig) {
            if (*sig != *p) return false;
        }
        if (comps[1]) {
            if (*sig != ',') return false;
            ++sig;
        }
    }
    return *sig == 0;
}

constexpr bool allPoseSigsOk() {
    for (const PoseField& f : kPoseFields) {
        if (!poseSigOk(f.sig, f.comps)) return false;
    }
    return true;
}
static_assert(allPoseSigsOk(), "a DistributedNode/SmoothNode pose field changed layout");

const PoseField* findPoseField(uint16_t id) {
    for (const PoseField& f : kPoseFields) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

int16_t packFixed(float v, float scale) {
    float s = std::floor(v * scale + 0.5f);
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    return int16_t(s);
}

int16_t packAngle(float deg) {
    float a = std::fmod(deg, 360.0f);
    if (a < 0.0f) a += 360.0f;
    return packFixed(a, 10.0f);
}

void copyBytes(char* dst, uint32_t cap, Bytes b) {
    uint32_t n = b.len < cap - 1 ? b.len : cap - 1;
    if (n) memcpy(dst, b.data, n);
    dst[n] = 0;
}

// Finds "key" in a JSON blob and parses the integer after the colon.
bool jsonInt(Bytes json, const char* key, long* out) {
    uint32_t klen = uint32_t(strlen(key));
    for (uint32_t i = 0; i + klen + 2 <= json.len; ++i) {
        if (json.data[i] != '"' || memcmp(json.data + i + 1, key, klen) != 0 ||
            json.data[i + 1 + klen] != '"') {
            continue;
        }
        uint32_t j = i + klen + 2;
        while (j < json.len && (json.data[j] == ' ' || json.data[j] == ':')) ++j;
        char num[16];
        uint32_t n = 0;
        while (j < json.len && n < sizeof(num) - 1 &&
               (json.data[j] == '-' || (json.data[j] >= '0' && json.data[j] <= '9'))) {
            num[n++] = char(json.data[j++]);
        }
        num[n] = 0;
        if (n == 0) return false;
        *out = strtol(num, nullptr, 10);
        return true;
    }
    return false;
}

int64_t ticksOf(int64_t ms) { // round(ms / 10), floor semantics like ClockDelta
    int64_t t = ms + 5;
    return t >= 0 ? t / 10 : -((-t + 9) / 10);
}

} // namespace

const char* stateName(State s) {
    switch (s) {
    case State::Idle: return "idle";
    case State::Connecting: return "connecting";
    case State::Hello: return "hello";
    case State::Login: return "login";
    case State::Districts: return "districts";
    case State::AvatarList: return "avatar list";
    case State::ChooseAvatar: return "choose avatar";
    case State::CreateAvatar: return "create avatar";
    case State::PlayAvatar: return "play avatar";
    case State::EnterZone: return "entering zone";
    case State::InZone: return "in zone";
    case State::ChangingZone: return "changing zone";
    case State::Backoff: return "reconnect wait";
    case State::Failed: return "failed";
    }
    return "?";
}

// ============================================================== lifecycle
bool Client::start(Transport& transport, const ClientConfig& cfg) {
    if (!rx_) {
        rx_ = static_cast<uint8_t*>(malloc(kRxBufferSize));
        if (!rx_) { error_ = "out of memory"; return false; }
    }
    transport_ = &transport;
    now_ = plat::nowMs();
    cfg_ = cfg;
    strncpy(host_, cfg.host, sizeof(host_) - 1);
    strncpy(token_, cfg.playToken, sizeof(token_) - 1);
    cfg_.host = host_;
    cfg_.playToken = token_;
    fatal_ = false;
    backoffMs_ = 0;
    error_ = "";
    ejectCode_ = 0;
    rejoinAvatarId_ = 0;
    resetWorld();
    beginConnect();
    return true;
}

bool Client::connect() {
    if (!transport_ || !rx_) return false;
    if (state_ != State::Idle && state_ != State::Failed) return false;
    now_ = plat::nowMs();
    fatal_ = false;
    backoffMs_ = 0;
    error_ = "";
    ejectCode_ = 0;
    rejoinAvatarId_ = 0;
    resetWorld();
    beginConnect();
    return true;
}

void Client::forceReconnect(const char* why) {
    if (!transport_ || state_ == State::Idle || state_ == State::Failed) return;
    fail(why, true);
}

void Client::disconnect() {
    if (!transport_) return;
    if (state_ != State::Idle && state_ != State::Connecting && state_ != State::Backoff &&
        state_ != State::Failed) {
        DatagramWriter w = beginMessage(msg::CLIENT_DISCONNECT);
        finish(w);
    }
    transport_->disconnect();
    resetWorld();
    rejoinAvatarId_ = 0;
    setState(State::Idle);
}

void Client::stop() {
    if (!transport_) return;
    if (state_ != State::Idle && state_ != State::Connecting && state_ != State::Backoff &&
        state_ != State::Failed) {
        DatagramWriter w = beginMessage(msg::CLIENT_DISCONNECT);
        finish(w);
    }
    transport_->disconnect();
    resetWorld();
    setState(State::Idle);
    free(rx_);
    rx_ = nullptr;
}

void Client::setState(State s) {
    if (s == state_) return;
    TT_LOG("net: %s -> %s", stateName(state_), stateName(s));
    state_ = s;
    stateSince_ = now_;
}

void Client::beginConnect() {
    TT_LOG("net: connecting to %s:%u", host_, unsigned(cfg_.port));
    session_ = transport_->session();
    transport_->connect(host_, cfg_.port);
    setState(State::Connecting);
}

void Client::fail(const char* why, bool retry) {
    if (why != errorBuf_) {
        strncpy(errorBuf_, why, sizeof(errorBuf_) - 1);
        errorBuf_[sizeof(errorBuf_) - 1] = 0;
    }
    error_ = errorBuf_;
    TT_LOG("net: error in %s: %s", stateName(state_), why);
    transport_->disconnect();
    resetWorld();
    if (!retry || fatal_) {
        setState(State::Failed);
        return;
    }
    backoffMs_ = backoffMs_ == 0 ? kBackoffMinMs : (backoffMs_ * 2 > kBackoffMaxMs ? kBackoffMaxMs : backoffMs_ * 2);
    backoffUntil_ = now_ + backoffMs_;
    TT_LOG("net: retry in %u ms", unsigned(backoffMs_));
    setState(State::Backoff);
}

void Client::resetWorld() {
    bool hadWorld = state_ == State::InZone || numToons_ > 0;
    for (RemoteToon& t : toons_) {
        if (t.active) pushEvent(EventType::Despawn, t.doId);
        t.active = false;
    }
    numToons_ = 0;
    for (NpcToon& n : npcs_) {
        if (n.active) pushEvent(EventType::NpcDespawn, n.doId);
        n.active = false;
    }
    for (Treasure& t : treasures_) {
        if (t.active) pushEvent(EventType::TreasureGone, t.doId);
        t.active = false;
    }
    if (hadWorld) pushEvent(EventType::LeftZone, 0);
    for (District& d : districts_) d.active = false;
    numAvatars_ = 0;
    avatarId_ = 0;
    ownerReceived_ = false;
    shardId_ = 0;
    timeManagerId_ = 0;
    tutorialManagerId_ = 0;
    timeSynced_ = false;
    districtCtx_ = uberCtx_ = zoneCtx_ = 0;
    districtsDone_ = uberDone_ = zoneDone_ = false;
    zoneReadyMs_ = 0;
}

void Client::onSessionStart() {
    session_ = transport_->session();
    nextContext_ = 1;
    ejectCode_ = 0;
    sendHello();
    setState(State::Hello);
}

// ============================================================== main pump
void Client::update(uint32_t nowMs) {
    now_ = nowMs;
    if (!transport_) return;
    switch (state_) {
    case State::Idle:
    case State::Failed:
        return;
    case State::Backoff:
        if (int32_t(now_ - backoffUntil_) >= 0) beginConnect();
        return;
    case State::Connecting: {
        Transport::Status st = transport_->status();
        if (st == Transport::Status::Connected && transport_->session() != session_) {
            onSessionStart();
        } else if (st == Transport::Status::Disconnected) {
            fail(transport_->lastError()[0] ? transport_->lastError() : "connect failed", true);
            return;
        } else {
            if (now_ - stateSince_ > 10000) fail("connect timed out", true);
            return;
        }
        break;
    }
    default:
        break;
    }

    if (transport_->status() != Transport::Status::Connected || transport_->session() != session_) {
        fail(transport_->lastError()[0] ? transport_->lastError() : "connection lost", true);
        return;
    }

    for (int i = 0; i < kMaxMessagesPerUpdate; ++i) {
        int n = transport_->recvMessage(rx_, kRxBufferSize);
        if (n == 0) break;
        if (n < 0) { TT_LOG("net: dropped oversize message"); continue; }
        bytesIn_ += uint32_t(n);
        ++msgsIn_;
        DatagramReader r(rx_, uint32_t(n));
        handleMessage(r);
        if (state_ == State::Backoff || state_ == State::Failed || state_ == State::Idle) return;
    }

    maybeFinishEnterZone();
    // ChooseAvatar waits for the player; the heartbeat keeps the session alive.
    if (state_ != State::InZone && state_ != State::ChooseAvatar && now_ - stateSince_ > kStepTimeoutMs) {
        fail("server did not answer (timeout)", true);
    }
}

void Client::handleMessage(DatagramReader& r) {
    uint16_t type = r.u16();
    switch (type) {
    case msg::CLIENT_HELLO_RESP:
        if (state_ == State::Hello) {
            transport_->setHeartbeat(true);
            sendRequestLogin();
            setState(State::Login);
        }
        break;
    case msg::CLIENT_EJECT: {
        uint16_t code = r.u16();
        char reason[96];
        r.string(reason, sizeof(reason));
        TT_LOG("net: ejected (%u): %s", unsigned(code), reason);
        // Retrying cannot fix these; everything else (e.g. 122 from the login
        // manager for a taken slot or bad DNA) reconnects and starts over.
        if (code == kEjectLoggedInElsewhere || code == kEjectBadDcHash || code == kEjectBadVersion) fatal_ = true;
        if (code == kEjectLoggedInElsewhere) {
            snprintf(errorBuf_, sizeof(errorBuf_), "This account was logged in somewhere else.");
        } else if (code == kEjectBadDcHash || code == kEjectBadVersion) {
            snprintf(errorBuf_, sizeof(errorBuf_), "This client does not match the server version (code %u).",
                     unsigned(code));
        } else if (code == kEjectHeartbeat) {
            snprintf(errorBuf_, sizeof(errorBuf_), "The server timed out waiting for us.");
        } else {
            snprintf(errorBuf_, sizeof(errorBuf_), "Disconnected by the server (%u): %s", unsigned(code),
                     reason[0] ? reason : "no reason given");
        }
        fail(errorBuf_, true);
        ejectCode_ = code;
        break;
    }
    case msg::CLIENT_HEARTBEAT:
        break;
    case msg::CLIENT_ENTER_OBJECT_REQUIRED:
    case msg::CLIENT_ENTER_OBJECT_REQUIRED_OTHER:
    case msg::CLIENT_ENTER_OBJECT_REQUIRED_OWNER:
    case msg::CLIENT_ENTER_OBJECT_REQUIRED_OTHER_OWNER:
        handleEnter(r, type);
        break;
    case msg::CLIENT_OBJECT_SET_FIELD: {
        uint32_t doId = r.u32();
        uint16_t fieldId = r.u16();
        if (r.ok()) handleSetField(r, doId, fieldId);
        break;
    }
    case msg::CLIENT_OBJECT_SET_FIELDS: {
        uint32_t doId = r.u32();
        uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && r.ok(); ++i) {
            uint16_t fieldId = r.u16();
            uint32_t before = r.pos();
            handleSetField(r, doId, fieldId);
            if (r.pos() == before && !skipField(r, fieldId)) break;
        }
        break;
    }
    case msg::CLIENT_OBJECT_LEAVING:
    case msg::CLIENT_OBJECT_LEAVING_OWNER: {
        uint32_t doId = r.u32();
        if (doId == avatarId_ && avatarId_ != 0) {
            fail("our avatar was removed by the server", true);
            return;
        }
        handleLeaving(doId);
        break;
    }
    case msg::CLIENT_OBJECT_LOCATION: {
        uint32_t doId = r.u32();
        uint32_t parent = r.u32();
        uint32_t zone = r.u32();
        RemoteToon* t = toonById(doId);
        if (t && r.ok()) {
            t->parentId = parent;
            t->zoneId = zone;
            if (parent != shardId_ || zone != cfg_.zoneId) handleLeaving(doId);
        }
        break;
    }
    case msg::CLIENT_DONE_INTEREST_RESP: {
        uint32_t ctx = r.u32();
        uint16_t handle = r.u16();
        if (r.ok()) handleInterestDone(ctx, handle);
        break;
    }
    default:
        TT_LOG("net: ignoring message type %u", unsigned(type));
        break;
    }
}

// ============================================================== objects
void Client::handleEnter(DatagramReader& r, uint16_t type) {
    uint32_t doId = r.u32();
    uint32_t parent = r.u32();
    uint32_t zone = r.u32();
    uint16_t classId = r.u16();
    if (!r.ok()) return;
    const bool owner = type == msg::CLIENT_ENTER_OBJECT_REQUIRED_OWNER ||
                       type == msg::CLIENT_ENTER_OBJECT_REQUIRED_OTHER_OWNER;
    const bool other = type == msg::CLIENT_ENTER_OBJECT_REQUIRED_OTHER ||
                       type == msg::CLIENT_ENTER_OBJECT_REQUIRED_OTHER_OWNER;
    const dc::ClassDesc* cls = findClass(classId);
    if (!cls) return; // a class we never look at: the whole message is dropped
    const uint16_t* req = owner ? requiredOwner(*cls) : requiredOther(*cls);
    const uint16_t reqCount = owner ? cls->ownerCount : cls->otherCount;

    if (classId == dc::DistributedToon::kClassId) {
        if (doId == avatarId_) {
            if (!owner) return; // our own toon seen through an interest
            for (uint16_t i = 0; i < reqCount && r.ok(); ++i) applyOwnField(r, req[i]);
            if (other) {
                uint16_t n = r.u16();
                for (uint16_t i = 0; i < n && r.ok(); ++i) applyOwnField(r, r.u16());
            }
            if (!r.ok()) TT_LOG("net: owner view of %u did not parse", unsigned(doId));
            ownerReceived_ = true;
            TT_LOG("net: playing as %u \"%s\"", unsigned(doId), ownName_);
            if (state_ == State::PlayAvatar) {
                setState(State::EnterZone);
                maybePickDistrict();
            }
            maybeSkipTutorial();
            return;
        }
        RemoteToon* t = toonById(doId);
        bool fresh = t == nullptr;
        if (!t) {
            for (RemoteToon& s : toons_) {
                if (!s.active) { t = &s; break; }
            }
            if (!t) {
                ++toonOverflow_;
                return;
            }
            memset(t, 0, sizeof(*t));
            t->scIndex = -1;
            t->doId = doId;
            t->animRate = 1.0f;
            strcpy(t->animState, "neutral");
        }
        t->parentId = parent;
        t->zoneId = zone;
        t->sampleMs = t->lastUpdateTick = now_;
        bool okParse = true;
        for (uint16_t i = 0; i < reqCount && okParse; ++i) okParse = applyToonField(*t, r, req[i]);
        if (okParse && other) {
            uint16_t n = r.u16();
            for (uint16_t i = 0; i < n && okParse && r.ok(); ++i) okParse = applyToonField(*t, r, r.u16());
        }
        if (!okParse || !r.ok()) {
            TT_LOG("net: toon %u generate did not parse", unsigned(doId));
            if (fresh) return; // slot stays inactive
        }
        if (fresh) {
            t->active = true;
            ++numToons_;
            TT_LOG("net: toon %u \"%s\" entered (%d here)", unsigned(doId), t->name, numToons_);
            pushEvent(EventType::Spawn, doId);
        }
        return;
    }

    if (classId == dc::ToontownDistrict::kClassId || classId == dc::DistributedDistrict::kClassId) {
        District* d = districtById(doId);
        if (!d) {
            for (District& s : districts_) {
                if (!s.active) { d = &s; break; }
            }
            if (!d) return;
            memset(d, 0, sizeof(*d));
            d->doId = doId;
        }
        for (uint16_t i = 0; i < reqCount && r.ok(); ++i) applyDistrictField(*d, r, req[i]);
        if (other) {
            uint16_t n = r.u16();
            for (uint16_t i = 0; i < n && r.ok(); ++i) applyDistrictField(*d, r, r.u16());
        }
        d->active = true;
        TT_LOG("net: district %u \"%s\"%s", unsigned(doId), d->name, d->available ? "" : " (unavailable)");
        if (state_ == State::EnterZone) maybePickDistrict();
        return;
    }

    if (classId == dc::TimeManager::kClassId) {
        timeManagerId_ = doId;
        if (avatarId_) sendRequestServerTime();
        return;
    }

    if (classId == dc::TutorialManager::kClassId) {
        tutorialManagerId_ = doId; // arrives with the (shard, 2) interest
        maybeSkipTutorial();
        return;
    }
    // Families are recognized by their base class's fields (every subclass inherits them).
    if (classHasField(*cls, dc::DistributedNPCToonBase::k_setPositionIndex)) {
        handleNpcEnter(*cls, doId, classId, r, req, reqCount, other);
        return;
    }
    if (classHasField(*cls, dc::DistributedTreasure::k_requestGrab)) {
        handleTreasureEnter(doId, r, req, reqCount, other);
        return;
    }
    if (classHasField(*cls, dc::DistributedCCharBase::k_setWalk)) {
        handleCCharEnter(doId, classId, r, req, reqCount, other);
        return;
    }
}

void Client::maybeSkipTutorial() {
    // Once per toon created in this session, after requestPlayAvatar (the AI looks
    // our toon up, or waits for it, so it must have been activated already).
    if (skipTutorialSent_ || tutorialManagerId_ == 0 || !playingNewAvatar() || !ownerReceived_) return;
    sendRequestSkipTutorial();
    skipTutorialSent_ = true;
    TT_LOG("net: requested tutorial skip for new toon %u", unsigned(avatarId_));
}

void Client::handleSetField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    if (doId == kLoginManagerId) {
        handleLoginManagerField(r, fieldId);
        return;
    }
    for (Treasure& t : treasures_) {
        if (t.active && t.doId == doId) {
            if (fieldId == dc::DistributedTreasure::k_setGrab) {
                const uint32_t who = r.u32();
                if (!r.ok()) return;
                t.active = false;
                if (who == avatarId_) TT_LOG("net: grabbed treasure %u", unsigned(doId));
                pushEvent(EventType::TreasureGone, doId);
            }
            return;
        }
    }
    for (CChar& c : cchars_) {
        if (c.active && c.doId == doId) {
            if (fieldId == dc::DistributedCCharBase::k_setWalk) {
                if (applyCCharField(c, r, fieldId)) pushEvent(EventType::CCharWalk, doId);
            }
            return; // setChat / fadeAway / Dale's setFollowChip: not shown yet
        }
    }
    if (doId == tutorialManagerId_ && tutorialManagerId_ != 0) {
        if (fieldId == dc::TutorialManager::k_skipTutorialResponse) {
            uint8_t ok = r.u8();
            if (!r.ok()) return;
            skipTutorialResult_ = ok ? 1 : 0;
            TT_LOG("net: skipTutorialResponse %u", unsigned(ok));
        }
        return;
    }
    if (doId == timeManagerId_ && timeManagerId_ != 0) {
        if (fieldId == dc::TimeManager::k_serverTime) {
            uint8_t ctx = r.u8();
            int32_t ts = r.i32();
            r.u32(); // time of day
            if (!r.ok() || ctx != timeContext_) return;
            int64_t avgLocal = (int64_t(timeRequestMs_) + int64_t(now_)) / 2;
            clockDeltaMs_ = avgLocal - int64_t(ts) * 10;
            timeSynced_ = true;
            TT_LOG("net: clock synced, rtt %u ms", unsigned(now_ - timeRequestMs_));
            maybeFinishEnterZone();
        }
        return;
    }
    if (doId == avatarId_ && avatarId_ != 0) {
        applyOwnField(r, fieldId);
        return;
    }
    if (RemoteToon* t = toonById(doId)) {
        if (!applyToonField(*t, r, fieldId)) TT_LOG("net: bad field %u for toon %u", unsigned(fieldId), unsigned(doId));
        return;
    }
    if (District* d = districtById(doId)) {
        applyDistrictField(*d, r, fieldId);
        if (state_ == State::EnterZone) maybePickDistrict();
    }
}

void Client::handleLeaving(uint32_t doId) {
    if (RemoteToon* t = toonById(doId)) {
        t->active = false;
        --numToons_;
        TT_LOG("net: toon %u left (%d here)", unsigned(doId), numToons_);
        pushEvent(EventType::Despawn, doId);
        return;
    }
    if (District* d = districtById(doId)) {
        d->active = false;
        if (doId == shardId_) fail("our district went away", true);
        return;
    }
    if (doId == timeManagerId_) timeManagerId_ = 0;
    if (doId == tutorialManagerId_) tutorialManagerId_ = 0;
    for (NpcToon& n : npcs_) {
        if (n.active && n.doId == doId) {
            n.active = false;
            pushEvent(EventType::NpcDespawn, doId);
        }
    }
    for (Treasure& t : treasures_) {
        if (t.active && t.doId == doId) {
            t.active = false;
            pushEvent(EventType::TreasureGone, doId);
        }
    }
    for (CChar& c : cchars_) {
        if (c.active && c.doId == doId) {
            c.active = false;
            pushEvent(EventType::CCharGone, doId);
        }
    }
}

void Client::handleInterestDone(uint32_t context, uint16_t handle) {
    (void)handle;
    if (context == districtCtx_ && !districtsDone_) {
        districtsDone_ = true;
        if (state_ == State::Districts) {
            sendRequestAvatarList();
            setState(State::AvatarList);
        }
    } else if (context == uberCtx_) {
        uberDone_ = true;
    } else if (context == zoneCtx_) {
        zoneDone_ = true;
    } else if (context == quietCtx_ && state_ == State::ChangingZone && !quietDone_) {
        // In the quiet zone: everything from the old zone is gone. Now open the new one.
        quietDone_ = true;
        cfg_.zoneId = targetZone_;
        sendSetLocation(avatarId_, shardId_, targetZone_);
        zoneCtx_ = nextContext_;
        zoneDone_ = false;
        uint32_t zones[kMaxVisZones + 1];
        int n = 0;
        zones[n++] = targetZone_;
        for (int i = 0; i < numVisZones_; ++i) {
            if (visZones_[i] != targetZone_) zones[n++] = visZones_[i];
        }
        if (n == 1) sendAddInterest(kZoneInterest, shardId_, targetZone_);
        else sendAddInterestMultiple(kZoneInterest, shardId_, zones, n);
    }
    if (state_ == State::ChangingZone && quietDone_ && zoneDone_) {
        setState(State::InZone);
        TT_LOG("net: now in zone %u", unsigned(cfg_.zoneId));
        pushEvent(EventType::EnteredZone, avatarId_);
        return;
    }
    if (state_ == State::EnterZone && uberDone_ && zoneDone_ && zoneReadyMs_ == 0) {
        zoneReadyMs_ = now_ ? now_ : 1;
        if (timeManagerId_ != 0 && !timeSynced_) TT_LOG("net: waiting for clock sync");
    }
    maybeFinishEnterZone();
}

void Client::maybeFinishEnterZone() {
    // Movement timestamps are only meaningful once the clock is synced, so
    // hold InZone until TimeManager answered (or kTimeSyncWaitMs passed).
    if (state_ != State::EnterZone || zoneReadyMs_ == 0) return;
    if (!timeSynced_ && now_ - zoneReadyMs_ < kTimeSyncWaitMs) return;
    sendSetParent(kParentRender);
    backoffMs_ = 0;
    error_ = "";
    setState(State::InZone);
    TT_LOG("net: in zone %u of district %u with %d toons", unsigned(cfg_.zoneId),
           unsigned(shardId_), numToons_);
    pushEvent(EventType::EnteredZone, avatarId_);
}

void Client::handleLoginManagerField(DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::AstronLoginManager::k_loginResponse) {
        Bytes json = r.blob();
        long rc = -1;
        if (!r.ok() || !jsonInt(json, "returnCode", &rc)) {
            fail("bad loginResponse", true);
            return;
        }
        if (rc != 0) {
            snprintf(errorBuf_, sizeof(errorBuf_), "The server rejected the login (code %ld).", rc);
            // The same token is rejected again; only automatic mode keeps retrying.
            fail(errorBuf_, !cfg_.manualAvatar);
            return;
        }
        if (state_ != State::Login) return;
        districtCtx_ = nextContext_;
        sendAddInterest(kDistrictInterest, kGameRootId, kDistrictsZone);
        setState(State::Districts);
    } else if (fieldId == dc::AstronLoginManager::k_avatarListResponse) {
        Bytes list = r.blob();
        if (!r.ok()) { fail("bad avatarListResponse", true); return; }
        DatagramReader a(list.data, list.len);
        numAvatars_ = 0;
        while (a.ok() && a.remaining() > 0) {
            PotentialAvatar pa;
            memset(&pa, 0, sizeof(pa));
            pa.id = a.u32();
            a.string(pa.name, sizeof(pa.name));
            Bytes dna = a.blob();
            pa.slot = a.u8();
            pa.nameState = a.u8();
            if (!a.ok()) break;
            pa.hasDna = dna.len == kDnaSize;
            memcpy(pa.dna, dna.data, dna.len < kDnaSize ? dna.len : kDnaSize);
            if (numAvatars_ < kMaxAvatars) avatars_[numAvatars_++] = pa;
        }
        if (state_ != State::AvatarList) return;
        TT_LOG("net: %d avatar(s) on account", numAvatars_);
        onAvatarList();
    } else if (fieldId == dc::AstronLoginManager::k_createAvatarResponse) {
        uint32_t avId = r.u32();
        if (state_ != State::CreateAvatar) return;
        if (!r.ok() || avId == 0) { fail("The server could not create the toon.", true); return; }
        TT_LOG("net: created avatar %u", unsigned(avId));
        avatarId_ = avId;
        newAvatarId_ = avId;
        skipTutorialSent_ = false;
        skipTutorialResult_ = -1;
        rejoinAvatarId_ = avId;
        ownName_[0] = 0; // the server names it "<Color> <Species>"; the owner view brings it
        memcpy(ownDna_, pendingDna_, kDnaSize);
        sendRequestPlayAvatar(avatarId_);
        setState(State::PlayAvatar);
    }
}

void Client::onAvatarList() {
    if (cfg_.manualAvatar) {
        // After a reconnect, go straight back to the toon the player chose.
        if (rejoinAvatarId_ != 0) {
            for (int i = 0; i < numAvatars_; ++i) {
                if (avatars_[i].id == rejoinAvatarId_) {
                    playAvatar(avatars_[i]);
                    return;
                }
            }
            rejoinAvatarId_ = 0;
        }
        setState(State::ChooseAvatar);
        return;
    }
    if (numAvatars_ == 0) {
        memcpy(pendingDna_, cfg_.dna, kDnaSize);
        sendCreateAvatar(cfg_.dna, cfg_.slot);
        setState(State::CreateAvatar);
        return;
    }
    const PotentialAvatar* pick = &avatars_[0];
    for (int i = 0; i < numAvatars_; ++i) {
        if (avatars_[i].slot == cfg_.slot) pick = &avatars_[i];
    }
    playAvatar(*pick);
}

void Client::playAvatar(const PotentialAvatar& pa) {
    avatarId_ = pa.id;
    strncpy(ownName_, pa.name, sizeof(ownName_) - 1);
    ownName_[sizeof(ownName_) - 1] = 0;
    memcpy(ownDna_, pa.dna, kDnaSize);
    sendRequestPlayAvatar(avatarId_);
    setState(State::PlayAvatar);
}

const PotentialAvatar* Client::avatarInSlot(uint8_t slot) const {
    for (int i = 0; i < numAvatars_; ++i) {
        if (avatars_[i].slot == slot) return &avatars_[i];
    }
    return nullptr;
}

bool Client::chooseAvatar(uint32_t avId) {
    if (state_ != State::ChooseAvatar) return false;
    for (int i = 0; i < numAvatars_; ++i) {
        if (avatars_[i].id == avId) {
            TT_LOG("net: player chose avatar %u \"%s\"", unsigned(avId), avatars_[i].name);
            rejoinAvatarId_ = avId;
            error_ = "";
            playAvatar(avatars_[i]);
            return true;
        }
    }
    return false;
}

bool Client::createAvatar(const uint8_t dna[kDnaSize], uint8_t slot) {
    if (state_ != State::ChooseAvatar) return false;
    if (slot >= kMaxAvatars || avatarInSlot(slot) != nullptr) {
        TT_LOG("net: refusing createAvatar in slot %u (taken or out of range)", unsigned(slot));
        return false;
    }
    if (!world::isValidNetString(dna, kDnaSize)) {
        TT_LOG("net: refusing createAvatar with invalid DNA");
        return false;
    }
    memcpy(pendingDna_, dna, kDnaSize);
    error_ = "";
    sendCreateAvatar(dna, slot);
    setState(State::CreateAvatar);
    return true;
}

const char* Client::progressText() const {
    switch (state_) {
    case State::Idle: return "Offline";
    case State::Connecting: return "Connecting to the server...";
    case State::Hello: return "Saying hello to the server...";
    case State::Login: return "Logging in...";
    case State::Districts: return "Looking for districts...";
    case State::AvatarList: return "Loading your toons...";
    case State::ChooseAvatar: return "Pick a toon";
    case State::CreateAvatar: return "Creating your toon...";
    case State::PlayAvatar: return "Getting your toon ready...";
    case State::EnterZone: return "Entering Toontown Central...";
    case State::InZone: return "In Toontown";
    case State::ChangingZone: return "Travelling...";
    case State::Backoff: return "Connection lost. Reconnecting soon...";
    case State::Failed: return "Disconnected";
    }
    return "";
}

uint32_t Client::retryInMs() const {
    if (state_ != State::Backoff) return 0;
    int32_t left = int32_t(backoffUntil_ - now_);
    return left > 0 ? uint32_t(left) : 0;
}

bool Client::applyOwnField(DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedToon::k_setName) {
        r.string(ownName_, sizeof(ownName_));
    } else if (fieldId == dc::DistributedToon::k_setDNAString) {
        Bytes b = r.blob();
        if (r.ok()) memcpy(ownDna_, b.data, b.len < kDnaSize ? b.len : kDnaSize);
    } else if (fieldId == dc::DistributedToon::k_setHp || fieldId == dc::DistributedToon::k_setMaxHp ||
               fieldId == dc::DistributedToon::k_setMoney || fieldId == dc::DistributedToon::k_setMaxMoney ||
               fieldId == dc::DistributedToon::k_setBankMoney) {
        const int16_t v = r.i16();
        if (!r.ok()) return false;
        if (fieldId == dc::DistributedToon::k_setHp) ownStats_.hp = v;
        if (fieldId == dc::DistributedToon::k_setMaxHp) ownStats_.maxHp = v;
        if (fieldId == dc::DistributedToon::k_setMoney) ownStats_.money = v;
        if (fieldId == dc::DistributedToon::k_setMaxMoney) ownStats_.maxMoney = v;
        if (fieldId == dc::DistributedToon::k_setBankMoney) ownStats_.bankMoney = v;
        pushEvent(EventType::Stats, avatarId_);
    } else if (fieldId == dc::DistributedToon::k_setExperience) {
        // Experience.makeNetString: uint16 per track.
        Bytes b = r.blob();
        if (!r.ok()) return false;
        for (uint32_t t = 0; t < 7 && 2 * t + 1 < b.len; t++)
            ownStats_.experience[t] = uint16_t(b.data[2 * t] | (b.data[2 * t + 1] << 8));
        pushEvent(EventType::Stats, avatarId_);
    } else if (fieldId == dc::DistributedToon::k_setInventory) {
        // InventoryBase.makeNetString: uint8 count per track and level.
        Bytes b = r.blob();
        if (!r.ok()) return false;
        for (uint32_t i = 0; i < 49 && i < b.len; i++) ownStats_.inventory[i / 7][i % 7] = b.data[i];
        ownStats_.hasGags = true;
        pushEvent(EventType::Stats, avatarId_);
    } else if (fieldId == dc::DistributedToon::k_setTrackAccess) {
        const uint16_t bytes = r.u16();
        for (uint16_t i = 0; i < bytes / 2 && r.ok(); i++) {
            const uint16_t v = r.u16();
            if (i < 7) ownStats_.trackAccess[i] = v ? 1 : 0;
        }
        pushEvent(EventType::Stats, avatarId_);
    } else if (fieldId == dc::DistributedToon::k_setMaxCarry) {
        ownStats_.maxCarry = r.u8();
    } else {
        skipField(r, fieldId);
    }
    return r.ok();
}

bool Client::applyNpcField(NpcToon& n, DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedNPCToonBase::k_setName) {
        r.string(n.name, sizeof(n.name));
    } else if (fieldId == dc::DistributedNPCToonBase::k_setDNAString) {
        Bytes b = r.blob();
        if (!r.ok()) return false;
        n.hasDna = b.len == kDnaSize;
        memcpy(n.dna, b.data, b.len < kDnaSize ? b.len : kDnaSize);
    } else if (fieldId == dc::DistributedNPCToonBase::k_setPositionIndex) {
        n.posIndex = r.u8();
    } else {
        skipField(r, fieldId);
    }
    return r.ok();
}

bool Client::applyTreasureField(Treasure& t, DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedTreasure::k_setPosition) {
        t.x = r.i16() / 10.0f;
        t.y = r.i16() / 10.0f;
        t.z = r.i16() / 10.0f;
    } else {
        skipField(r, fieldId);
    }
    return r.ok();
}

bool Client::handleNpcEnter(const dc::ClassDesc& cls, uint32_t doId, uint16_t classId, DatagramReader& r,
                            const uint16_t* req, uint16_t reqCount, bool other) {
    (void)cls;
    NpcToon* n = nullptr;
    for (NpcToon& s : npcs_) {
        if (s.active && s.doId == doId) n = &s;
    }
    for (NpcToon& s : npcs_) {
        if (!n && !s.active) n = &s;
    }
    if (!n) return false;
    NpcToon tmp{};
    tmp.doId = doId;
    tmp.classId = classId;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyNpcField(tmp, r, req[i]);
    if (ok && other) {
        uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && ok && r.ok(); ++i) ok = applyNpcField(tmp, r, r.u16());
    }
    if (!ok || !r.ok()) {
        TT_LOG("net: NPC %u generate did not parse", unsigned(doId));
        return false;
    }
    tmp.active = true;
    *n = tmp;
    TT_LOG("net: NPC %u \"%s\" at origin %u", unsigned(doId), n->name, unsigned(n->posIndex));
    pushEvent(EventType::NpcSpawn, doId);
    return true;
}

bool Client::handleTreasureEnter(uint32_t doId, DatagramReader& r, const uint16_t* req, uint16_t reqCount,
                                 bool other) {
    Treasure* t = nullptr;
    for (Treasure& s : treasures_) {
        if (s.active && s.doId == doId) t = &s;
    }
    for (Treasure& s : treasures_) {
        if (!t && !s.active) t = &s;
    }
    if (!t) return false;
    Treasure tmp{};
    tmp.doId = doId;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyTreasureField(tmp, r, req[i]);
    if (ok && other) {
        uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && ok && r.ok(); ++i) ok = applyTreasureField(tmp, r, r.u16());
    }
    if (!ok || !r.ok()) return false;
    tmp.active = true;
    *t = tmp;
    TT_LOG("net: treasure %u at %.1f,%.1f,%.1f", unsigned(doId), double(t->x), double(t->y), double(t->z));
    pushEvent(EventType::TreasureSpawn, doId);
    return true;
}

bool Client::applyCCharField(CChar& c, DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedCCharBase::k_setWalk) {
        r.string(c.src, sizeof(c.src));
        r.string(c.dst, sizeof(c.dst));
        const int16_t ts = r.i16();
        // CharWalkState seeks the walk to globalClockDelta.localElapsedTime(ts).
        c.walkStartMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
    } else {
        skipField(r, fieldId);
    }
    return r.ok();
}

bool Client::handleCCharEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                              uint16_t reqCount, bool other) {
    CChar* c = nullptr;
    for (CChar& s : cchars_) {
        if (s.active && s.doId == doId) c = &s;
    }
    for (CChar& s : cchars_) {
        if (!c && !s.active) c = &s;
    }
    if (!c) return false;
    CChar tmp{};
    tmp.doId = doId;
    tmp.classId = classId;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyCCharField(tmp, r, req[i]);
    if (ok && other) {
        uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && ok && r.ok(); ++i) ok = applyCCharField(tmp, r, r.u16());
    }
    if (!ok || !r.ok()) {
        TT_LOG("net: classic char %u generate did not parse", unsigned(doId));
        return false;
    }
    tmp.active = true;
    *c = tmp;
    TT_LOG("net: classic char %u class %u walk %s->%s", unsigned(doId), unsigned(classId), c->src, c->dst);
    pushEvent(EventType::CCharSpawn, doId);
    return true;
}

const CChar* Client::findCChar(uint32_t doId) const {
    for (const CChar& c : cchars_) {
        if (c.active && c.doId == doId) return &c;
    }
    return nullptr;
}

const NpcToon* Client::findNpc(uint32_t doId) const {
    for (const NpcToon& n : npcs_) {
        if (n.active && n.doId == doId) return &n;
    }
    return nullptr;
}

bool Client::sendRequestGrab(uint32_t treasureDoId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, treasureDoId, DistributedTreasure, requestGrab);
    return finish(w);
}

bool Client::applyDistrictField(District& d, DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedDistrict::k_setName) r.string(d.name, sizeof(d.name));
    else if (fieldId == dc::DistributedDistrict::k_setAvailable) d.available = r.u8() != 0;
    else skipField(r, fieldId);
    return r.ok();
}

bool Client::applyToonField(RemoteToon& t, DatagramReader& r, uint16_t fieldId) {
    static const dc::ClassDesc* toonClass = findClass(dc::DistributedToon::kClassId);
    if (const PoseField* pf = findPoseField(fieldId)) {
        bool timed = false;
        for (const char* c = pf->comps; *c; ++c) {
            switch (*c) {
            case 'X': t.x = r.i16() / 10.0f; break;
            case 'Y': t.y = r.i16() / 10.0f; break;
            case 'Z': t.z = r.i16() / 10.0f; break;
            case 'H': t.h = r.i16() / 10.0f; break;
            case 'P': t.p = r.i16() / 10.0f; break;
            case 'R': t.r = r.i16() / 10.0f; break;
            case 'T': {
                int16_t ts = r.i16();
                // Before the TimeManager sync the sender's clock is meaningless to us.
                t.sampleMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
                timed = true;
                break;
            }
            case 'L': r.u64(); break;
            default: break;
            }
        }
        if (!r.ok()) return false;
        if (!timed) t.sampleMs = now_;
        t.lastUpdateTick = now_;
        bool wasMoving = t.moving;
        t.moving = fieldId != dc::DistributedSmoothNode::k_setSmStop;
        if (t.active && (t.moving || wasMoving)) pushEvent(EventType::Pose, t.doId);
        return true;
    }
    switch (fieldId) {
    case dc::DistributedToon::k_setName:
        r.string(t.name, sizeof(t.name));
        if (t.active && r.ok()) pushEvent(EventType::Name, t.doId);
        break;
    case dc::DistributedToon::k_setDNAString: {
        Bytes b = r.blob();
        if (!r.ok()) return false;
        t.hasDna = b.len == kDnaSize;
        memcpy(t.dna, b.data, b.len < kDnaSize ? b.len : kDnaSize);
        if (t.active) pushEvent(EventType::Dna, t.doId);
        break;
    }
    case dc::DistributedToon::k_setAnimState: {
        Bytes name = r.blob();
        int16_t rate = r.i16();
        r.i16(); // timestamp; the game applies anim changes immediately
        if (!r.ok()) return false;
        copyBytes(t.animState, sizeof(t.animState), name);
        t.animRate = rate / 1000.0f;
        if (t.active) pushEvent(EventType::Anim, t.doId);
        break;
    }
    case dc::DistributedToon::k_setSC: {
        const uint16_t index = r.u16();
        if (!r.ok()) return false;
        t.chat[0] = '\0';
        t.scIndex = index;
        t.chatMs = now_;
        if (t.active) pushEvent(EventType::Chat, t.doId);
        break;
    }
    case dc::DistributedNode::k_setParent:
        t.parentToken = r.u32();
        break;
    case dc::DistributedToon::k_setTalk: {
        r.u32(); // from avatar
        r.u32(); // from account
        r.blob(); // sender name
        Bytes chat = r.blob();
        r.blob(); // TalkModification[] (whitelist highlighting)
        r.u8();   // chat flags
        if (!r.ok()) return false;
        copyBytes(t.chat, sizeof(t.chat), chat);
        t.scIndex = -1;
        t.chatMs = now_;
        if (t.active) pushEvent(EventType::Chat, t.doId);
        break;
    }
    default:
        if (!toonClass || !classHasField(*toonClass, fieldId)) {
            r.fail();
            return false;
        }
        skipField(r, fieldId);
        break;
    }
    return r.ok();
}

void Client::maybePickDistrict() {
    if (state_ != State::EnterZone || shardId_ != 0 || !ownerReceived_) return;
    for (const District& d : districts_) {
        if (d.active && d.available) {
            shardId_ = d.doId;
            break;
        }
    }
    if (shardId_ == 0) return; // wait for one to become available
    enterZone();
}

void Client::enterZone() {
    TT_LOG("net: entering district %u \"%s\" zone %u", unsigned(shardId_), districtName(),
           unsigned(cfg_.zoneId));
    sendSetLocation(avatarId_, shardId_, cfg_.zoneId);
    uberCtx_ = nextContext_;
    sendAddInterest(kUberInterest, shardId_, kManagementZone);
    zoneCtx_ = nextContext_;
    sendAddInterest(kZoneInterest, shardId_, cfg_.zoneId);
}

const char* Client::districtName() const {
    for (const District& d : districts_) {
        if (d.active && d.doId == shardId_) return d.name;
    }
    return "";
}

RemoteToon* Client::toonById(uint32_t doId) {
    for (RemoteToon& t : toons_) {
        if (t.active && t.doId == doId) return &t;
    }
    return nullptr;
}

const RemoteToon* Client::findToon(uint32_t doId) const {
    for (const RemoteToon& t : toons_) {
        if (t.active && t.doId == doId) return &t;
    }
    return nullptr;
}

District* Client::districtById(uint32_t doId) {
    for (District& d : districts_) {
        if (d.active && d.doId == doId) return &d;
    }
    return nullptr;
}

void Client::pushEvent(EventType type, uint32_t doId) {
    if (evHead_ - evTail_ >= uint32_t(kEventQueue)) {
        ++droppedEvents_;
        return;
    }
    events_[evHead_ % kEventQueue] = {type, doId};
    ++evHead_;
}

bool Client::pollEvent(Event& out) {
    if (evHead_ == evTail_) return false;
    out = events_[evTail_ % kEventQueue];
    ++evTail_;
    return true;
}

// ============================================================== clock
int16_t Client::networkTime(uint32_t nowMs) const {
    int64_t ticks = ticksOf(int64_t(nowMs) - clockDeltaMs_);
    return int16_t(uint16_t(uint64_t(ticks) & 0xffff));
}

uint32_t Client::networkToLocal(int16_t ts, uint32_t nowMs) const {
    int16_t ntime = networkTime(nowMs);
    int16_t diff = int16_t(uint16_t(uint16_t(ts) - uint16_t(ntime)));
    return uint32_t(int64_t(nowMs) + int64_t(diff) * 10);
}

// ============================================================== outgoing
DatagramWriter Client::beginMessage(uint16_t type) {
    DatagramWriter w(tx_, sizeof(tx_));
    w.u16(type);
    return w;
}

void Client::beginUpdate(DatagramWriter& w, uint32_t doId, uint16_t fieldId) {
    w.u32(doId);
    w.u16(fieldId);
}

bool Client::finish(DatagramWriter& w) {
    if (!w.ok() || !transport_->sendMessage(w.data(), w.size())) return false;
    bytesOut_ += w.size();
    ++msgsOut_;
    return true;
}

void Client::sendHello() {
    DatagramWriter w = beginMessage(msg::CLIENT_HELLO);
    w.u32(dc::kHash);
    w.string(dc::kServerVersion);
    finish(w);
}

void Client::sendAddInterest(uint16_t handle, uint32_t parent, uint32_t zone) {
    DatagramWriter w = beginMessage(msg::CLIENT_ADD_INTEREST);
    w.u32(nextContext_++);
    w.u16(handle);
    w.u32(parent);
    w.u32(zone);
    finish(w);
}

void Client::sendAddInterestMultiple(uint16_t handle, uint32_t parent, const uint32_t* zones, int n) {
    DatagramWriter w = beginMessage(msg::CLIENT_ADD_INTEREST_MULTIPLE);
    w.u32(nextContext_++);
    w.u16(handle);
    w.u32(parent);
    w.u16(static_cast<uint16_t>(n));
    for (int i = 0; i < n; ++i) w.u32(zones[i]);
    finish(w);
}

void Client::dropZoneObjects() {
    // QuietZoneState.dumpAllSubShardObjects: forget everything in the old zone (the
    // server also sends OBJECT_LEAVING; unknown ids are ignored then).
    for (RemoteToon& t : toons_) {
        if (t.active) pushEvent(EventType::Despawn, t.doId);
        t.active = false;
    }
    numToons_ = 0;
    for (NpcToon& n : npcs_) {
        if (n.active) pushEvent(EventType::NpcDespawn, n.doId);
        n.active = false;
    }
    for (Treasure& t : treasures_) {
        if (t.active) pushEvent(EventType::TreasureGone, t.doId);
        t.active = false;
    }
    for (CChar& c : cchars_) {
        if (c.active) pushEvent(EventType::CCharGone, c.doId);
        c.active = false;
    }
}

bool Client::changeStreetCell(uint32_t cellZone, const uint16_t* visZones, int numVisZones) {
    if (state_ != State::InZone) return false;
    uint32_t zones[kMaxVisZones + 1];
    int n = 0;
    zones[n++] = cellZone;
    for (int i = 0; i < numVisZones && n <= kMaxVisZones; ++i) {
        if (visZones[i] != cellZone) zones[n++] = visZones[i];
    }
    cfg_.zoneId = cellZone;
    sendSetLocation(avatarId_, shardId_, cellZone);
    zoneCtx_ = nextContext_;
    sendAddInterestMultiple(kZoneInterest, shardId_, zones, n);
    TT_LOG("net: street cell %u (%d zones in interest)", unsigned(cellZone), n);
    return true;
}

bool Client::changeZone(uint32_t zoneId, const uint32_t* visZones, int numVisZones) {
    if (state_ != State::InZone) return false;
    targetZone_ = zoneId;
    numVisZones_ = numVisZones < kMaxVisZones ? numVisZones : kMaxVisZones;
    for (int i = 0; i < numVisZones_; ++i) visZones_[i] = visZones[i];
    TT_LOG("net: changing zone %u -> %u (%d vis zones)", unsigned(cfg_.zoneId), unsigned(zoneId), numVisZones_);
    sendSetLocation(avatarId_, shardId_, kQuietZone);
    quietCtx_ = nextContext_;
    quietDone_ = false;
    sendAddInterest(kZoneInterest, shardId_, kQuietZone);
    dropZoneObjects();
    setState(State::ChangingZone);
    return true;
}

void Client::sendSetLocation(uint32_t doId, uint32_t parent, uint32_t zone) {
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_LOCATION);
    w.u32(doId);
    w.u32(parent);
    w.u32(zone);
    finish(w);
}

void Client::sendRequestLogin() {
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, kLoginManagerId, AstronLoginManager, requestLogin);
    w.string(token_);
    finish(w);
}

void Client::sendRequestAvatarList() {
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, kLoginManagerId, AstronLoginManager, requestAvatarList);
    finish(w);
}

void Client::sendCreateAvatar(const uint8_t* dna, uint8_t slot) {
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, kLoginManagerId, AstronLoginManager, createAvatar);
    w.blob(dna, kDnaSize);
    w.u8(slot);
    finish(w);
}

void Client::sendRequestSkipTutorial() {
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, tutorialManagerId_, TutorialManager, requestSkipTutorial);
    finish(w);
}

void Client::sendRequestPlayAvatar(uint32_t avId) {
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, kLoginManagerId, AstronLoginManager, requestPlayAvatar);
    w.u32(avId);
    finish(w);
}

void Client::sendRequestServerTime() {
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, timeManagerId_, TimeManager, requestServerTime);
    w.u8(++timeContext_);
    timeRequestMs_ = now_;
    finish(w);
}

void Client::sendSetParent(uint32_t token) {
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_OWN_UPDATE(w, DistributedNode, setParent);
    w.u32(token);
    finish(w);
}

bool Client::sendPosHpr(float x, float y, float z, float h, float p, float r) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_OWN_UPDATE(w, DistributedSmoothNode, setSmPosHpr);
    w.i16(packFixed(x, 10.0f));
    w.i16(packFixed(y, 10.0f));
    w.i16(packFixed(z, 10.0f));
    w.i16(packAngle(h));
    w.i16(packAngle(p));
    w.i16(packAngle(r));
    w.i16(networkTime(now_));
    return finish(w);
}

bool Client::sendStop() {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_OWN_UPDATE(w, DistributedSmoothNode, setSmStop);
    w.i16(networkTime(now_));
    return finish(w);
}

namespace {

// toontown/toonbase/ToontownGlobals.py ToonAnimStates (the subset this client sends).
// The AI bans (and, on stock Open Toontown, crashes in banManager) on any other name.
const char* const kAllowedAnimStates[] = {"off", "neutral", "run", "walk", "jumpSquat", "jump", "jumpAirborne",
                                          "jumpLand", "Happy", "Sad", "Sleep", "Emote"};

bool allowedAnimState(const char* name) {
    for (const char* allowed : kAllowedAnimStates) {
        if (!strcmp(name, allowed)) return true;
    }
    return false;
}

} // namespace

bool Client::sendAnimState(const char* name, float rate) {
    if (state_ != State::InZone) return false;
    if (!allowedAnimState(name)) {
        TT_LOG("net: refusing to send invalid animState '%s'", name);
        return false;
    }
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_OWN_UPDATE(w, DistributedToon, setAnimState);
    w.string(name);
    w.i16(packFixed(rate, 1000.0f));
    w.i16(networkTime(now_));
    return finish(w);
}

bool Client::sendSpeedChat(uint16_t msgIndex) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_OWN_UPDATE(w, DistributedToon, setSC);
    w.u16(msgIndex);
    return finish(w);
}

} // namespace tt::net
