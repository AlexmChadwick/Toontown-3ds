#include "net/client.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "net/msg_types.h"
#include "game/name_tables.h"
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
TT_SIG(AstronLoginManager, setNamePattern, "u32,i16,u8,i16,u8,i16,u8,i16,u8");
TT_SIG(AstronLoginManager, namePatternAnswer, "u32,u8");
TT_SIG(AstronLoginManager, setNameTyped, "u32,string");
TT_SIG(AstronLoginManager, nameTypedResponse, "u32,u8");
TT_SIG(AstronLoginManager, acknowledgeAvatarName, "u32");
TT_SIG(AstronLoginManager, acknowledgeAvatarNameResponse, "");
TT_SIG(AstronLoginManager, requestRemoveAvatar, "u32");
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
TT_SIG(DistributedToon, setEmoteState, "i16,i16/1000,i16");
TT_SIG(DistributedToon, setEmoteAccess, "u8[]");
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
TT_SIG(DistributedNPCToonBase, avatarEnter, "");
TT_SIG(DistributedNPCClerk, setMovie, "u8,u32,u32,i16");
TT_SIG(DistributedNPCClerk, setInventory, "blob,i16,u8");
TT_SIG(DistributedNPCTailor, setMovie, "u8,u32,u32,i16");
TT_SIG(DistributedNPCTailor, setDNA, "blob,i8,u8");
// quests section
TT_SIG(DistributedToon, setQuests, "u32[]");
TT_SIG(DistributedToon, setQuestHistory, "u16[]");
TT_SIG(DistributedToon, setRewardHistory, "u8,u16[]");
TT_SIG(DistributedToon, setQuestCarryLimit, "u8");
TT_SIG(DistributedToon, requestDeleteQuest, "u32[]");
// Shtiker Book section (source/net/client_book.cpp)
TT_SIG(DistributedToon, setTrackProgress, "i8,u32");
TT_SIG(DistributedToon, setCogStatus, "u32[]");
TT_SIG(DistributedToon, setCogCount, "u32[]");
TT_SIG(DistributedToon, setCogRadar, "u8[]");
TT_SIG(DistributedToon, setBuildingRadar, "u8[]");
TT_SIG(DistributedToon, setCogParts, "u32[]");
TT_SIG(DistributedToon, setCogMerits, "u16[]");
TT_SIG(DistributedToon, setCogTypes, "u8[]");
TT_SIG(DistributedToon, setCogLevels, "u8[]");
TT_SIG(DistributedToon, setFishCollection, "u8[],u8[],u16[]");
TT_SIG(DistributedToon, setFishingTrophies, "u8[]");
TT_SIG(DistributedToon, setKartingTrophies, "u8[33]");
TT_SIG(DistributedToon, setKartingHistory, "u8[16]");
TT_SIG(DistributedToon, setTickets, "u32");
TT_SIG(DistributedToon, setGolfHistory, "u16[18]");
TT_SIG(DistributedToon, setPackedGolfHoleBest, "u8[18]");
TT_SIG(DistributedToon, setGolfCourseBest, "u8[3]");
TT_SIG(DistributedToon, setNPCFriendsDict, "{u32,u8}[]");
TT_SIG(DistributedNPCToonBase, avatarEnter, "");
TT_SIG(DistributedNPCToon, setMovie, "u8,u32,u32,u16[],i16");
TT_SIG(DistributedNPCToon, setMovieDone, "");
TT_SIG(DistributedNPCToon, chooseQuest, "u16");
TT_SIG(DistributedNPCToon, chooseTrack, "i8");
TT_SIG(DistributedNPCSpecialQuestGiver, setMovie, "u8,u32,u32,u16[],i16");
TT_SIG(DistributedNPCSpecialQuestGiver, setMovieDone, "");
TT_SIG(DistributedNPCSpecialQuestGiver, chooseQuest, "u16");
TT_SIG(DistributedNPCSpecialQuestGiver, chooseTrack, "i8");
TT_SIG(DistributedTreasure, setPosition, "i16/10,i16/10,i16/10");
TT_SIG(DistributedTreasure, requestGrab, "");
TT_SIG(DistributedTreasure, setGrab, "u32");
TT_SIG(DistributedCCharBase, setWalk, "string,string,i16");
TT_SIG(DistributedBoat, setState, "string,i16");
TT_SIG(DistributedSuitPlanner, setZoneId, "u32");
TT_SIG(DistributedSuit, setDNAString, "blob");
TT_SIG(DistributedSuit, setLevelDist, "i16");
TT_SIG(DistributedSuit, setSkelecog, "u8");
TT_SIG(DistributedSuit, setSkeleRevives, "u8");
TT_SIG(DistributedSuit, setHP, "i16");
TT_SIG(DistributedSuit, setSPDoId, "u32");
TT_SIG(DistributedSuit, setPathEndpoints, "u16,u16,u16,u16");
TT_SIG(DistributedSuit, setPathPosition, "u16,i16");
TT_SIG(DistributedSuit, setPathState, "i8");
TT_SIG(DistributedSuit, setBrushOff, "i16");
TT_SIG(DistributedSuit, denyBattle, "");
TT_SIG(DistributedSuit, requestBattle, "i16/10,i16/10,i16/10,i16/10,i16/10,i16/10");
// ---- battle section: the layouts parseBattle*() read by hand ----
TT_SIG(DistributedToon, setBattleId, "u32");
TT_SIG(DistributedBattle, setPosition, "i16/10,i16/10,i16/10");
TT_SIG(DistributedBattle, setInitialSuitPos, "i16/10,i16/10,i16/10");
TT_SIG(DistributedBattle, setZoneId, "u32");
TT_SIG(DistributedBattle, setMembers, "u32[],string,string,string,string,string,u32[],string,string,string,string,i16");
TT_SIG(DistributedBattle, adjust, "i16");
TT_SIG(DistributedBattle, setMovie,
       "i8,u32[],u32[],i8,i8,i8,i32,i16[],i16,i16,i16[],i8,i8,i8,i8,i8,i32,i16[],i16,i16,i16[],i8,i8,i8,i8,i8,i32,i16[],"
       "i16,i16,i16[],i8,i8,i8,i8,i8,i32,i16[],i16,i16,i16[],i8,i8,i8,i8,i8,i16[],i8,i8,i8,i8,i8,i8,i16[],i8,i8,i8,i8,i8,"
       "i8,i16[],i8,i8,i8,i8,i8,i8,i16[],i8,i8,i8");
TT_SIG(DistributedBattle, setBattleExperience,
       "i32,i16[],i16[],u32[],i16[],i16[],i16[],i16[],u32[],i32,i16[],i16[],u32[],i16[],i16[],i16[],i16[],u32[],i32,i16[],"
       "i16[],u32[],i16[],i16[],i16[],i16[],u32[],i32,i16[],i16[],u32[],i16[],i16[],i16[],i16[],u32[],u8[],i16[],u32[]");
TT_SIG(DistributedBattle, setState, "string,i16");
TT_SIG(DistributedBattle, faceOffDone, "");
TT_SIG(DistributedBattle, toonRequestJoin, "i16/10,i16/10,i16/10");
TT_SIG(DistributedBattle, toonRequestRun, "");
TT_SIG(DistributedBattle, adjustDone, "");
TT_SIG(DistributedBattle, timeout, "");
TT_SIG(DistributedBattle, movieDone, "");
TT_SIG(DistributedBattle, rewardDone, "");
TT_SIG(DistributedBattle, joinDone, "u32");
TT_SIG(DistributedBattle, requestAttack, "i8,i8,i32");
TT_SIG(DistributedCCharBase, setChat, "u32,u32,u32");
TT_SIG(DistributedDale, setFollowChip, "string,string,i16,i16/100,i16/100");
TT_SIG(DistributedDale, setChipId, "u32");
TT_SIG(DistributedJailbirdDale, setFollowChip, "string,string,i16,i16/100,i16/100");
TT_SIG(DistributedJailbirdDale, setChipId, "u32");
TT_SIG(DistributedFishingPond, setArea, "u32");
TT_SIG(DistributedFishingPond, hitTarget, "u32");
TT_SIG(DistributedFishingSpot, setPondDoId, "u32");
TT_SIG(DistributedFishingSpot, setPosHpr, "i16/10,i16/10,i16/10,i16/10,i16/10,i16/10");
TT_SIG(DistributedFishingSpot, requestEnter, "");
TT_SIG(DistributedFishingSpot, requestExit, "");
TT_SIG(DistributedFishingSpot, setOccupied, "u32");
TT_SIG(DistributedFishingSpot, doCast, "u8/255,i16/100");
TT_SIG(DistributedFishingSpot, sellFish, "");
TT_SIG(DistributedFishingSpot, sellFishComplete, "u8,u16");
TT_SIG(DistributedFishingSpot, setMovie, "u8,u8,u16,u16,u16,u8/100,i16/100");
TT_SIG(DistributedToon, setFishingRod, "u8");
TT_SIG(DistributedToon, setMaxFishTank, "u8");
TT_SIG(DistributedToon, setFishTank, "u8[],u8[],u16[]");
// ---- trolley section: layouts parsed by hand below (docs/design/trolley.md) ----
TT_SIG(DistributedTrolley, setState, "string,i16");
TT_SIG(DistributedTrolley, fillSlot0, "u32");
TT_SIG(DistributedTrolley, emptySlot0, "u32,i16");
TT_SIG(DistributedTrolley, requestBoard, "");
TT_SIG(DistributedTrolley, rejectBoard, "u32");
TT_SIG(DistributedTrolley, requestExit, "");
TT_SIG(DistributedTrolley, setMinigameZone, "u32,u16");
TT_SIG(DistributedElevator, fillSlot0, "u32,u8");
TT_SIG(DistributedElevator, emptySlot0, "u32,i8,i16,i16");
TT_SIG(DistributedMinigame, setParticipants, "u32[]");
TT_SIG(DistributedMinigame, setTrolleyZone, "u32");
TT_SIG(DistributedMinigame, setDifficultyOverrides, "i32,i32");
TT_SIG(DistributedMinigame, setGameStart, "i16");
TT_SIG(DistributedCatchGame, claimCatch, "u32,u32");
TT_SIG(DistributedCatchGame, setObjectCaught, "u32,u32");
TT_SIG(DistributedCatchGame, hitBySuit, "u32,i16");
TT_SIG(PurchaseManager, setPlayerIds, "u32,u32,u32,u32");
TT_SIG(PurchaseManager, setMinigamePoints, "u8,u8,u8,u8");
TT_SIG(PurchaseManager, setPlayerMoney, "u8,u8,u8,u8");
TT_SIG(PurchaseManager, setPlayerStates, "u8,u8,u8,u8");
TT_SIG(PurchaseManager, setInventory, "blob,i16,u8");
static_assert(dc::DistributedTrolley::k_fillSlot3 == dc::DistributedTrolley::k_fillSlot0 + 3, "fillSlot ids");
static_assert(dc::DistributedTrolley::k_emptySlot3 == dc::DistributedTrolley::k_emptySlot0 + 3, "emptySlot ids");
// ---- end trolley section ----
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
    // ---- cogdo reconnect: return to the stored street, never a dead interior ----
    if (const CogdoState* interior = cogdoInterior()) {
        cfg_.zoneId = interior->exterior;
        targetZone_ = interior->exterior;
        numVisZones_ = 0;
    }
    // ---- end cogdo reconnect ----
    // A VP dynamic zone is destroyed when its riders disconnect. Reconnect to
    // the static lobby instead of requesting an interest in the abandoned office.
    // ---- CEO boss reconnect: dynamic banquet zones cannot be rejoined. ----
    if (boss_.active && boss_.kind == BossKind::Bossbot) {
        cfg_.zoneId = 10100;
        targetZone_ = 10100;
        numVisZones_ = 0;
    }
    if (boss_.active && boss_.kind == BossKind::Lawbot) {
        cfg_.zoneId = 13100;
        targetZone_ = 13100;
        numVisZones_ = 0;
    }
    if (boss_.active && (boss_.kind == BossKind::Sellbot || boss_.kind == BossKind::Cashbot)) {
        cfg_.zoneId = boss_.kind == BossKind::Cashbot ? 12100 : 11100;
        targetZone_ = cfg_.zoneId;
        numVisZones_ = 0;
    }
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
    resetClerks(); // clerks: reset owner/session on disconnect

    resetBoss();
    bossPies_ = 0;
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
    shopMovie_ = ShopMovie{}; // the clerk is gone; a session with it cannot continue
    tailorMovie_ = TailorMovie{};
    for (Treasure& t : treasures_) {
        if (t.active) pushEvent(EventType::TreasureGone, t.doId);
        t.active = false;
    }
    clearSuits();
    for (CChar& c : cchars_) {
        if (c.active) pushEvent(EventType::CCharGone, c.doId);
        c.active = false;
    }
    if (boat_.active) pushEvent(EventType::BoatGone, boat_.doId);
    boat_.active = false;
    clearBattles();
    clearCogObjects();
    clearEstateObjects();
    ownBattleId_ = 0;
    clearTrolleyObjects();
    if (hadWorld) pushEvent(EventType::LeftZone, 0);
    clearFishing();
    for (District& d : districts_) d.active = false;
    numAvatars_ = 0;
    avatarId_ = 0;
    ownQuests_ = OwnQuests{};
    ownBook_.initDefaults();
    movieHead_ = movieTail_ = 0;
    ownerReceived_ = false;
    shardId_ = 0;
    timeManagerId_ = 0;
    tutorialManagerId_ = 0;
    timeSynced_ = false;
    districtCtx_ = uberCtx_ = zoneCtx_ = 0;
    districtsDone_ = uberDone_ = zoneDone_ = false;
    zoneReadyMs_ = 0;
    friendsReset(); // friends hook (client_friends.cpp)
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

    friendsUpdate(); // friends hook (client_friends.cpp)
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
            // Still visible if it moved to another zone of our interest (street visgroups).
            bool inInterest = zone == cfg_.zoneId;
            for (int i = 0; i < numVisZones_ && !inInterest; ++i) inInterest = visZones_[i] == zone;
            if (parent != shardId_ || !inInterest) handleLeaving(doId);
        }
        // A cog crossing into the next visgroup (DistributedSuitAI.sendSetZone).
        for (Suit& su : suits_) {
            if (r.ok() && su.active && su.doId == doId) su.zoneId = zone;
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
    if (friendsHandleEnter(*cls, r, doId, req, reqCount, other)) return; // friends hook (client_friends.cpp)

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
    if (classId == dc::EstateManager::kClassId) {
        estateManagerDoId_ = doId;
        return;
    }
    if (classId == dc::DistributedDoor::kClassId || classId == dc::DistributedAnimDoor::kClassId ||
        classId == dc::DistributedCogHQDoor::kClassId || classId == dc::DistributedSellbotHQDoor::kClassId ||
        classId == dc::DistributedHouseDoor::kClassId) {
        NetDoor d;
        d.doId = doId;
        d.zoneId = zone;
        for (uint16_t i = 0; i < reqCount && r.ok(); ++i) {
            if (req[i] == dc::DistributedDoor::k_setZoneIdAndBlock) {
                (void)r.u32();
                d.block = r.u32();
            } else if (req[i] == dc::DistributedDoor::k_setDoorType) {
                d.doorType = r.u8();
            } else if (req[i] == dc::DistributedDoor::k_setDoorIndex) {
                d.doorIndex = r.u8();
            } else {
                skipField(r, req[i]);
            }
        }
        if (numDoors_ < kMaxDoors) {
            doors_[numDoors_++] = d;
        }
        if (classId == dc::DistributedHouseDoor::kClassId) {
            houseDoorDoId_ = doId;
        }
        return;
    }
    if (classId == dc::DistributedEstate::kClassId || classId == dc::DistributedHouse::kClassId ||
        classId == dc::DistributedHouseInterior::kClassId || classId == dc::DistributedHouseDoor::kClassId ||
        classId == dc::DistributedMailbox::kClassId || classId == dc::DistributedBank::kClassId ||
        classId == dc::DistributedCloset::kClassId ||
        classHasField(*cls, dc::DistributedFurnitureItem::k_setItem) ||
        classHasField(*cls, dc::DistributedPet::k_setHead)) {
        if (classId == dc::DistributedHouseDoor::kClassId) {
            houseDoorDoId_ = doId;
            return;
        }
        if (classId == dc::DistributedCloset::kClassId) {
            closet_ = Closet{}; closet_.doId = doId;
            for (uint16_t i = 0; i < reqCount && r.ok(); ++i) {
                if (req[i] == dc::DistributedCloset::k_setOwnerId) closet_.ownerId = r.u32();
                else skipField(r, req[i]);
            }
            if (other) {
                const uint16_t n = r.u16();
                for (uint16_t i = 0; i < n && r.ok(); ++i) {
                    const uint16_t id = r.u16();
                    if (id == dc::DistributedCloset::k_setOwnerId) closet_.ownerId = r.u32();
                    else skipField(r, id);
                }
            }
            closet_.active = r.ok();
            if (closet_.active) pushEvent(EventType::ClosetUp, doId);
            return;
        }
        if (classId == dc::DistributedMailbox::kClassId) {
            mailboxDoId_ = doId; pushEvent(EventType::MailboxUp, doId);
            return;
        }
        if (classId == dc::DistributedBank::kClassId) {
            bankDoId_ = doId; pushEvent(EventType::BankUp, doId);
            return;
        }
        handleEstateEnter(doId, classId, r, req, reqCount, other);
        return;
    }
    // trolley section: the playground trolley, any minigame, the purchase manager.
    if (classId == dc::DistributedTrolley::kClassId) {
        handleTrolleyEnter(doId, r, req, reqCount, other);
        return;
    }
    if (handleRacingEnter(doId, classId, r, req, reqCount, other)) return;
    if (classId == dc::DistributedGolfKart::kClassId ||
        classId == dc::DistributedGolfCourse::kClassId ||
        classId == dc::DistributedGolfHole::kClassId ||
        classId == dc::DistributedPhysicsWorld::kClassId) {
        handleGolfEnter(doId, classId, r, req, reqCount, other);
        return;
    }
    if (handleCogdoEnter(doId, classId, r, req, reqCount, other)) return; // cogdo
    if (handleBossEnter(doId, classId, r, req, reqCount, other)) return; // bosses
    if (handleLevelEnter(doId, classId, r, req, reqCount, other)) return; // client_level.cpp
    if (classHasField(*cls, dc::DistributedBuilding::k_setBlock)) {
        handleCogBuildingEnter(doId, r, req, reqCount, other);
        return;
    }
    if (classHasField(*cls, dc::DistributedElevator::k_setBldgDoId)) {
        handleCogElevatorEnter(doId, classId, r, req, reqCount, other);
        return;
    }
    if (classId == dc::DistributedSuitInterior::kClassId) {
        handleCogInteriorEnter(doId, r, req, reqCount, other);
        return;
    }
    if (classHasField(*cls, dc::DistributedMinigame::k_setAvatarJoined)) {
        handleMinigameEnter(doId, classId, r, req, reqCount, other);
        return;
    }
    if (classHasField(*cls, dc::PurchaseManager::k_setPurchaseExit)) {
        handlePurchaseEnter(doId, classId, r, req, reqCount, other);
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
    if (classId == dc::DistributedBoat::kClassId) {
        handleBoatEnter(doId, r, req, reqCount, other);
        return;
    }
    if (classId == dc::DistributedFishingPond::kClassId || classId == dc::DistributedFishingSpot::kClassId) {
        handleFishingEnter(doId, classId, r, req, reqCount, other);
        return;
    }
    if (classId == dc::DistributedSuit::kClassId) {
        handleSuitEnter(doId, zone, r, req, reqCount, other);
        return;
    }
    if (classId == dc::DistributedBattle::kClassId ||
        classHasField(*cls, dc::DistributedBattle::k_setState)) {
        handleBattleEnter(doId, zone, r, req, reqCount, other);
        return;
    }
    if (classId == dc::DistributedSuitPlanner::kClassId) {
        // One per street, in the branch zone: setZoneId names the street DNA to walk.
        SuitPlanner* sp = nullptr;
        for (SuitPlanner& s : planners_) {
            if (s.active && s.doId == doId) sp = &s;
        }
        for (SuitPlanner& s : planners_) {
            if (!sp && !s.active) sp = &s;
        }
        if (!sp) return;
        sp->doId = doId;
        sp->zoneId = zone; // overwritten by setZoneId below
        for (uint16_t i = 0; i < reqCount && r.ok(); ++i) {
            if (req[i] == dc::DistributedSuitPlanner::k_setZoneId) sp->zoneId = r.u32();
            else skipField(r, req[i]);
        }
        if (other) {
            uint16_t count = r.u16();
            for (uint16_t i = 0; i < count && r.ok(); ++i) {
                const uint16_t fid = r.u16();
                if (fid == dc::DistributedSuitPlanner::k_setZoneId) sp->zoneId = r.u32();
                else skipField(r, fid);
            }
        }
        if (!r.ok()) return;
        sp->active = true;
        TT_LOG("net: suit planner %u for street %u", unsigned(doId), unsigned(sp->zoneId));
        pushEvent(EventType::SuitPlannerUp, doId);
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
    if (handleClerksField(r, doId, fieldId)) return; // clerks

    if (handleCogdoField(r, doId, fieldId)) return; // cogdo
    if (handleBossField(r, doId, fieldId)) return; // bosses
    if (friendsHandleSetField(r, doId, fieldId)) return; // friends hook (client_friends.cpp)
    if (doId == kLoginManagerId) {
        handleLoginManagerField(r, fieldId);
        return;
    }
    if (doId == estateManagerDoId_ && fieldId == dc::EstateManager::k_setEstateZone) {
        const uint32_t owner = r.u32();
        estateZone_ = r.u32();
        if (owner == avatarId_ && estateZone_ != 0) changeZone(estateZone_);
        pushEvent(EventType::EstateZone, estateZone_);
        return;
    }
    if (fieldId == dc::DistributedNPCClerk::k_setMovie) {
        handleClerkMovie(r, doId);
        return;
    }
    if (fieldId == dc::DistributedNPCTailor::k_setMovie) {
        handleTailorMovie(r, doId);
        return;
    }
    if (handleTrolleyField(r, doId, fieldId)) return;
    if (handleGolfField(r, doId, fieldId)) return;
    if (handleLevelField(r, doId, fieldId)) return; // client_level.cpp
    if (handleEstateField(r, doId, fieldId)) return;
    if (handleCogField(r, doId, fieldId)) return;
    if (handleRacingField(r, doId, fieldId)) return;
    for (NpcToon& n : npcs_) {
        if (n.active && n.doId == doId) {
            handleNpcField(n, r, fieldId);
            return;
        }
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
    if (boat_.active && boat_.doId == doId) {
        if (fieldId == dc::DistributedBoat::k_setState && applyBoatField(boat_, r, fieldId)) {
            pushEvent(EventType::BoatState, doId);
        }
        return;
    }
    for (CChar& c : cchars_) {
        if (c.active && c.doId == doId) {
            if (fieldId == dc::DistributedCCharBase::k_setWalk) {
                if (applyCCharField(c, r, fieldId)) pushEvent(EventType::CCharWalk, doId);
            } else if (fieldId == dc::DistributedCCharBase::k_setChat) {
                if (applyCCharField(c, r, fieldId)) pushEvent(EventType::CCharChat, doId);
            } else if (fieldId == dc::DistributedDale::k_setFollowChip ||
                       fieldId == dc::DistributedJailbirdDale::k_setFollowChip) {
                if (applyCCharField(c, r, fieldId)) pushEvent(EventType::CCharWalk, doId);
            } else if (fieldId == dc::DistributedDale::k_setChipId ||
                       fieldId == dc::DistributedJailbirdDale::k_setChipId) {
                applyCCharField(c, r, fieldId);
            }
            return;
        }
    }
    for (Battle& b : battles_) {
        if (b.active && b.doId == doId) {
            applyBattleField(b, r, fieldId, true);
            return;
        }
    }
    for (Suit& su : suits_) {
        if (su.active && su.doId == doId) {
            if (fieldId == dc::DistributedSuit::k_denyBattle) { // sent to us alone: the cog said no
                pushEvent(EventType::SuitDenied, doId);
            } else if (fieldId == dc::DistributedSuit::k_setBrushOff) {
                r.i16(); // index into SuitDialog.SuitBrushOffs (text not used)
                if (r.ok()) pushEvent(EventType::SuitBrushOff, doId);
            } else if (applySuitField(su, r, fieldId)) {
                pushEvent(EventType::SuitUpdate, doId);
            }
            return;
        }
    }
    if (handleFishingField(r, doId, fieldId)) return;
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
    // ---- clerks: a removed clerk cannot retain transaction authority ----
    if (clerkSession_.pending == doId || clerkSession_.movie.clerk == doId) resetClerks(false);
    // ---- end clerks ----

    cogdoLeaving(doId); // cogdo lifecycle
    bossLeaving(doId); // boss lifecycle hook
    friendsHandleLeaving(doId); // friends hook (client_friends.cpp)
    for (RacingPad& p : racingPads_) if (p.active && p.doId == doId) { p = RacingPad{}; pushEvent(EventType::RacingGone, doId); return; }
    for (RacingBlock& b : racingBlocks_) if (b.active && b.doId == doId) { b = RacingBlock{}; pushEvent(EventType::RacingGone, doId); return; }
    for (RacingVehicle& v : racingVehicles_) if (v.active && v.doId == doId) { v = RacingVehicle{}; pushEvent(EventType::RacingGone, doId); return; }
    if (racingRace_.active && racingRace_.doId == doId) { racingRace_ = RacingRace{}; pushEvent(EventType::RacingGone, doId); return; }
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
    if (handleTrolleyLeaving(doId)) return;
    if (handleGolfLeaving(doId)) return;
    if (handleLevelLeaving(doId)) return; // client_level.cpp
    if (estate_.active && estate_.doId == doId) { estate_ = Estate{}; pushEvent(EventType::EstateGone, doId); return; }
    if (closet_.active && closet_.doId == doId) { closet_ = Closet{}; pushEvent(EventType::ClosetGone, doId); return; }
    if (mailboxDoId_ == doId) { mailboxDoId_ = 0; return; }
    if (bankDoId_ == doId) { bankDoId_ = 0; return; }
    if (houseDoorDoId_ == doId) { houseDoorDoId_ = 0; }
    for (size_t i = 0; i < numDoors_; ++i) {
        if (doors_[i].doId == doId) {
            doors_[i] = doors_[--numDoors_];
            return;
        }
    }
    if (houseInterior_.active && houseInterior_.doId == doId) { houseInterior_ = HouseInterior{}; return; }
    for (EstateHouse& h : estate_.houses) if (h.active && h.doId == doId) { h = EstateHouse{}; return; }
    for (EstateFurniture& f : furniture_) if (f.active && f.doId == doId) { f = EstateFurniture{}; return; }
    for (Pet& p : pets_) if (p.active && p.doId == doId) { p = Pet{}; pushEvent(EventType::PetGone, doId); return; }
    for (CogBuilding& b : cogBuildings_) {
        if (b.active && b.doId == doId) {
            b.active = false;
            pushEvent(EventType::CogBuildingGone, doId);
            return;
        }
    }
    for (CogElevator& e : cogElevators_) {
        if (e.active && e.doId == doId) {
            e.active = false;
            return;
        }
    }
    if (cogInterior_.active && cogInterior_.doId == doId) {
        pushEvent(EventType::CogInteriorGone, doId);
        cogInterior_ = CogInterior{};
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
    if (boat_.active && boat_.doId == doId) {
        boat_.active = false;
        pushEvent(EventType::BoatGone, doId);
    }
    for (Suit& su : suits_) {
        if (su.active && su.doId == doId) {
            su.active = false;
            pushEvent(EventType::SuitGone, doId);
        }
    }
    for (SuitPlanner& sp : planners_) {
        if (sp.active && sp.doId == doId) sp.active = false;
    }
    handleFishingLeaving(doId);
    for (Battle& b : battles_) {
        if (b.active && b.doId == doId) {
            b.active = false;
            TT_LOG("net: battle %u gone", unsigned(doId));
            pushEvent(EventType::BattleGone, doId);
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
        uint32_t zones[kMaxVisZones + 2];
        int n = 0;
        zones[n++] = targetZone_;
        // A street cell (2101..2199, not an interior 2500+) also needs its branch zone, which
        // holds the street's DistributedBuildings (Street.doEnterZone, Astron path).
        const uint32_t branchZone = targetZone_ - targetZone_ % 100;
        const bool streetCell = targetZone_ < 10000 && targetZone_ % 100 != 0 && targetZone_ % 1000 < 500;
        if (streetCell) zones[n++] = branchZone;
        for (int i = 0; i < numVisZones_; ++i) {
            if (visZones_[i] != targetZone_ && !(streetCell && visZones_[i] == branchZone)) zones[n++] = visZones_[i];
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
        if (state_ == State::ChooseAvatar) {
            TT_LOG("net: updated avatar list (%d toons)", numAvatars_);
            return;
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
        if (pendingNameAction_ == 1) {
            sendSetNamePattern(avId,
                               pendingPattern_[0], pendingPatternFlags_[0],
                               pendingPattern_[1], pendingPatternFlags_[1],
                               pendingPattern_[2], pendingPatternFlags_[2],
                               pendingPattern_[3], pendingPatternFlags_[3]);
        } else if (pendingNameAction_ == 2) {
            sendSetNameTyped(avId, pendingTypedName_);
        } else {
            sendRequestPlayAvatar(avatarId_);
            setState(State::PlayAvatar);
        }
    } else if (fieldId == dc::AstronLoginManager::k_namePatternAnswer) {
        uint32_t avId = r.u32();
        uint8_t status = r.u8();
        TT_LOG("net: namePatternAnswer avId=%u status=%u", unsigned(avId), unsigned(status));
        namePatternResult_ = status;
        for (int i = 0; i < numAvatars_; ++i) {
            if (avatars_[i].id == avId) {
                if (status == 1) {
                    avatars_[i].nameState = static_cast<uint8_t>(NameState::Locked);
                    if (pendingPatternName_[0] != '\0') {
                        snprintf(avatars_[i].name, sizeof(avatars_[i].name), "%s", pendingPatternName_);
                    }
                }
                break;
            }
        }
        if (state_ == State::CreateAvatar && pendingNameAction_ == 1) {
            if (status == 1 && pendingPatternName_[0] != '\0') {
                snprintf(ownName_, sizeof(ownName_), "%s", pendingPatternName_);
            }
            sendRequestPlayAvatar(avatarId_);
            setState(State::PlayAvatar);
        }
    } else if (fieldId == dc::AstronLoginManager::k_nameTypedResponse) {
        uint32_t avId = r.u32();
        uint8_t status = r.u8();
        TT_LOG("net: nameTypedResponse avId=%u status=%u", unsigned(avId), unsigned(status));
        nameTypedResult_ = status;
        for (int i = 0; i < numAvatars_; ++i) {
            if (avatars_[i].id == avId) {
                if (status == 1) {
                    avatars_[i].nameState = static_cast<uint8_t>(NameState::Pending);
                } else if (status == 2) {
                    avatars_[i].nameState = static_cast<uint8_t>(NameState::Locked);
                    if (pendingTypedName_[0] != '\0') {
                        snprintf(avatars_[i].name, sizeof(avatars_[i].name), "%s", pendingTypedName_);
                    }
                }
                break;
            }
        }
        if (state_ == State::CreateAvatar && pendingNameAction_ == 2) {
            sendRequestPlayAvatar(avatarId_);
            setState(State::PlayAvatar);
        }
    } else if (fieldId == dc::AstronLoginManager::k_acknowledgeAvatarNameResponse) {
        TT_LOG("net: acknowledgeAvatarNameResponse");
        if (pendingAckAvId_ != 0) {
            for (int i = 0; i < numAvatars_; ++i) {
                if (avatars_[i].id == pendingAckAvId_) {
                    if (avatars_[i].nameState == static_cast<uint8_t>(NameState::Approved)) {
                        avatars_[i].nameState = static_cast<uint8_t>(NameState::Locked);
                    } else if (avatars_[i].nameState == static_cast<uint8_t>(NameState::Rejected)) {
                        avatars_[i].nameState = static_cast<uint8_t>(NameState::Open);
                    }
                    break;
                }
            }
            pendingAckAvId_ = 0;
        }
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
    if (clerksOwnField(r, fieldId)) return r.ok(); // clerks

    if (bossOwnField(r, fieldId)) return r.ok();
    if (friendsOwnField(r, fieldId)) return r.ok(); // friends hook (client_friends.cpp)
    if (fieldId == dc::DistributedToon::k_setName) {
        r.string(ownName_, sizeof(ownName_));
    } else if (fieldId == dc::DistributedToon::k_setDNAString) {
        Bytes b = r.blob();
        if (r.ok()) memcpy(ownDna_, b.data, b.len < kDnaSize ? b.len : kDnaSize);
        if (r.ok()) {
            char hex[2 * kDnaSize + 1];
            for (uint32_t i = 0; i < kDnaSize && i < b.len; i++) snprintf(hex + 2 * i, 3, "%02x", ownDna_[i]);
            TT_LOG("net: our DNA %s", hex);
        }
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
    } else if (fieldId == dc::DistributedToon::k_setDefaultZone) {
        ownStats_.defaultZone = r.u32(); // where a sad toon goes (DistributedToon.died)
        if (!r.ok()) return false;
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
        r.skip(bytes & 1); // malformed odd length: stay aligned with the next field
        pushEvent(EventType::Stats, avatarId_);
    } else if (fieldId == dc::DistributedToon::k_setEmoteAccess) {
        const Bytes access=r.blob();
        if (!r.ok()) return false;
        memset(ownStats_.emoteAccess,0,sizeof(ownStats_.emoteAccess));
        for (uint32_t i=0;i<access.len && i<25;i++) ownStats_.emoteAccess[i]=access.data[i]?1:0;
        ownStats_.hasEmotes=true;
    } else if (fieldId == dc::DistributedToon::k_setMaxCarry) {
        ownStats_.maxCarry = r.u8();
    } else if (fieldId == dc::DistributedToon::k_setFishingRod || fieldId == dc::DistributedToon::k_setMaxFishTank ||
               fieldId == dc::DistributedToon::k_setFishTank) {
        applyOwnFishField(r, fieldId);
    } else if (fieldId == dc::DistributedToon::k_setBattleId) {
        ownBattleId_ = r.u32(); // battle section (the AI first sets our own doId, then the battle's)
    } else if (fieldId == dc::DistributedToon::k_setQuests) {
        if (!parseQuests(r, ownQuests_)) return false;
        pushEvent(EventType::Quests, avatarId_);
    } else if (fieldId == dc::DistributedToon::k_setQuestHistory) {
        if (!parseQuestHistory(r, ownQuests_)) return false;
    } else if (fieldId == dc::DistributedToon::k_setRewardHistory) {
        if (!parseRewardHistory(r, ownQuests_)) return false;
    } else if (fieldId == dc::DistributedToon::k_setQuestCarryLimit) {
        ownQuests_.carryLimit = r.u8();
        ownQuests_.version++;
        pushEvent(EventType::Quests, avatarId_);
    } else if (parseOwnBookField(r, fieldId, ownBook_)) {
        pushEvent(EventType::Stats, avatarId_);
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
    tmp.questGiver = classHasField(cls, dc::DistributedNPCToon::k_chooseQuest) ||
                     classHasField(cls, dc::DistributedNPCSpecialQuestGiver::k_chooseQuest);
    tmp.special = classHasField(cls, dc::DistributedNPCSpecialQuestGiver::k_chooseQuest);
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
        c.hasFollowChip = false;
    } else if (fieldId == dc::DistributedCCharBase::k_setChat) {
        c.chatCategory = r.u32();
        c.chatMsgIndex = r.u32();
        c.chatAvatarDoId = r.u32();
    } else if (fieldId == dc::DistributedDale::k_setFollowChip ||
               fieldId == dc::DistributedJailbirdDale::k_setFollowChip) {
        r.string(c.src, sizeof(c.src));
        r.string(c.dst, sizeof(c.dst));
        const int16_t ts = r.i16();
        c.walkStartMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
        c.offsetX = r.i16() / 100.0f;
        c.offsetY = r.i16() / 100.0f;
        c.hasFollowChip = true;
    } else if (fieldId == dc::DistributedDale::k_setChipId ||
               fieldId == dc::DistributedJailbirdDale::k_setChipId) {
        c.chipId = r.u32();
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

bool Client::applyBoatField(Boat& b, DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedBoat::k_setState) {
        char name[16];
        r.string(name, sizeof(name));
        const int16_t ts = r.i16();
        if (!r.ok()) return false;
        // DistributedBoat.setState: fsm.request(state, [localElapsedTime(ts)]); the track
        // starts that far into its run, so the state began at networkToLocal(ts).
        b.state = !strcmp(name, "DockedEast")    ? BoatState::DockedEast
                  : !strcmp(name, "SailingWest") ? BoatState::SailingWest
                  : !strcmp(name, "DockedWest")  ? BoatState::DockedWest
                  : !strcmp(name, "SailingEast") ? BoatState::SailingEast
                                                 : BoatState::Off;
        b.stateStartMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
    } else {
        skipField(r, fieldId);
    }
    return r.ok();
}

// ---- street cogs (DistributedSuit) ----
void Client::clearSuits() {
    for (Suit& su : suits_) {
        if (su.active) pushEvent(EventType::SuitGone, su.doId);
        su.active = false;
    }
    for (SuitPlanner& sp : planners_) sp.active = false;
}

bool Client::applySuitField(Suit& s, DatagramReader& r, uint16_t fieldId) {
    switch (fieldId) {
    case dc::DistributedSuit::k_setDNAString: {
        // SuitDNA.makeFromNetString: 's', a 3-byte NUL padded head code, a dept letter.
        Bytes b = r.blob();
        if (!r.ok()) return false;
        s.name[0] = 0;
        s.dept = 0;
        s.hasDna = false;
        if (b.len >= 5 && b.data[0] == 's') {
            int n = 0;
            for (int i = 0; i < 3 && b.data[1 + i]; ++i) s.name[n++] = char(b.data[1 + i]);
            s.name[n] = 0;
            s.dept = char(b.data[4]);
            s.hasDna = n > 0;
        }
        break;
    }
    case dc::DistributedSuit::k_setLevelDist: s.levelDist = r.i16(); break;
    case dc::DistributedSuit::k_setSkelecog: s.skelecog = r.u8(); break;
    case dc::DistributedSuit::k_setSkeleRevives: s.skeleRevives = r.u8(); break;
    case dc::DistributedSuit::k_setHP: s.hp = r.i16(); break;
    case dc::DistributedSuit::k_setSPDoId: s.spDoId = r.u32(); break;
    case dc::DistributedSuit::k_setPathEndpoints:
        s.pathStart = r.u16();
        s.pathEnd = r.u16();
        s.pathMinLen = r.u16();
        s.pathMaxLen = r.u16();
        break;
    case dc::DistributedSuit::k_setPathPosition: {
        s.pathIndex = r.u16();
        const int16_t ts = r.i16();
        // pathStartTime = networkToLocalTime(timestamp) - legList.getStartTime(index)
        s.pathPosMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
        break;
    }
    case dc::DistributedSuit::k_setPathState: s.pathState = r.i8(); break;
    default: // setBrushOff (chat only), denyBattle, anything inherited
        skipField(r, fieldId);
        break;
    }
    return r.ok();
}

void Client::handleBoatEnter(uint32_t doId, DatagramReader& r, const uint16_t* req, uint16_t reqCount, bool other) {
    Boat tmp{};
    tmp.doId = doId;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyBoatField(tmp, r, req[i]);
    if (ok && other) {
        uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && ok && r.ok(); ++i) ok = applyBoatField(tmp, r, r.u16());
    }
    if (!ok || !r.ok()) {
        TT_LOG("net: boat %u generate did not parse", unsigned(doId));
        return;
    }
    tmp.active = true;
    boat_ = tmp;
    TT_LOG("net: boat %u state %d", unsigned(doId), int(tmp.state));
    pushEvent(EventType::BoatSpawn, doId);
}

bool Client::handleSuitEnter(uint32_t doId, uint32_t zone, DatagramReader& r, const uint16_t* req, uint16_t reqCount,
                             bool other) {
    Suit* su = nullptr;
    for (Suit& s : suits_) {
        if (s.active && s.doId == doId) su = &s;
    }
    for (Suit& s : suits_) {
        if (!su && !s.active) su = &s;
    }
    if (!su) {
        TT_LOG("net: no room for cog %u", unsigned(doId));
        return false;
    }
    Suit tmp{};
    tmp.doId = doId;
    tmp.zoneId = zone;
    tmp.pathPosMs = now_;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applySuitField(tmp, r, req[i]);
    if (ok && other) {
        uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && ok && r.ok(); ++i) ok = applySuitField(tmp, r, r.u16());
    }
    if (!ok || !r.ok()) {
        TT_LOG("net: cog %u generate did not parse", unsigned(doId));
        return false;
    }
    const bool fresh = !su->active;
    tmp.active = true;
    *su = tmp;
    TT_LOG("net: cog %u \"%s\" lvl %d zone %u path %u->%u leg %u state %d", unsigned(doId), su->name,
           int(su->levelDist), unsigned(zone), unsigned(su->pathStart), unsigned(su->pathEnd), unsigned(su->pathIndex),
           int(su->pathState));
    pushEvent(fresh ? EventType::SuitSpawn : EventType::SuitUpdate, doId);
    return true;
}

const Suit* Client::findSuit(uint32_t doId) const {
    for (const Suit& s : suits_) {
        if (s.active && s.doId == doId) return &s;
    }
    return nullptr;
}

uint32_t Client::suitPlannerZone(uint32_t doId) const {
    for (const SuitPlanner& sp : planners_) {
        if (sp.active && sp.doId == doId) return sp.zoneId;
    }
    return 0;
}

// =========================================================================================
// ---- battle section: DistributedBattle (docs/design/combat.md sections 4 to 7) ----
// =========================================================================================
namespace {

// A DC array field: u16 byte length, then elements. Reads up to cap elements, drops the rest.
int readU32Array(DatagramReader& r, uint32_t* out, int cap) {
    const uint16_t bytes = r.u16();
    const int n = bytes / 4;
    int stored = 0;
    for (int i = 0; i < n && r.ok(); ++i) {
        const uint32_t v = r.u32();
        if (stored < cap) out[stored++] = v;
    }
    if (bytes % 4) r.skip(bytes % 4);
    return stored;
}

int readI16Array(DatagramReader& r, int16_t* out, int cap) {
    const uint16_t bytes = r.u16();
    const int n = bytes / 2;
    int stored = 0;
    for (int i = 0; i < n && r.ok(); ++i) {
        const int16_t v = r.i16();
        if (stored < cap) out[stored++] = v;
    }
    if (bytes % 2) r.skip(bytes % 2);
    return stored;
}

void skipArray(DatagramReader& r) {
    const uint16_t bytes = r.u16();
    r.skip(bytes);
}

// setMembers sends each role list as a string of digits, each digit an index into suits/toons.
uint8_t digitMask(Bytes s) {
    uint8_t m = 0;
    for (uint32_t i = 0; i < s.len; ++i) {
        const int d = int(s.data[i]) - '0';
        if (d >= 0 && d < 8) m |= uint8_t(1u << d);
    }
    return m;
}

BattleStateId battleStateFromName(const char* name) {
    static const struct { const char* name; BattleStateId id; } kStates[] = {
        {"Off", BattleStateId::Off},
        {"FaceOff", BattleStateId::FaceOff},
        {"WaitForJoin", BattleStateId::WaitForJoin},
        {"WaitForInput", BattleStateId::WaitForInput},
        {"MakeMovie", BattleStateId::MakeMovie},
        {"PlayMovie", BattleStateId::PlayMovie},
        {"Reward", BattleStateId::Reward},
        // The subclasses' own reward states end the same way (play the reward, rewardDone):
        // DistributedBattleBldg, DistributedBattleFactory, ...Mint, ...Stage, ...CountryClub.
        {"BuildingReward", BattleStateId::Reward},
        {"FactoryReward", BattleStateId::Reward},
        {"MintReward", BattleStateId::Reward},
        {"StageReward", BattleStateId::Reward},
        {"CountryClubReward", BattleStateId::Reward},
        {"Resume", BattleStateId::Resume},
    };
    for (const auto& s : kStates) {
        if (!strcmp(s.name, name)) return s.id;
    }
    return BattleStateId::Other;
}

} // namespace

void Client::clearBattles() {
    for (Battle& b : battles_) {
        if (b.active) pushEvent(EventType::BattleGone, b.doId);
        b.active = false;
    }
}

bool Client::parseBattleMembers(Battle& b, DatagramReader& r) {
    // suits[], suitsJoining, suitsPending, suitsActive, suitsLured, suitTraps,
    // toons[], toonsJoining, toonsPending, toonsActive, toonsRunning, timestamp
    b.numSuits = uint8_t(readU32Array(r, b.suits, kBattleSuits));
    b.suitsJoining = digitMask(r.blob());
    b.suitsPending = digitMask(r.blob());
    b.suitsActive = digitMask(r.blob());
    b.suitsLured = digitMask(r.blob());
    const Bytes traps = r.blob();
    for (int i = 0; i < kBattleSuits; ++i) {
        // One char per suit: the trap level, or '9' for none (BattleBase NO_TRAP = -1).
        const int d = uint32_t(i) < traps.len ? int(traps.data[i]) - '0' : 9;
        b.suitTrap[i] = int8_t(d >= 0 && d < 9 ? d : -1);
    }
    b.numToons = uint8_t(readU32Array(r, b.toons, kBattleToons));
    b.toonsJoining = digitMask(r.blob());
    b.toonsPending = digitMask(r.blob());
    b.toonsActive = digitMask(r.blob());
    b.toonsRunning = digitMask(r.blob());
    r.i16(); // timestamp (joins and adjusts are timed from their own messages)
    if (!r.ok()) return false;
    // The masks only mean something for members that exist.
    const uint8_t sm = uint8_t((1u << b.numSuits) - 1), tm = uint8_t((1u << b.numToons) - 1);
    b.suitsJoining &= sm, b.suitsPending &= sm, b.suitsActive &= sm, b.suitsLured &= sm;
    b.toonsJoining &= tm, b.toonsPending &= tm, b.toonsActive &= tm, b.toonsRunning &= tm;
    ++b.membersSeq;
    return true;
}

bool Client::parseBattleMovie(Battle& b, DatagramReader& r) {
    BattleMovie m{};
    m.active = r.i8() == 1;
    m.numToons = uint8_t(readU32Array(r, m.toons, kBattleToons));
    m.numSuits = uint8_t(readU32Array(r, m.suits, kBattleSuits));
    for (BattleToonAttack& a : m.toonAttacks) {
        a.toonIdx = r.i8();
        a.track = r.i8();
        a.level = r.i8();
        a.target = r.i32();
        a.numHp = uint8_t(readI16Array(r, a.hp, kBattleSuits));
        a.accBonus = r.i16();
        a.hpBonus = r.i16();
        skipArray(r); // kbBonus[]
        a.died = r.i8();
        a.revive = r.i8();
    }
    for (BattleSuitAttack& a : m.suitAttacks) {
        a.suitIdx = r.i8();
        a.attack = r.i8();
        a.target = r.i8();
        a.numHp = uint8_t(readI16Array(r, a.hp, kBattleToons));
        a.toonDied = r.i8();
        a.beforeToons = r.i8();
        a.taunt = r.i8();
    }
    if (!r.ok()) return false;
    b.movie = m;
    if (m.active) ++b.movieSeq;
    return true;
}

bool Client::parseBattleExperience(Battle& b, DatagramReader& r) {
    BattleExperience e{};
    for (BattleExpToon& t : e.toons) {
        t.id = r.i32();
        readI16Array(r, t.origExp, 7);
        readI16Array(r, t.earnedExp, 7);
        skipArray(r); // origQuests
        skipArray(r); // items
        skipArray(r); // missedItems
        skipArray(r); // origMerits
        skipArray(r); // merits
        skipArray(r); // parts
    }
    const uint16_t deathBytes = r.u16(); // groups of 4: head type index, level, toon bits, flags
    for (uint16_t i = 0; i + 3 < deathBytes && r.ok(); i += 4) {
        uint8_t g[4];
        for (uint8_t& v : g) v = r.u8();
        if (e.numDeathGroups < kMaxDeathGroups) memcpy(e.deaths[e.numDeathGroups++], g, 4);
    }
    r.skip(deathBytes % 4);
    skipArray(r); // uberList
    skipArray(r); // helpfulToons
    if (!r.ok()) return false;
    b.exp = e;
    ++b.expSeq;
    return true;
}

bool Client::applyBattleField(Battle& b, DatagramReader& r, uint16_t fieldId, bool live) {
    if (bossBattleField(b, r, fieldId)) return r.ok();
    switch (fieldId) {
    case dc::DistributedBattle::k_setPosition:
        b.x = r.i16() / 10.0f;
        b.y = r.i16() / 10.0f;
        b.z = r.i16() / 10.0f;
        break;
    case dc::DistributedBattle::k_setInitialSuitPos:
        b.sx = r.i16() / 10.0f;
        b.sy = r.i16() / 10.0f;
        b.sz = r.i16() / 10.0f;
        break;
    case dc::DistributedBattle::k_setZoneId: b.zoneId = r.u32(); break;
    case dc::DistributedBattle::k_setLevelDoId: b.levelDoId = r.u32(); break;
    case dc::DistributedBattle::k_setBattleCellId: b.battleCellId = r.u32(); break;
    case dc::DistributedBattle::k_setMembers:
        if (parseBattleMembers(b, r) && live) pushEvent(EventType::BattleMembers, b.doId);
        break;
    case dc::DistributedBattle::k_setMovie:
        if (parseBattleMovie(b, r) && live && b.movie.active) pushEvent(EventType::BattleMovie, b.doId);
        break;
    case dc::DistributedBattle::k_setBattleExperience:
        if (parseBattleExperience(b, r) && live) pushEvent(EventType::BattleExperience, b.doId);
        break;
    case dc::DistributedBattle::k_setState: {
        r.string(b.stateName, sizeof(b.stateName));
        const int16_t ts = r.i16();
        if (!r.ok()) return false;
        b.state = battleStateFromName(b.stateName);
        // The AI stamps the state two seconds ahead (SERVER_BUFFER_TIME); the input countdown runs
        // from that moment (DistributedBattleBase.enterWaitForInput passes the elapsed time).
        b.stateMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
        ++b.stateSeq;
        if (live) {
            TT_LOG("net: battle %u state %s", unsigned(b.doId), b.stateName);
            pushEvent(EventType::BattleState, b.doId);
        }
        break;
    }
    case dc::DistributedBattle::k_adjust: {
        const int16_t ts = r.i16();
        if (!r.ok()) return false;
        b.adjustMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
        ++b.adjustSeq;
        if (live) pushEvent(EventType::BattleAdjust, b.doId);
        break;
    }
    case dc::DistributedBattle::k_denyLocalToonJoin:
        if (live) pushEvent(EventType::BattleJoinDenied, b.doId);
        break;
    default: // setLevelDoId, setBattleCellId, setInteractivePropTrackBonus, setChosenToonAttacks, setBossBattle
        skipField(r, fieldId);
        break;
    }
    return r.ok();
}

bool Client::handleBattleEnter(uint32_t doId, uint32_t zone, DatagramReader& r, const uint16_t* req,
                               uint16_t reqCount, bool other) {
    Battle* slot = nullptr;
    for (Battle& b : battles_) {
        if (b.active && b.doId == doId) slot = &b;
    }
    for (Battle& b : battles_) {
        if (!slot && !b.active) slot = &b;
    }
    if (!slot) {
        TT_LOG("net: no room for battle %u", unsigned(doId));
        return false;
    }
    Battle tmp{};
    tmp.doId = doId;
    tmp.zoneId = zone;
    tmp.state = BattleStateId::None;
    tmp.stateMs = now_;
    tmp.movie.active = false;
    for (BattleExpToon& t : tmp.exp.toons) t.id = -1;
    for (int8_t& t : tmp.suitTrap) t = -1;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyBattleField(tmp, r, req[i], false);
    if (ok && other) {
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && ok && r.ok(); ++i) ok = applyBattleField(tmp, r, r.u16(), false);
    }
    if (!ok || !r.ok()) {
        TT_LOG("net: battle %u generate did not parse", unsigned(doId));
        return false;
    }
    positionBossBattle(tmp);
    tmp.active = true;
    // DistributedLevelBattle.setBattleCellId: facility battles sit on their spec battle cell
    // (setPosition is a no-op there); the level runtime knows where that is in our world.
    float lx, ly, lz, lh;
    if (tmp.levelDoId && levelBattlePlacer_ &&
        levelBattlePlacer_(levelBattlePlacerUser_, tmp.levelDoId, tmp.battleCellId, &lx, &ly, &lz, &lh)) {
        const float rad = lh * 0.01745329252f;
        tmp.x = lx, tmp.y = ly, tmp.z = lz;
        tmp.sx = lx - std::sin(rad), tmp.sy = ly + std::cos(rad), tmp.sz = lz; // heading lh
    }
    *slot = tmp;
    TT_LOG("net: battle %u zone %u at %.1f,%.1f,%.1f state %s: %d cogs, %d toons", unsigned(doId), unsigned(slot->zoneId),
           double(slot->x), double(slot->y), double(slot->z), slot->stateName, int(slot->numSuits),
           int(slot->numToons));
    pushEvent(EventType::BattleSpawn, doId);
    return true;
}

const Battle* Client::findBattle(uint32_t doId) const {
    for (const Battle& b : battles_) {
        if (b.active && b.doId == doId) return &b;
    }
    return nullptr;
}

const Battle* Client::localBattle() const {
    if (avatarId_ == 0) return nullptr;
    for (const Battle& b : battles_) {
        if (!b.active) continue;
        for (int i = 0; i < b.numToons; ++i) {
            if (b.toons[i] == avatarId_) return &b;
        }
    }
    return nullptr;
}

bool Client::sendRequestBattle(uint32_t suitDoId, float x, float y, float z, float h, float p, float r) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, suitDoId, DistributedSuit, requestBattle);
    w.i16(packFixed(x, 10.0f));
    w.i16(packFixed(y, 10.0f));
    w.i16(packFixed(z, 10.0f));
    w.i16(packFixed(h, 10.0f)); // int16/10 (not %360): the AI hands it straight to the battle
    w.i16(packFixed(p, 10.0f));
    w.i16(packFixed(r, 10.0f));
    TT_LOG("net: requestBattle on cog %u at %.1f,%.1f,%.1f h %.1f", unsigned(suitDoId), double(x), double(y),
           double(z), double(h));
    return finish(w);
}

// Field senders with no arguments share this shape; the macro's static_assert pins clsend.
#define TT_BATTLE_SEND0(fn, field)                                                      \
    bool Client::fn(uint32_t battleId) {                                                \
        if (state_ != State::InZone || battleId == 0) return false;                     \
        DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);                  \
        TT_NET_UPDATE(w, battleId, DistributedBattle, field);                           \
        TT_LOG("net: battle %u: %s", unsigned(battleId), #field);                       \
        return finish(w);                                                               \
    }
TT_BATTLE_SEND0(sendBattleFaceOffDone, faceOffDone)
TT_BATTLE_SEND0(sendBattleRequestRun, toonRequestRun)
TT_BATTLE_SEND0(sendBattleTimeout, timeout)
TT_BATTLE_SEND0(sendBattleAdjustDone, adjustDone)
TT_BATTLE_SEND0(sendBattleMovieDone, movieDone)
TT_BATTLE_SEND0(sendBattleRewardDone, rewardDone)
#undef TT_BATTLE_SEND0

bool Client::sendBattleRequestJoin(uint32_t battleId, float x, float y, float z) {
    if (state_ != State::InZone || battleId == 0) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, battleId, DistributedBattle, toonRequestJoin);
    w.i16(packFixed(x, 10.0f));
    w.i16(packFixed(y, 10.0f));
    w.i16(packFixed(z, 10.0f));
    TT_LOG("net: battle %u: toonRequestJoin", unsigned(battleId));
    return finish(w);
}

bool Client::sendBattleJoinDone(uint32_t battleId, uint32_t avId) {
    if (state_ != State::InZone || battleId == 0) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, battleId, DistributedBattle, joinDone);
    w.u32(avId);
    TT_LOG("net: battle %u: joinDone %u", unsigned(battleId), unsigned(avId));
    return finish(w);
}

bool Client::sendBattleRequestAttack(uint32_t battleId, int8_t track, int8_t level, int32_t av) {
    if (state_ != State::InZone || battleId == 0) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, battleId, DistributedBattle, requestAttack);
    w.i8(track);
    w.i8(level);
    w.i32(av);
    TT_LOG("net: battle %u: requestAttack track %d level %d target %d", unsigned(battleId), int(track), int(level),
           int(av));
    return finish(w);
}
// ---- end of battle section ----

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

// ---- shop ------------------------------------------------------------------
// DistributedNPCClerk (docs/design/shop.md). setMovie is `broadcast`, so every toon in the
// zone sees it; we keep only movies about us, plus CLEAR (which frees the clerk).
void Client::handleClerkMovie(DatagramReader& r, uint32_t doId) {
    const NpcToon* n = findNpc(doId);
    if (!n || n->classId != dc::DistributedNPCClerk::kClassId) return;
    const uint8_t mode = r.u8();
    const uint32_t npcId = r.u32();
    const uint32_t avId = r.u32();
    const int16_t ts = r.i16();
    if (!r.ok()) return;
    const bool ours = avId == avatarId_ && avatarId_ != 0;
    if (!ours && mode != kPurchaseMovieClear) return;
    if (mode == kPurchaseMovieClear && shopMovie_.clerkDoId != doId) return;
    ShopMovie m;
    m.clerkDoId = doId;
    m.mode = mode;
    m.npcId = npcId;
    m.avId = avId;
    // remain = CLERK_COUNTDOWN_TIME - localElapsedTime(timestamp); never trust more than the full window.
    uint32_t start = now_;
    if (timeSynced_) {
        const uint32_t t = networkToLocal(ts, now_);
        if (int32_t(now_ - t) >= 0) start = t;
    }

    m.deadlineMs = start + kClerkCountdownMs;
    if (int32_t(m.deadlineMs - now_) > int32_t(kClerkCountdownMs)) m.deadlineMs = now_ + kClerkCountdownMs;
    m.seq = shopMovie_.seq + 1;
    shopMovie_ = m;
    TT_LOG("net: clerk %u setMovie mode %u av %u", unsigned(doId), unsigned(mode), unsigned(avId));
    pushEvent(EventType::ShopMovie, doId);
}

void Client::handleTailorMovie(DatagramReader& r, uint32_t doId) {
    const NpcToon* n = findNpc(doId);
    if (!n || n->classId != dc::DistributedNPCTailor::kClassId) return;
    const uint8_t mode = r.u8();
    const uint32_t npcId = r.u32();
    const uint32_t avId = r.u32();
    const int16_t ts = r.i16();
    if (!r.ok()) return;
    const bool ours = avId == avatarId_ && avatarId_ != 0;
    if (!ours && mode != kTailorMovieClear) return;
    if (mode == kTailorMovieClear && tailorMovie_.tailorDoId != doId) return;
    TailorMovie m;
    m.tailorDoId = doId;
    m.mode = mode;
    m.npcId = npcId;
    m.avId = avId;
    uint32_t start = now_;
    if (timeSynced_) {
        const uint32_t t = networkToLocal(ts, now_);
        if (int32_t(now_ - t) >= 0) start = t;
    }
    m.deadlineMs = start + kTailorCountdownMs;
    if (int32_t(m.deadlineMs - now_) > int32_t(kTailorCountdownMs)) m.deadlineMs = now_ + kTailorCountdownMs;
    m.seq = tailorMovie_.seq + 1;
    tailorMovie_ = m;
    TT_LOG("net: tailor %u setMovie mode %u av %u", unsigned(doId), unsigned(mode), unsigned(avId));
    pushEvent(EventType::TailorMovie, doId);
}

bool Client::sendClerkAvatarEnter(uint32_t clerkDoId) {
    if (state_ != State::InZone) return false;
    const NpcToon* n = findNpc(clerkDoId);
    if (!n || n->classId != dc::DistributedNPCClerk::kClassId) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, clerkDoId, DistributedNPCToonBase, avatarEnter);
    return finish(w);
}

bool Client::sendClerkSetInventory(uint32_t clerkDoId, const uint8_t inv[7][7], int16_t money, bool done) {
    if (state_ != State::InZone || !shopOpenFor(clerkDoId)) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, clerkDoId, DistributedNPCClerk, setInventory);
    uint8_t blob[49]; // InventoryBase.makeNetString: uint8 per track and level
    for (int i = 0; i < 49; i++) blob[i] = inv[i / 7][i % 7];
    w.blob(blob, sizeof(blob));
    w.i16(money);
    w.u8(done ? 1 : 0);
    return finish(w);
}

bool Client::sendTailorAvatarEnter(uint32_t tailorDoId) {
    if (state_ != State::InZone) return false;
    const NpcToon* n = findNpc(tailorDoId);
    if (!n || n->classId != dc::DistributedNPCTailor::kClassId) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, tailorDoId, DistributedNPCToonBase, avatarEnter);
    return finish(w);
}

bool Client::sendTailorDNA(uint32_t tailorDoId, const uint8_t dna[kDnaSize], int8_t finished, uint8_t which) {
    if (state_ != State::InZone || !tailorOpenFor(tailorDoId) || !world::isValidNetString(dna, kDnaSize)) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, tailorDoId, DistributedNPCTailor, setDNA);
    w.blob(dna, kDnaSize);
    w.i8(finished);
    w.u8(which);
    return finish(w);
}

bool Client::sendRequestGrab(uint32_t treasureDoId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, treasureDoId, DistributedTreasure, requestGrab);
    return finish(w);
}

// ==== quests (docs/design/quests.md) begin ====
bool Client::handleNpcField(NpcToon& n, DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedNPCToon::k_setMovie || fieldId == dc::DistributedNPCSpecialQuestGiver::k_setMovie) {
        NpcMovie m;
        m.npcDoId = n.doId;
        if (!parseNpcMovie(r, m)) {
            TT_LOG("net: bad setMovie for NPC %u", unsigned(n.doId));
            return false;
        }
        if (movieTail_ - movieHead_ == uint32_t(kMovieQueue)) movieHead_++; // drop the oldest
        movies_[movieTail_++ % kMovieQueue] = m;
        TT_LOG("net: NPC %u movie mode %u for %u (%d values)", unsigned(n.doId), unsigned(m.mode), unsigned(m.avId),
               m.numValues);
        pushEvent(EventType::NpcMovie, n.doId);
        return true;
    }
    if (fieldId == dc::DistributedNPCToonBase::k_freeAvatar) {
        pushEvent(EventType::NpcFree, n.doId);
        return true;
    }
    return true; // setAnimState, setPageNumber, ...: nothing we draw
}

bool Client::pollNpcMovie(NpcMovie& out) {
    if (movieHead_ == movieTail_) return false;
    out = movies_[movieHead_++ % kMovieQueue];
    return true;
}

bool Client::sendNpcAvatarEnter(uint32_t npcDoId) {
    const NpcToon* n = findNpc(npcDoId);
    if (state_ != State::InZone || !n || !n->questGiver) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, npcDoId, DistributedNPCToonBase, avatarEnter);
    return finish(w);
}

bool Client::sendChooseQuest(uint32_t npcDoId, uint16_t questId) {
    const NpcToon* n = findNpc(npcDoId);
    if (state_ != State::InZone || !n || !n->questGiver) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (n->special) {
        TT_NET_UPDATE(w, npcDoId, DistributedNPCSpecialQuestGiver, chooseQuest);
    } else {
        TT_NET_UPDATE(w, npcDoId, DistributedNPCToon, chooseQuest);
    }
    w.u16(questId);
    return finish(w);
}

bool Client::sendChooseTrack(uint32_t npcDoId, int8_t track) {
    const NpcToon* n = findNpc(npcDoId);
    if (state_ != State::InZone || !n || !n->questGiver) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (n->special) {
        TT_NET_UPDATE(w, npcDoId, DistributedNPCSpecialQuestGiver, chooseTrack);
    } else {
        TT_NET_UPDATE(w, npcDoId, DistributedNPCToon, chooseTrack);
    }
    w.i8(track);
    return finish(w);
}

bool Client::sendNpcMovieDone(uint32_t npcDoId) {
    const NpcToon* n = findNpc(npcDoId);
    if (state_ != State::InZone || !n || !n->questGiver) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (n->special) {
        TT_NET_UPDATE(w, npcDoId, DistributedNPCSpecialQuestGiver, setMovieDone);
    } else {
        TT_NET_UPDATE(w, npcDoId, DistributedNPCToon, setMovieDone);
    }
    return finish(w);
}

bool Client::sendDeleteQuest(int index) {
    if (state_ != State::InZone || index < 0 || index >= ownQuests_.count) return false;
    const QuestEntry& e = ownQuests_.quests[index];
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_OWN_UPDATE(w, DistributedToon, requestDeleteQuest);
    w.u16(5 * 4);
    w.u32(e.questId);
    w.u32(e.fromNpc);
    w.u32(e.toNpc);
    w.u32(e.reward);
    w.u32(e.progress);
    return finish(w);
}
// ==== quests end ====

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
    case dc::DistributedToon::k_setEmoteState: {
        const int id=r.i16();
        r.i16(); // stock Toon.doEmote ignores the multiplier
        const int16_t stamp=r.i16();
        if (!r.ok()) return false;
        if (id>=0 && id<25) {
            const uint32_t time=timeSynced_ ? networkToLocal(stamp,now_) : now_;
            if (t.emoteSerial && (int16_t(uint16_t(stamp)-uint16_t(t.emoteStamp))<0 ||
                (t.emoteId==id && t.emoteStamp==stamp))) break;
            t.emoteId=static_cast<int16_t>(id);t.emoteStamp=stamp;t.emoteMs=time;++t.emoteSerial;
            if (t.active) pushEvent(EventType::Emote,t.doId);
        }
        break; // incoming -1 is the stock no-op clear sentinel
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
    case dc::DistributedToon::k_setTunnelIn: {
        const int16_t ts = r.i16();
        const int16_t endX = r.i16();
        const int16_t x = r.i16();
        const int16_t y = r.i16();
        const int16_t z = r.i16();
        const int32_t h = r.i32();
        if (!r.ok()) return false;
        t.tunnelEndX = endX / 10.0f;
        t.tunnelX = x / 10.0f;
        t.tunnelY = y / 10.0f;
        t.tunnelZ = z / 100.0f;
        t.tunnelH = h / 100.0f;
        t.tunnelTs = ts;
        if (t.active) pushEvent(EventType::TunnelIn, t.doId);
        break;
    }
    case dc::DistributedToon::k_setTunnelOut: {
        const int16_t ts = r.i16();
        const int16_t startX = r.i16();
        const int16_t startY = r.i16();
        const int16_t x = r.i16();
        const int16_t y = r.i16();
        const int16_t z = r.i16();
        const int32_t h = r.i32();
        if (!r.ok()) return false;
        t.tunnelStartX = startX / 10.0f;
        t.tunnelStartY = startY / 10.0f;
        t.tunnelX = x / 10.0f;
        t.tunnelY = y / 10.0f;
        t.tunnelZ = z / 100.0f;
        t.tunnelH = h / 100.0f;
        t.tunnelTs = ts;
        if (t.active) pushEvent(EventType::TunnelOut, t.doId);
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
    // friends detail panel: laff is broadcast
    case dc::DistributedToon::k_setHp:
        t.hp = r.i16();
        break;
    case dc::DistributedToon::k_setMaxHp:
        t.maxHp = r.i16();
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
    resetClerks(false); // clerks: retain authoritative owner profile across zones

    resetBoss();
    bossPies_ = 0;
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
    shopMovie_ = ShopMovie{}; // the clerk is gone; a session with it cannot continue
    tailorMovie_ = TailorMovie{};
    movieHead_ = movieTail_ = 0; // quests section: pending NPC movies belong to the old zone
    for (Treasure& t : treasures_) {
        if (t.active) pushEvent(EventType::TreasureGone, t.doId);
        t.active = false;
    }
    for (CChar& c : cchars_) {
        if (c.active) pushEvent(EventType::CCharGone, c.doId);
        c.active = false;
    }
    if (boat_.active) pushEvent(EventType::BoatGone, boat_.doId);
    boat_.active = false;
    clearFishing();
    clearSuits();
    clearBattles();
    clearCogObjects();
    clearTrolleyObjects();
    clearRacing();
    clearGolfObjects();
}

bool Client::changeStreetCell(uint32_t cellZone, const uint16_t* visZones, int numVisZones) {
    if (state_ != State::InZone) return false;
    // Street.doEnterZone (Astron): the branch zone comes first. It holds the street's
    // DistributedBuildings and suit planner, which would leave on every cell change otherwise.
    const uint32_t branchZone = cellZone - cellZone % 100;
    uint32_t zones[kMaxVisZones + 2];
    int n = 0;
    zones[n++] = branchZone;
    if (cellZone != branchZone) zones[n++] = cellZone;
    for (int i = 0; i < numVisZones && n <= kMaxVisZones + 1; ++i) {
        if (visZones[i] != cellZone && visZones[i] != branchZone) zones[n++] = visZones[i];
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
    numDoors_ = 0;
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

void Client::setPendingPatternName(int16_t p1, uint8_t f1, int16_t p2, uint8_t f2,
                                   int16_t p3, uint8_t f3, int16_t p4, uint8_t f4) {
    pendingNameAction_ = 1;
    pendingPattern_[0] = p1; pendingPatternFlags_[0] = f1;
    pendingPattern_[1] = p2; pendingPatternFlags_[1] = f2;
    pendingPattern_[2] = p3; pendingPatternFlags_[2] = f3;
    pendingPattern_[3] = p4; pendingPatternFlags_[3] = f4;
    game::buildPatternName(p1, f1, p2, f2, p3, f3, p4, f4, pendingPatternName_, sizeof(pendingPatternName_));
}

void Client::setPendingTypedName(const char* name) {
    pendingNameAction_ = 2;
    if (name) {
        snprintf(pendingTypedName_, sizeof(pendingTypedName_), "%s", name);
    } else {
        pendingTypedName_[0] = '\0';
    }
}

void Client::clearPendingName() {
    pendingNameAction_ = 0;
    pendingPatternName_[0] = '\0';
    pendingTypedName_[0] = '\0';
}

bool Client::sendSetNamePattern(uint32_t avId, int16_t p1, uint8_t f1, int16_t p2, uint8_t f2,
                                int16_t p3, uint8_t f3, int16_t p4, uint8_t f4) {
    if (state_ != State::ChooseAvatar && state_ != State::CreateAvatar) return false;
    namePatternResult_ = -1;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, kLoginManagerId, AstronLoginManager, setNamePattern);
    w.u32(avId);
    w.i16(p1);
    w.u8(f1);
    w.i16(p2);
    w.u8(f2);
    w.i16(p3);
    w.u8(f3);
    w.i16(p4);
    w.u8(f4);
    finish(w);
    return true;
}

bool Client::sendSetNameTyped(uint32_t avId, const char* name) {
    if (state_ != State::ChooseAvatar && state_ != State::CreateAvatar) return false;
    nameTypedResult_ = -1;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, kLoginManagerId, AstronLoginManager, setNameTyped);
    w.u32(avId);
    w.string(name ? name : "");
    finish(w);
    return true;
}

bool Client::sendAcknowledgeAvatarName(uint32_t avId) {
    if (state_ != State::ChooseAvatar) return false;
    pendingAckAvId_ = avId;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, kLoginManagerId, AstronLoginManager, acknowledgeAvatarName);
    w.u32(avId);
    finish(w);
    return true;
}

bool Client::sendRequestRemoveAvatar(uint32_t avId) {
    if (state_ != State::ChooseAvatar) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, kLoginManagerId, AstronLoginManager, requestRemoveAvatar);
    w.u32(avId);
    finish(w);
    return true;
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
    ownX_ = x;
    ownY_ = y;
    ownZ_ = z;
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
                                          "jumpLand", "Happy", "Sad", "Sleep", "Emote", "Sit", "SitStart", "swim", "swimhold", "TeleportOut", "TeleportIn", "Catching", "CatchEating"};

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

bool Client::sendEmote(int id,float multiplier) {
    if (state_!=State::InZone || id < -1 || id>=25 || !std::isfinite(multiplier)) return false;
    if (id>=0 && (!ownStats_.hasEmotes || !ownStats_.emoteAccess[id])) return false;
    DatagramWriter w=beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_OWN_UPDATE(w,DistributedToon,setEmoteState);
    w.i16(static_cast<int16_t>(id));w.i16(packFixed(multiplier,1000));w.i16(networkTime(now_));
    return finish(w);
}

bool Client::sendSpeedChat(uint16_t msgIndex) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_OWN_UPDATE(w, DistributedToon, setSC);
    w.u16(msgIndex);
    return finish(w);
}

// ---- Transitions (source/world/transition.*, docs/design/transitions.md) ----
bool Client::sendTunnelOut(float startX, float startY, float x, float y, float z, float h) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_OWN_UPDATE(w, DistributedToon, setTunnelOut);
    w.i16(networkTime(now_));
    w.i16(packFixed(startX, 10.0f));
    w.i16(packFixed(startY, 10.0f));
    w.i16(packFixed(x, 10.0f));
    w.i16(packFixed(y, 10.0f));
    w.i16(packFixed(z, 100.0f));
    w.i32(packFixed(h, 100.0f));
    return finish(w);
}

bool Client::sendTunnelIn(float endX, float x, float y, float z, float h) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_OWN_UPDATE(w, DistributedToon, setTunnelIn);
    w.i16(networkTime(now_));
    w.i16(packFixed(endX, 10.0f));
    w.i16(packFixed(x, 10.0f));
    w.i16(packFixed(y, 10.0f));
    w.i16(packFixed(z, 100.0f));
    w.i32(packFixed(h, 100.0f));
    return finish(w);
}

bool Client::sendDoorRequestEnter(uint32_t doorDoId) {
    if (state_ != State::InZone || doorDoId == 0) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doorDoId, DistributedDoor, requestEnter);
    return finish(w);
}

bool Client::sendDoorRequestExit(uint32_t doorDoId) {
    if (state_ != State::InZone || doorDoId == 0) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doorDoId, DistributedDoor, requestExit);
    return finish(w);
}
// ---- End Transitions ----

const char* Client::avatarName(uint32_t doId) const {
    if (doId == avatarId_ && avatarId_ != 0) return ownName_;
    const RemoteToon* t = findToon(doId);
    if (t) return t->name;
    return nullptr;
}

bool Client::avatarPos(uint32_t doId, float* x, float* y, float* z) const {
    if (doId == avatarId_ && avatarId_ != 0) {
        if (x) *x = ownX_;
        if (y) *y = ownY_;
        if (z) *z = ownZ_;
        return true;
    }
    const RemoteToon* t = findToon(doId);
    if (t) {
        if (x) *x = t->x;
        if (y) *y = t->y;
        if (z) *z = t->z;
        return true;
    }
    return false;
}

// ====================================================================== fishing
// DistributedFishingPond / DistributedFishingSpot (docs/design/fishing.md). Additive section:
// everything fishing-specific in this file lives here, plus the small hooks in handleEnter,
// handleSetField, handleLeaving, resetWorld, dropZoneObjects and applyOwnField.

const FishingSpot* Client::findFishSpot(uint32_t doId) const {
    for (const FishingSpot& s : fishSpots_) {
        if (s.active && s.doId == doId) return &s;
    }
    return nullptr;
}

void Client::clearFishing() {
    for (FishingSpot& s : fishSpots_) {
        if (s.active) pushEvent(EventType::FishSpotGone, s.doId);
        s.active = false;
    }
    pond_.active = false;
}

bool Client::applyFishSpotField(FishingSpot& s, DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedFishingSpot::k_setPondDoId) {
        s.pondDoId = r.u32();
    } else if (fieldId == dc::DistributedFishingSpot::k_setPosHpr) {
        s.x = r.i16() / 10.0f;
        s.y = r.i16() / 10.0f;
        s.z = r.i16() / 10.0f;
        s.h = r.i16() / 10.0f;
        s.p = r.i16() / 10.0f;
        s.r = r.i16() / 10.0f;
    } else if (fieldId == dc::DistributedFishingSpot::k_setOccupied) {
        s.occupant = r.u32();
    } else {
        skipField(r, fieldId);
    }
    return r.ok();
}

// ============================================================== trolley section
// DistributedTrolley, DistributedMinigame (+ Catch) and PurchaseManager objects, and the
// handful of clsend fields we answer them with (docs/design/trolley.md sections 1-2).
// Everything for this feature lives between this banner and the end of the file.

void Client::pushEventArg(EventType type, uint32_t doId, int32_t arg) {
    if (evHead_ - evTail_ >= uint32_t(kEventQueue)) {
        ++droppedEvents_;
        return;
    }
    Event e{type, doId};
    e.arg = arg;
    events_[evHead_ % kEventQueue] = e;
    ++evHead_;
}

void Client::clearTrolleyObjects() {
    if (trolley_.active) pushEvent(EventType::TrolleyGone, trolley_.doId);
    if (minigame_.active) pushEvent(EventType::MinigameGone, minigame_.doId);
    if (purchase_.active) pushEvent(EventType::PurchaseGone, purchase_.doId);
    trolley_ = Trolley{};
    minigame_ = Minigame{};
    purchase_ = Purchase{};
}

bool Client::applyTrolleyField(Trolley& t, DatagramReader& r, uint16_t fieldId, bool announce) {
    namespace T = dc::DistributedTrolley;
    if (fieldId == T::k_setState) {
        char name[24];
        r.string(name, sizeof(name));
        const int16_t ts = r.i16();
        if (!r.ok()) return false;
        // DistributedTrolley.setState: fsm.request(state, [localElapsedTime(ts)]).
        t.state = !strcmp(name, "entering")        ? TrolleyState::Entering
                  : !strcmp(name, "waitEmpty")     ? TrolleyState::WaitEmpty
                  : !strcmp(name, "waitCountdown") ? TrolleyState::WaitCountdown
                  : !strcmp(name, "leaving")       ? TrolleyState::Leaving
                                                   : TrolleyState::Off;
        t.stateStartMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
        if (announce) pushEvent(EventType::TrolleyState, t.doId);
    } else if (fieldId >= T::k_fillSlot0 && fieldId <= T::k_fillSlot3) {
        const uint32_t av = r.u32();
        if (!r.ok()) return false;
        const int i = int(fieldId - T::k_fillSlot0);
        t.slot[i] = av;
        if (announce && av != 0) pushEventArg(EventType::TrolleyFill, av, i);
    } else if (fieldId >= T::k_emptySlot0 && fieldId <= T::k_emptySlot3) {
        const uint32_t av = r.u32();
        r.i16(); // timestamp
        if (!r.ok()) return false;
        const int i = int(fieldId - T::k_emptySlot0);
        if (av == 0) t.slot[i] = 0; // the seat is free again
        else if (announce) pushEventArg(EventType::TrolleyEmpty, av, i);
    } else if (fieldId == T::k_rejectBoard) {
        const uint32_t av = r.u32();
        if (!r.ok()) return false;
        if (announce && av == avatarId_) pushEvent(EventType::TrolleyReject, t.doId);
    } else if (fieldId == T::k_setMinigameZone) {
        const uint32_t zone = r.u32();
        const uint16_t game = r.u16();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::TrolleyMinigameZone, zone, game);
    } else {
        skipField(r, fieldId);
    }
    return r.ok();
}

bool Client::handleFishingEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                                uint16_t reqCount, bool other) {
    if (classId == dc::DistributedFishingPond::kClassId) {
        FishingPond tmp{};
        tmp.doId = doId;
        for (uint16_t i = 0; i < reqCount && r.ok(); ++i) {
            if (req[i] == dc::DistributedFishingPond::k_setArea) tmp.area = r.u32();
            else skipField(r, req[i]);
        }
        if (other) {
            uint16_t n = r.u16();
            for (uint16_t i = 0; i < n && r.ok(); ++i) {
                const uint16_t fid = r.u16();
                if (fid == dc::DistributedFishingPond::k_setArea) tmp.area = r.u32();
                else skipField(r, fid);
            }
        }
        if (!r.ok()) return false;
        tmp.active = true;
        pond_ = tmp;
        TT_LOG("net: fishing pond %u area %u", unsigned(doId), unsigned(tmp.area));
        pushEvent(EventType::FishPondUp, doId);
        return true;
    }
    FishingSpot* spot = nullptr;
    for (FishingSpot& s : fishSpots_) {
        if (s.active && s.doId == doId) spot = &s;
    }
    for (FishingSpot& s : fishSpots_) {
        if (!spot && !s.active) spot = &s;
    }
    if (!spot) {
        TT_LOG("net: no room for fishing spot %u", unsigned(doId));
        return false;
    }
    FishingSpot tmp{};
    tmp.doId = doId;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyFishSpotField(tmp, r, req[i]);
    if (ok && other) {
        uint16_t n = r.u16();
        for (uint16_t i = 0; i < n && ok && r.ok(); ++i) ok = applyFishSpotField(tmp, r, r.u16());
    }
    if (!ok || !r.ok()) {
        TT_LOG("net: fishing spot %u generate did not parse", unsigned(doId));
        return false;
    }
    tmp.active = true;
    *spot = tmp;
    TT_LOG("net: fishing spot %u at %.1f,%.1f,%.1f h %.0f", unsigned(doId), double(tmp.x), double(tmp.y), double(tmp.z),
           double(tmp.h));
    pushEvent(EventType::FishSpotSpawn, doId);
    return true;
}

// Returns true when doId is one of our spots (the field was consumed).
bool Client::handleFishingField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    FishingSpot* spot = nullptr;
    for (FishingSpot& s : fishSpots_) {
        if (s.active && s.doId == doId) spot = &s;
    }
    if (!spot) return false;
    switch (fieldId) {
    case dc::DistributedFishingSpot::k_setOccupied:
        if (applyFishSpotField(*spot, r, fieldId)) pushEvent(EventType::FishOccupied, doId);
        break;
    case dc::DistributedFishingSpot::k_setPosHpr:
    case dc::DistributedFishingSpot::k_setPondDoId:
        applyFishSpotField(*spot, r, fieldId);
        break;
    case dc::DistributedFishingSpot::k_setMovie: {
        // setMovie(mode, code, itemDesc1, itemDesc2, itemDesc3, power/100, h/100)
        FishMovieMsg m{};
        m.spotDoId = doId;
        m.mode = r.u8();
        m.code = r.u8();
        m.item1 = r.u16();
        m.item2 = r.u16();
        m.item3 = r.u16();
        m.power = r.u8() / 100.0f;
        m.heading = r.i16() / 100.0f;
        if (!r.ok()) break;
        m.seq = fishMovie_.seq + 1;
        fishMovie_ = m;
        pushEvent(EventType::FishMovie, doId);
        break;
    }
    case dc::DistributedFishingSpot::k_rejectEnter:
        pushEvent(EventType::FishReject, doId);
        break;
    case dc::DistributedFishingSpot::k_sellFishComplete:
        fishSellTrophy_ = r.u8() != 0;
        fishSellCount_ = r.u16();
        if (r.ok()) pushEvent(EventType::FishSold, doId);
        break;
    default:
        skipField(r, fieldId);
        break;
    }
    return true;
}

void Client::handleFishingLeaving(uint32_t doId) {
    for (FishingSpot& s : fishSpots_) {
        if (s.active && s.doId == doId) {
            s.active = false;
            pushEvent(EventType::FishSpotGone, doId);
        }
    }
    if (pond_.active && pond_.doId == doId) pond_.active = false;
}

// setFishingRod(u8), setMaxFishTank(u8), setFishTank(u8[] genus, u8[] species, u16[] weight).
bool Client::applyOwnFishField(DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedToon::k_setFishingRod) {
        ownFish_.rod = r.u8();
    } else if (fieldId == dc::DistributedToon::k_setMaxFishTank) {
        ownFish_.maxTank = r.u8();
    } else {
        const Bytes g = r.blob();
        const Bytes sp = r.blob();
        const Bytes w = r.blob();
        if (!r.ok()) return false;
        uint32_t n = g.len < sp.len ? g.len : sp.len;
        if (n > w.len / 2) n = w.len / 2;
        if (n > uint32_t(kMaxFishTank)) n = kMaxFishTank;
        ownFish_.tankCount = uint8_t(n);
        for (uint32_t i = 0; i < n; ++i) {
            ownFish_.genus[i] = g.data[i];
            ownFish_.species[i] = sp.data[i];
            ownFish_.weight[i] = uint16_t(w.data[2 * i] | (w.data[2 * i + 1] << 8));
        }
    }
    if (r.ok()) pushEvent(EventType::FishStats, avatarId_);
    return r.ok();
}

// DistributedFishingSpot.d_requestEnter / d_requestExit / d_doCast / sellFish: clsend, to the spot.
bool Client::sendFishRequestEnter(uint32_t spotDoId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, spotDoId, DistributedFishingSpot, requestEnter);
    return finish(w);
}

bool Client::sendFishRequestExit(uint32_t spotDoId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, spotDoId, DistributedFishingSpot, requestExit);
    return finish(w);
}

// power01 in [0, 1] (sent as uint8/255), headingDeg = the aim angle local to the spot
// (angleNP.getH(), within +-FishGlobals.FishingAngleMax; sent as int16/100).
bool Client::sendFishCast(uint32_t spotDoId, float power01, float headingDeg) {
    if (state_ != State::InZone) return false;
    if (power01 < 0.0f) power01 = 0.0f;
    if (power01 > 1.0f) power01 = 1.0f;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, spotDoId, DistributedFishingSpot, doCast);
    w.u8(uint8_t(std::floor(power01 * 255.0f + 0.5f)));
    w.i16(packFixed(headingDeg, 100.0f));
    return finish(w);
}

bool Client::sendFishSell(uint32_t spotDoId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, spotDoId, DistributedFishingSpot, sellFish);
    return finish(w);
}

bool Client::sendFishHitTarget(uint32_t pondDoId, uint32_t targetDoId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, pondDoId, DistributedFishingPond, hitTarget);
    w.u32(targetDoId);
    return finish(w);
}
// ================================================================== end fishing
bool Client::handleTrolleyEnter(uint32_t doId, DatagramReader& r, const uint16_t* req, uint16_t reqCount,
                                bool other) {
    Trolley tmp{};
    tmp.doId = doId;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyTrolleyField(tmp, r, req[i], false);
    if (ok && other) {
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && ok && r.ok(); ++i) ok = applyTrolleyField(tmp, r, r.u16(), false);
    }
    if (!ok || !r.ok()) {
        TT_LOG("net: trolley %u generate did not parse", unsigned(doId));
        return false;
    }
    tmp.active = true;
    trolley_ = tmp;
    TT_LOG("net: trolley %u state %d seats %u/%u/%u/%u", unsigned(doId), int(tmp.state), unsigned(tmp.slot[0]),
           unsigned(tmp.slot[1]), unsigned(tmp.slot[2]), unsigned(tmp.slot[3]));
    pushEvent(EventType::TrolleyUp, doId);
    return true;
}

bool Client::applyMinigameField(Minigame& m, DatagramReader& r, uint16_t fieldId, bool announce) {
    namespace M = dc::DistributedMinigame;
    if (fieldId == M::k_setParticipants) {
        const uint16_t bytes = r.u16();
        m.numParticipants = 0;
        for (uint16_t i = 0; i < bytes / 4 && r.ok(); ++i) {
            const uint32_t av = r.u32();
            if (m.numParticipants < kMinigamePlayers) m.participants[m.numParticipants++] = av;
        }
    } else if (fieldId == M::k_setTrolleyZone) {
        m.trolleyZone = r.u32();
    } else if (fieldId == M::k_setDifficultyOverrides) {
        m.difficultyOverride = r.i32();
        m.zoneOverride = r.i32();
    } else if (fieldId == M::k_setGameReady) {
        m.gameReady = true;
        if (announce) pushEvent(EventType::MinigameReady, m.doId);
    } else if (fieldId == M::k_setGameStart) {
        const int16_t ts = r.i16();
        if (!r.ok()) return false;
        m.started = true;
        m.gameStartMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
        if (announce) pushEvent(EventType::MinigameStart, m.doId);
    } else if (fieldId == M::k_setGameExit) {
        m.exitMsg = true;
        if (announce) pushEvent(EventType::MinigameExit, m.doId);
    } else if (fieldId == M::k_setGameAbort) {
        m.aborted = true;
        if (announce) pushEvent(EventType::MinigameAbort, m.doId);
    } else if (fieldId == dc::DistributedCatchGame::k_setObjectCaught) {
        const uint32_t av = r.u32();
        const uint32_t obj = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::CatchCaught, av, int32_t(obj));
    } else if (fieldId == dc::DistributedCatchGame::k_setEveryoneDone) {
        if (announce) pushEvent(EventType::CatchEveryoneDone, m.doId);
    } else if (fieldId == dc::DistributedCatchGame::k_hitBySuit) {
        const uint32_t av = r.u32();
        const int16_t stamp = r.i16();
        if (!r.ok()) return false;
        if (announce && av != avatarId_)
            pushEventArg(EventType::CatchHitBySuit, av, static_cast<int32_t>(timeSynced_ ? networkToLocal(stamp, now_) : now_));
    // ---- DistributedCannonGame ----
    } else if (fieldId == dc::DistributedCannonGame::k_updateCannonPosition) {
        const uint32_t av = r.u32();
        const int32_t zRot = r.i32();
        const uint32_t ang = r.u32();
        if (!r.ok()) return false;
        for (int i = 0; i < m.numParticipants; ++i) {
            if (m.participants[i] == av) {
                m.cannonZRot[i] = zRot / 100.0f;
                m.cannonAngle[i] = ang / 100.0f;
                break;
            }
        }
        if (announce) pushEventArg(EventType::CannonUpdatePosition, av, 0);
    } else if (fieldId == dc::DistributedCannonGame::k_setCannonWillFire) {
        const uint32_t av = r.u32();
        const int32_t fireTime = r.i32();
        const int32_t zRot = r.i32();
        const uint32_t ang = r.u32();
        if (!r.ok()) return false;
        for (int i = 0; i < m.numParticipants; ++i) {
            if (m.participants[i] == av) {
                m.cannonFireTime[i] = fireTime / 100.0f;
                m.cannonZRot[i] = zRot / 100.0f;
                m.cannonAngle[i] = ang / 100.0f;
                break;
            }
        }
        if (announce) pushEventArg(EventType::CannonWillFire, av, fireTime);
    } else if (fieldId == dc::DistributedCannonGame::k_announceToonWillLandInWater) {
        const uint32_t av = r.u32();
        const int32_t landTime = r.i32();
        if (!r.ok()) return false;
        m.cannonLandedAv = av;
        m.cannonLandTime = landTime / 100.0f;
        if (announce) pushEventArg(EventType::CannonLandedInWater, av, landTime);

    // ---- DistributedTugOfWarGame ----
    } else if (fieldId == dc::DistributedTugOfWarGame::k_sendGameType) {
        m.tugGameType = r.u8();
        m.tugSuitType = r.u8();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::TugGameType, m.doId, (m.tugGameType << 8) | m.tugSuitType);
    } else if (fieldId == dc::DistributedTugOfWarGame::k_sendGoSignal) {
        const uint16_t bytes = r.u16();
        r.skip(bytes);
        if (!r.ok()) return false;
        m.tugGo = true;
        if (announce) pushEvent(EventType::TugGoSignal, m.doId);
    } else if (fieldId == dc::DistributedTugOfWarGame::k_sendStopSignal) {
        const uint16_t wlen = r.u16(); r.skip(wlen);
        const uint16_t llen = r.u16(); r.skip(llen);
        const uint16_t tlen = r.u16(); r.skip(tlen);
        if (!r.ok()) return false;
        m.tugStopped = true;
        if (announce) pushEvent(EventType::TugStopSignal, m.doId);
    } else if (fieldId == dc::DistributedTugOfWarGame::k_sendCurrentPosition) {
        const uint16_t avBytes = r.u16();
        uint32_t avs[kMinigamePlayers] = {};
        const int avCount = int(avBytes / 4);
        for (int i = 0; i < avCount && r.ok(); ++i) {
            uint32_t av = r.u32();
            if (i < kMinigamePlayers) avs[i] = av;
        }
        const uint16_t offBytes = r.u16();
        const int offCount = int(offBytes / 2);
        for (int i = 0; i < offCount && r.ok(); ++i) {
            int16_t off = r.i16();
            if (i < avCount) {
                for (int j = 0; j < m.numParticipants; ++j) {
                    if (m.participants[j] == avs[i]) {
                        m.tugOffsets[j] = off / 1000.0f;
                        break;
                    }
                }
            }
        }
        if (!r.ok()) return false;
        if (announce) pushEvent(EventType::TugPosition, m.doId);
    } else if (fieldId == dc::DistributedTugOfWarGame::k_sendSuitPosition) {
        m.tugSuitOffset = r.i32() / 1000.0f;
        if (!r.ok()) return false;
        if (announce) pushEvent(EventType::TugPosition, m.doId);
    } else if (fieldId == dc::DistributedTugOfWarGame::k_remoteKeyRateUpdate) {
        const uint32_t av = r.u32();
        const uint32_t rate = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::TugRemoteKeyRate, av, int32_t(rate));

    // ---- DistributedRingGame ----
    } else if (fieldId == dc::DistributedRingGame::k_setTimeBase) {
        m.ringTimeBase = r.i16();
    } else if (fieldId == dc::DistributedRingGame::k_setColorIndices) {
        m.ringColorIndices[0] = r.i8();
        m.ringColorIndices[1] = r.i8();
        m.ringColorIndices[2] = r.i8();
        m.ringColorIndices[3] = r.i8();
    } else if (fieldId == dc::DistributedRingGame::k_setRingGroupResults) {
        m.ringGroupResult = r.u8();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::RingGroupResults, m.doId, m.ringGroupResult);

    // ---- DistributedMazeGame ----
    } else if (fieldId == dc::DistributedMazeGame::k_setTreasureGrabbed) {
        m.mazeGrabbedAv = r.u32();
        m.mazeGrabbedTreasure = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::MazeTreasureGrabbed, m.mazeGrabbedAv, int32_t(m.mazeGrabbedTreasure));
    } else if (fieldId == dc::DistributedMazeGame::k_allTreasuresTaken) {
        m.mazeAllTaken = true;
        if (announce) pushEvent(EventType::MazeAllTreasuresTaken, m.doId);
    } else if (fieldId == dc::DistributedMazeGame::k_hitBySuit) {
        const uint32_t av = r.u32();
        const int16_t ts = r.i16();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::MazeHitBySuit, av, int32_t(ts));

    // ---- DistributedPairingGame ----
    } else if (fieldId == dc::DistributedPairingGame::k_setDeckSeed) {
        m.pairingDeckSeed = r.u32();
    } else if (fieldId == dc::DistributedPairingGame::k_setMaxOpenCards) {
        m.pairingMaxOpenCards = r.u8();
    } else if (fieldId == dc::DistributedPairingGame::k_openCardResult) {
        m.pairingCardToTurnUp = r.i16();
        m.pairingOpenAvId = r.u32();
        m.pairingMatchingCard = r.i16();
        m.pairingPoints = r.i8();
        const uint16_t downBytes = r.u16();
        m.pairingTurnDownCount = 0;
        for (uint16_t i = 0; i < downBytes / 2 && r.ok(); ++i) {
            int16_t card = r.i16();
            if (m.pairingTurnDownCount < 4) m.pairingCardsToTurnDown[m.pairingTurnDownCount++] = card;
        }
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::PairingOpenCardResult, m.pairingOpenAvId, int32_t(m.pairingCardToTurnUp));
    } else if (fieldId == dc::DistributedPairingGame::k_setEveryoneDone) {
        m.pairingEveryoneDone = true;
        if (announce) pushEvent(EventType::PairingEveryoneDone, m.doId);

    // ---- DistributedDivingGame ----
    } else if (fieldId == dc::DistributedDivingGame::k_setTreasureGrabbed) {
        m.divingGrabbedAv = r.u32();
        m.divingGrabbedChest = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::DivingTreasureGrabbed, m.divingGrabbedAv, int32_t(m.divingGrabbedChest));
    } else if (fieldId == dc::DistributedDivingGame::k_incrementScore) {
        m.divingScoreAv = r.u32();
        m.divingScoreNewSpot = r.u32();
        r.i16(); // ts
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::DivingIncrementScore, m.divingScoreAv, int32_t(m.divingScoreNewSpot));
    } else if (fieldId == dc::DistributedDivingGame::k_fishSpawn) {
        m.divingFishSpawnTs = r.i16();
        m.divingFishSpawnCode = r.u32();
        m.divingFishSpawnerId = r.u32();
        m.divingFishSpawnOffset = r.u16();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::DivingFishSpawn, m.doId, int32_t((m.divingFishSpawnerId << 16) | (m.divingFishSpawnCode & 0xffff)));
    } else if (fieldId == dc::DistributedDivingGame::k_setCrabMoving) {
        m.divingCrabId = r.u32();
        r.i16(); // ts
        m.divingCrabRand1 = r.i8();
        m.divingCrabRand2 = r.i8();
        m.divingCrabX = r.i16();
        m.divingCrabDir = r.i8();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::DivingCrabMoving, m.doId, int32_t(m.divingCrabId));
    } else if (fieldId == dc::DistributedDivingGame::k_setTreasureDropped) {
        const uint32_t av = r.u32();
        const int16_t ts = r.i16();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::DivingTreasureDropped, av, int32_t(ts));
    } else if (fieldId == dc::DistributedDivingGame::k_performFishCollision) {
        const uint32_t av = r.u32();
        r.u32(); // spawnId
        r.u32(); // spawnerId
        const int16_t ts = r.i16();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::DivingPerformFishCollision, av, int32_t(ts));
    } else if (fieldId == dc::DistributedDivingGame::k_performCrabCollision) {
        const uint32_t av = r.u32();
        const int16_t ts = r.i16();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::DivingPerformCrabCollision, av, int32_t(ts));

    // ---- DistributedVineGame ----
    } else if (fieldId == dc::DistributedVineGame::k_setVineSections) {
        const uint16_t bytes = r.u16();
        m.vineSectionCount = 0;
        for (uint16_t i = 0; i < bytes && r.ok(); ++i) {
            uint8_t sec = r.u8();
            if (m.vineSectionCount < 8) m.vineSections[m.vineSectionCount++] = sec;
        }
        if (!r.ok()) return false;
        if (announce) pushEvent(EventType::VineSetVineSections, m.doId);
    } else if (fieldId == dc::DistributedVineGame::k_setScore) {
        m.vineScoreAv = r.u32();
        m.vineScoreVal = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::VineSetScore, m.vineScoreAv, int32_t(m.vineScoreVal));
    } else if (fieldId == dc::DistributedVineGame::k_allAtEndVine) {
        m.vineAllAtEnd = true;
        if (announce) pushEvent(EventType::VineAllAtEnd, m.doId);
    } else if (fieldId == dc::DistributedVineGame::k_setTreasureGrabbed) {
        m.vineGrabbedAv = r.u32();
        m.vineGrabbedTreasure = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::VineTreasureGrabbed, m.vineGrabbedAv, int32_t(m.vineGrabbedTreasure));

    // ---- DistributedTargetGame ----
    } else if (fieldId == dc::DistributedTargetGame::k_setTargetSeed) {
        m.targetSeed = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEvent(EventType::TargetSetSeed, m.doId);
    } else if (fieldId == dc::DistributedTargetGame::k_setRoundDone) {
        m.targetRoundDone = true;
        if (announce) pushEvent(EventType::TargetRoundDone, m.doId);
    } else if (fieldId == dc::DistributedTargetGame::k_setSingleScore) {
        m.targetSingleScore = r.u16();
        m.targetSingleAvId = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::TargetSingleScore, m.targetSingleAvId, int32_t(m.targetSingleScore));
    } else if (fieldId == dc::DistributedTargetGame::k_setGameDone) {
        m.targetGameDone = true;
        if (announce) pushEvent(EventType::TargetGameDone, m.doId);

    // ---- DistributedCogThiefGame ----
    } else if (fieldId == dc::DistributedCogThiefGame::k_updateSuitGoal) {
        r.i32(); r.i32();
        m.cogThiefGoalCog = r.i8();
        m.cogThiefGoalType = r.i8();
        m.cogThiefGoalId = r.i64();
        m.cogThiefGoalPos[0] = r.i32() / 100.0f;
        m.cogThiefGoalPos[1] = r.i32() / 100.0f;
        m.cogThiefGoalPos[2] = r.i32() / 100.0f;
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::CogThiefUpdateGoal, m.doId, int32_t(m.cogThiefGoalCog));
    } else if (fieldId == dc::DistributedCogThiefGame::k_makeCogCarryBarrel) {
        r.i32(); r.i32();
        m.cogThiefCarryCog = r.i8();
        m.cogThiefCarryBarrel = r.i8();
        r.i32(); r.i32(); r.i32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::CogThiefCarryBarrel, m.doId, int32_t(m.cogThiefCarryBarrel));
    } else if (fieldId == dc::DistributedCogThiefGame::k_makeCogDropBarrel) {
        r.i32(); r.i32();
        m.cogThiefDropCog = r.i8();
        m.cogThiefDropBarrel = r.i8();
        r.i32(); r.i32(); r.i32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::CogThiefDropBarrel, m.doId, int32_t(m.cogThiefDropBarrel));
    } else if (fieldId == dc::DistributedCogThiefGame::k_markBarrelStolen) {
        r.i32(); r.i32();
        m.cogThiefStolenBarrel = r.i8();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::CogThiefBarrelStolen, m.doId, int32_t(m.cogThiefStolenBarrel));

    // ---- DistributedTwoDGame ----
    } else if (fieldId == dc::DistributedTwoDGame::k_setSectionsSelected) {
        const uint16_t bytes = r.u16();
        // Skip or read complex struct array
        for (uint16_t i = 0; i < bytes && r.ok(); ++i) r.u8();
        if (!r.ok()) return false;
        if (announce) pushEvent(EventType::TwoDSetSections, m.doId);
    } else if (fieldId == dc::DistributedTwoDGame::k_setTreasureGrabbed) {
        m.twoDGrabbedAv = r.u32();
        m.twoDGrabbedSection = r.u8();
        m.twoDGrabbedTreasure = r.u8();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::TwoDTreasureGrabbed, m.twoDGrabbedAv, int32_t(m.twoDGrabbedTreasure));
    } else if (fieldId == dc::DistributedTwoDGame::k_setEveryoneDone) {
        m.twoDEveryoneDone = true;
        if (announce) pushEvent(EventType::TwoDEveryoneDone, m.doId);

    // ---- DistributedTagGame ----
    } else if (fieldId == dc::DistributedTagGame::k_setIt) {
        m.tagItAvId = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::TagSetIt, m.tagItAvId, 0);
    } else if (fieldId == dc::DistributedTagGame::k_setTreasureScore) {
        const uint16_t bytes = r.u16();
        for (uint16_t i = 0; i < bytes / 2 && i < kMinigamePlayers && r.ok(); ++i) {
            m.tagTreasureScores[i] = r.u16();
        }
        if (!r.ok()) return false;
        if (announce) pushEvent(EventType::TagTreasureScore, m.doId);

    // ---- DistributedPatternGame ----
    } else if (fieldId == dc::DistributedPatternGame::k_setPattern) {
        const uint16_t bytes = r.u16();
        m.patternSeqLen = 0;
        for (uint16_t i = 0; i < bytes && r.ok(); ++i) {
            uint8_t btn = r.u8();
            if (m.patternSeqLen < 16) m.patternSeq[m.patternSeqLen++] = btn;
        }
        if (!r.ok()) return false;
        if (announce) pushEvent(EventType::PatternSetPattern, m.doId);
    } else if (fieldId == dc::DistributedPatternGame::k_setPlayerPatterns) {
        for (int p = 0; p < 4; ++p) {
            uint16_t pBytes = r.u16();
            for (uint16_t i = 0; i < pBytes && r.ok(); ++i) r.u8();
        }
        m.patternFastestAvId = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::PatternSetPlayerPatterns, m.patternFastestAvId, 0);
    } else if (fieldId == dc::DistributedPatternGame::k_remoteButtonPressed) {
        m.patternRemoteButtonAv = r.u32();
        m.patternRemoteButtonIdx = r.u8();
        m.patternRemoteButtonWrong = r.u8();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::PatternRemoteButton, m.patternRemoteButtonAv, int32_t(m.patternRemoteButtonIdx));

    // ---- DistributedIceGame ----
    } else if (fieldId == dc::DistributedIceGame::k_setMatchAndRound) {
        m.iceMatch = r.i8();
        m.iceRound = r.i8();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::IceMatchAndRound, m.doId, int32_t((m.iceMatch << 8) | (m.iceRound & 0xFF)));
    } else if (fieldId == dc::DistributedIceGame::k_setNewState) {
        r.string(m.iceState, sizeof(m.iceState));
        if (!r.ok()) return false;
        if (announce) pushEvent(EventType::IceNewState, m.doId);
    } else if (fieldId == dc::DistributedIceGame::k_setTimerStartTime) {
        m.iceTimerStart = r.i16();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::IceTimerStart, m.doId, int32_t(m.iceTimerStart));
    } else if (fieldId == dc::DistributedIceGame::k_setAvatarChoice) {
        m.iceChoicesForce[0] = r.i32() / 100.0f;
        m.iceChoicesDir[0] = r.i32() / 100.0f;
        if (!r.ok()) return false;
        if (announce) pushEvent(EventType::IceAvatarChoice, m.doId);
    } else if (fieldId == dc::DistributedIceGame::k_setTreasureGrabbed) {
        m.iceGrabbedAv = r.u32();
        m.iceGrabbedItem = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::IceTreasureGrabbed, m.iceGrabbedAv, int32_t(m.iceGrabbedItem));
    } else if (fieldId == dc::DistributedIceGame::k_setPenaltyGrabbed) {
        m.iceGrabbedAv = r.u32();
        m.iceGrabbedItem = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::IcePenaltyGrabbed, m.iceGrabbedAv, int32_t(m.iceGrabbedItem));

    // ---- DistributedRaceGame ----
    } else if (fieldId == dc::DistributedRaceGame::k_setTimerStartTime) {
        m.raceTimerStart = r.i16();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::RaceTimerStart, m.doId, int32_t(m.raceTimerStart));
    } else if (fieldId == dc::DistributedRaceGame::k_setAvatarChose) {
        m.raceAvatarChose = r.u32();
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::RaceAvatarChose, m.raceAvatarChose, 0);
    } else if (fieldId == dc::DistributedRaceGame::k_setChancePositions) {
        const uint16_t bytes = r.u16();
        for (uint16_t i = 0; i < bytes && i < kMinigamePlayers && r.ok(); ++i) {
            m.raceChancePositions[i] = r.u8();
        }
        if (!r.ok()) return false;
        if (announce) pushEvent(EventType::RaceChancePositions, m.doId);
    } else if (fieldId == dc::DistributedRaceGame::k_setServerChoices) {
        const uint16_t c1 = r.u16();
        m.raceServerChoiceCount = c1;
        for (uint16_t i = 0; i < c1 && i < 16 && r.ok(); ++i) m.raceServerChoices[i] = r.i8();
        const uint16_t c2 = r.u16();
        for (uint16_t i = 0; i < c2 && i < 16 && r.ok(); ++i) m.raceServerPositions[i] = r.u8();
        const uint16_t c3 = r.u16();
        for (uint16_t i = 0; i < c3 && i < 16 && r.ok(); ++i) m.raceServerRewards[i] = r.i8();
        if (!r.ok()) return false;
        if (announce) pushEvent(EventType::RaceServerChoices, m.doId);

    // ---- DistributedPhotoGame ----
    } else if (fieldId == dc::DistributedPhotoGame::k_newAIPhotoScore) {
        m.photoScoreAv = r.u32();
        m.photoScoreAssignment = r.u8();
        m.photoScoreStars = r.u32() / 100.0f;
        if (!r.ok()) return false;
        if (announce) pushEventArg(EventType::PhotoNewAIScore, m.photoScoreAv, int32_t(m.photoScoreAssignment));
    } else {
        skipField(r, fieldId); // setStartingVotes, setMetagameRound, game-specific fields
    }
    return r.ok();
}

bool Client::handleMinigameEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                                 uint16_t reqCount, bool other) {
    Minigame tmp{};
    tmp.doId = doId;
    tmp.classId = classId;
    tmp.difficultyOverride = kNoDifficultyOverride;
    tmp.zoneOverride = -1;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyMinigameField(tmp, r, req[i], false);
    if (ok && other) {
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && ok && r.ok(); ++i) ok = applyMinigameField(tmp, r, r.u16(), false);
    }
    if (!ok || !r.ok()) {
        TT_LOG("net: minigame %u (class %u) generate did not parse", unsigned(doId), unsigned(classId));
        return false;
    }
    tmp.active = true;
    minigame_ = tmp;
    TT_LOG("net: minigame %u class %u players %d trolley zone %u", unsigned(doId), unsigned(classId),
           tmp.numParticipants, unsigned(tmp.trolleyZone));
    pushEvent(EventType::MinigameUp, doId);
    return true;
}

bool Client::applyPurchaseField(Purchase& p, DatagramReader& r, uint16_t fieldId, bool announce) {
    namespace P = dc::PurchaseManager;
    if (fieldId == P::k_setPlayerIds) {
        for (int i = 0; i < kMinigamePlayers; ++i) p.playerIds[i] = r.u32();
    } else if (fieldId == P::k_setMinigamePoints) {
        for (int i = 0; i < kMinigamePlayers; ++i) p.minigamePoints[i] = r.u8();
    } else if (fieldId == P::k_setPlayerMoney) {
        for (int i = 0; i < kMinigamePlayers; ++i) p.playerMoney[i] = r.u8();
    } else if (fieldId == P::k_setPlayerStates) {
        for (int i = 0; i < kMinigamePlayers; ++i) p.playerStates[i] = r.u8();
        if (announce && r.ok()) pushEvent(EventType::PurchaseStates, p.doId);
    } else if (fieldId == P::k_setPurchaseExit) {
        p.exitMsg = true;
        if (announce) pushEvent(EventType::PurchaseExit, p.doId);
    } else {
        skipField(r, fieldId); // setNewbieIds, setCountdown, setMetagameRound, setVotesArray, ...
    }
    return r.ok();
}

bool Client::handlePurchaseEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                                 uint16_t reqCount, bool other) {
    Purchase tmp{};
    tmp.doId = doId;
    tmp.classId = classId;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyPurchaseField(tmp, r, req[i], false);
    if (ok && other) {
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && ok && r.ok(); ++i) ok = applyPurchaseField(tmp, r, r.u16(), false);
    }
    if (!ok || !r.ok()) {
        TT_LOG("net: purchase manager %u generate did not parse", unsigned(doId));
        return false;
    }
    tmp.active = true;
    purchase_ = tmp;
    TT_LOG("net: purchase manager %u (class %u) points %u/%u/%u/%u", unsigned(doId), unsigned(classId),
           unsigned(tmp.minigamePoints[0]), unsigned(tmp.minigamePoints[1]), unsigned(tmp.minigamePoints[2]),
           unsigned(tmp.minigamePoints[3]));
    pushEvent(EventType::PurchaseUp, doId);
    return true;
}

bool Client::handleTrolleyField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    if (trolley_.active && trolley_.doId == doId) {
        applyTrolleyField(trolley_, r, fieldId, true);
        return true;
    }
    if (minigame_.active && minigame_.doId == doId) {
        applyMinigameField(minigame_, r, fieldId, true);
        return true;
    }
    if (purchase_.active && purchase_.doId == doId) {
        applyPurchaseField(purchase_, r, fieldId, true);
        return true;
    }
    return false;
}

bool Client::handleTrolleyLeaving(uint32_t doId) {
    if (trolley_.active && trolley_.doId == doId) {
        trolley_ = Trolley{};
        pushEvent(EventType::TrolleyGone, doId);
        return true;
    }
    if (minigame_.active && minigame_.doId == doId) {
        minigame_.active = false;
        pushEvent(EventType::MinigameGone, doId);
        return true;
    }
    if (purchase_.active && purchase_.doId == doId) {
        purchase_.active = false;
        pushEvent(EventType::PurchaseGone, doId);
        return true;
    }
    return false;
}

bool Client::sendTrolleyBoard() {
    if (!trolley_.active || state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, trolley_.doId, DistributedTrolley, requestBoard);
    return finish(w);
}

bool Client::sendTrolleyExit() {
    if (!trolley_.active || state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, trolley_.doId, DistributedTrolley, requestExit);
    return finish(w);
}

// The minigame DO arrives while the quiet-zone change is still finishing (the AI generated
// it before telling us the zone), and upstream answers in announceGenerate, so these only
// need the object, not State::InZone.
bool Client::sendMinigameJoined() {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedMinigame, setAvatarJoined);
    return finish(w);
}

bool Client::sendMinigameReady() {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedMinigame, setAvatarReady);
    return finish(w);
}

bool Client::sendMinigameExited() {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedMinigame, setAvatarExited);
    return finish(w);
}

bool Client::sendMinigameExit() {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedMinigame, requestExit);
    return finish(w);
}

bool Client::sendCatchClaim(uint32_t objNum, uint32_t typeId) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedCatchGame, claimCatch);
    w.u32(objNum);
    w.u32(typeId);
    return finish(w);
}

bool Client::sendCatchDone() {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedCatchGame, reportDone);
    return finish(w);
}

bool Client::sendCatchHitBySuit(uint32_t avId) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedCatchGame, hitBySuit);
    w.u32(avId);
    w.i16(networkTime(now_));
    return finish(w);
}

bool Client::sendCannonPosition(float zRot, float angle) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedCannonGame, setCannonPosition);
    w.i32(int32_t(zRot * 100.0f));
    w.u32(uint32_t(angle * 100.0f));
    return finish(w);
}

bool Client::sendCannonLit(float zRot, float angle) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedCannonGame, setCannonLit);
    w.i32(int32_t(zRot * 100.0f));
    w.u32(uint32_t(angle * 100.0f));
    return finish(w);
}

bool Client::sendCannonToonWillLandInWater(float landTime) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedCannonGame, setToonWillLandInWater);
    w.i32(int32_t(landTime * 100.0f));
    return finish(w);
}

bool Client::sendTugPlayerReady(uint8_t side) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTugOfWarGame, reportPlayerReady);
    w.u8(side);
    return finish(w);
}

bool Client::sendTugKeyRate(uint32_t rate, float force) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTugOfWarGame, reportCurrentKeyRate);
    w.u32(rate);
    w.i16(int16_t(force * 100.0f));
    return finish(w);
}

bool Client::sendTugEndOfContest(uint8_t losingSide) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTugOfWarGame, reportEndOfContest);
    w.u8(losingSide);
    return finish(w);
}

bool Client::sendTugNewAvIdList(const uint32_t* avIds, int count) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTugOfWarGame, sendNewAvIdList);
    w.u16(uint16_t(count * 4));
    for (int i = 0; i < count; ++i) w.u32(avIds[i]);
    return finish(w);
}

bool Client::sendRingToonGotRing(uint8_t success) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedRingGame, setToonGotRing);
    w.u8(success);
    return finish(w);
}

// DistributedMazeGame
bool Client::sendMazeClaimTreasure(uint32_t treasureNum) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedMazeGame, claimTreasure);
    w.u32(treasureNum);
    return finish(w);
}

bool Client::sendMazeHitBySuit(uint32_t avId, int16_t ts) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedMazeGame, hitBySuit);
    w.u32(avId);
    w.i16(ts);
    return finish(w);
}

// DistributedPairingGame
bool Client::sendPairingOpenCard(int16_t deckIndex, int16_t bonusGlowCard) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedPairingGame, openCardRequest);
    w.i16(deckIndex);
    w.i16(bonusGlowCard);
    return finish(w);
}

bool Client::sendPairingDone() {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedPairingGame, reportDone);
    return finish(w);
}

// DistributedDivingGame
bool Client::sendDivingPickupTreasure(uint32_t chestId) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedDivingGame, pickupTreasure);
    w.u32(chestId);
    return finish(w);
}

bool Client::sendDivingTreasureRecovered() {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedDivingGame, treasureRecovered);
    return finish(w);
}

bool Client::sendDivingFishCollision(uint32_t avId, uint32_t spawnId, uint32_t spawnerId, const char* status) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedDivingGame, handleFishCollision);
    w.u32(avId);
    w.u32(spawnId);
    w.u32(spawnerId);
    w.string(status);
    return finish(w);
}

bool Client::sendDivingCrabCollision(uint32_t avId, const char* status) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedDivingGame, handleCrabCollision);
    w.u32(avId);
    w.string(status);
    return finish(w);
}

bool Client::sendDivingGetCrabMoving(uint32_t crabId, int16_t crabX, int8_t dir) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedDivingGame, getCrabMoving);
    w.u32(crabId);
    w.i16(crabX);
    w.i8(dir);
    return finish(w);
}

// DistributedVineGame
bool Client::sendVineNewVine(uint32_t avId, int8_t vineIndex, float vineT, int8_t facingRight) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedVineGame, setNewVine);
    w.u32(avId);
    w.i8(vineIndex);
    w.u32(uint32_t(vineT * 10000.0f));
    w.i8(facingRight);
    return finish(w);
}

bool Client::sendVineReachedEndVine(int8_t vineIndex) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedVineGame, reachedEndVine);
    w.i8(vineIndex);
    return finish(w);
}

bool Client::sendVineClaimTreasure(uint32_t treasureNum) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedVineGame, claimTreasure);
    w.u32(treasureNum);
    return finish(w);
}

bool Client::sendTargetScore(int32_t scoreX, int32_t scoreY) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTargetGame, setScore);
    w.i32(scoreX);
    w.i32(scoreY);
    return finish(w);
}

bool Client::sendTargetPlayerDone() {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTargetGame, setPlayerDone);
    return finish(w);
}

// DistributedCogThiefGame
bool Client::sendCogThiefPie(int32_t timestamp, float x, float y, float z, float h) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedCogThiefGame, throwingPie);
    w.u32(avatarId());
    w.i32(timestamp);
    w.i32(int32_t(x * 100.0f));
    w.i32(int32_t(y * 100.0f));
    w.i32(int32_t(z * 100.0f));
    w.i32(int32_t(h * 100.0f));
    return finish(w);
}

bool Client::sendCogThiefHitBySuit(uint32_t avId, int32_t timestamp, int8_t suitNum, float x, float y, float z) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedCogThiefGame, hitBySuit);
    w.u32(avId);
    w.i32(timestamp);
    w.i8(suitNum);
    w.i32(int32_t(x * 100.0f));
    w.i32(int32_t(y * 100.0f));
    w.i32(int32_t(z * 100.0f));
    return finish(w);
}

bool Client::sendCogThiefPieHitSuit(uint32_t avId, int32_t timestamp, int8_t suitNum, float x, float y, float z) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedCogThiefGame, pieHitSuit);
    w.u32(avId);
    w.i32(timestamp);
    w.i8(suitNum);
    w.i32(int32_t(x * 100.0f));
    w.i32(int32_t(y * 100.0f));
    w.i32(int32_t(z * 100.0f));
    return finish(w);
}

bool Client::sendCogThiefHitBarrel(int32_t clientStamp, int8_t cogIndex, int8_t barrelIndex, float x, float y, float z) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedCogThiefGame, cogHitBarrel);
    w.i32(clientStamp);
    w.i8(cogIndex);
    w.i8(barrelIndex);
    w.i32(int32_t(x * 100.0f));
    w.i32(int32_t(y * 100.0f));
    w.i32(int32_t(z * 100.0f));
    return finish(w);
}

bool Client::sendCogThiefAtReturnPos(int32_t clientStamp, int8_t cogIndex, int8_t barrelIndex) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedCogThiefGame, cogAtReturnPos);
    w.i32(clientStamp);
    w.i8(cogIndex);
    w.i8(barrelIndex);
    return finish(w);
}

// DistributedTwoDGame
bool Client::sendTwoDClaimTreasure(uint8_t sectionIndex, uint8_t treasureIndex) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTwoDGame, claimTreasure);
    w.u8(sectionIndex);
    w.u8(treasureIndex);
    return finish(w);
}

bool Client::sendTwoDClaimEnemyShot(uint8_t sectionIndex, uint8_t enemyIndex) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTwoDGame, claimEnemyShot);
    w.u8(sectionIndex);
    w.u8(enemyIndex);
    return finish(w);
}

bool Client::sendTwoDReportDone() {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTwoDGame, reportDone);
    return finish(w);
}

bool Client::sendTwoDHitByEnemy(uint32_t avId, int16_t ts) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTwoDGame, toonHitByEnemy);
    w.u32(avId);
    w.i16(ts);
    return finish(w);
}

bool Client::sendTwoDFellDown(uint32_t avId, int16_t ts) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTwoDGame, toonFellDown);
    w.u32(avId);
    w.i16(ts);
    return finish(w);
}

bool Client::sendTwoDSquished(uint32_t avId, int16_t ts) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTwoDGame, toonSquished);
    w.u32(avId);
    w.i16(ts);
    return finish(w);
}

bool Client::sendTwoDVictory(uint32_t avId, int16_t ts) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTwoDGame, toonVictory);
    w.u32(avId);
    w.i16(ts);
    return finish(w);
}

// DistributedTagGame
bool Client::sendTag(uint32_t taggedAvId) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedTagGame, tag);
    w.u32(taggedAvId);
    return finish(w);
}

// DistributedPatternGame
bool Client::sendPatternReady() {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedPatternGame, reportPlayerReady);
    return finish(w);
}

bool Client::sendPatternButton(uint8_t index, uint8_t wrong) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedPatternGame, reportButtonPress);
    w.u8(index);
    w.u8(wrong);
    return finish(w);
}

bool Client::sendPatternResult(const uint8_t* pattern, uint8_t len, uint16_t totalTimeMs) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedPatternGame, reportPlayerPattern);
    w.u16(len);
    for (uint8_t i = 0; i < len; ++i) w.u8(pattern[i]);
    w.u16(totalTimeMs);
    return finish(w);
}

// DistributedIceGame
bool Client::sendIceChoice(float force, float direction) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedIceGame, setAvatarChoice);
    w.i32(int32_t(force * 100.0f));
    w.i32(int32_t(direction * 100.0f));
    return finish(w);
}

bool Client::sendIceEndingPositions(const float* coords, int count) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedIceGame, endingPositions);
    w.u16(uint16_t(count * 6)); // Coord3 is 3 * int16
    for (int i = 0; i < count * 3; ++i) w.i16(int16_t(coords[i] * 10.0f));
    return finish(w);
}

bool Client::sendIceScoringMovieDone() {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedIceGame, reportScoringMovieDone);
    return finish(w);
}

bool Client::sendIceClaimTreasure(uint8_t n) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedIceGame, claimTreasure);
    w.u8(n);
    return finish(w);
}

bool Client::sendIceClaimPenalty(uint8_t n) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedIceGame, claimPenalty);
    w.u8(n);
    return finish(w);
}

// DistributedRaceGame
bool Client::sendRaceChoice(uint8_t choice) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedRaceGame, setAvatarChoice);
    w.u8(choice);
    return finish(w);
}

// ============================== Goofy Speedway racing ======================
void Client::clearRacing() {
    for (RacingPad& p : racingPads_) p = RacingPad{};
    for (RacingBlock& b : racingBlocks_) b = RacingBlock{};
    for (RacingVehicle& v : racingVehicles_) v = RacingVehicle{};
    if (racingRace_.active) pushEvent(EventType::RacingGone, racingRace_.doId);
    racingRace_ = RacingRace{};
}

bool Client::handleRacingEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                               uint16_t reqCount, bool other) {
    const bool padClass = classId == dc::DistributedRacePad::kClassId;
    const bool blockClass = classId == dc::DistributedStartingBlock::kClassId;
    const bool vehicleClass = classId == dc::DistributedVehicle::kClassId;
    const bool raceClass = classId == dc::DistributedRace::kClassId;
    if (!padClass && !blockClass && !vehicleClass && !raceClass) return false;
    auto apply = [&](uint16_t id) {
        if (padClass) {
            RacingPad* p = nullptr;
            for (RacingPad& s : racingPads_) if (s.active && s.doId == doId) p = &s;
            if (!p) for (RacingPad& s : racingPads_) if (!s.active) { p = &s; break; }
            if (!p) { skipField(r, id); return; }
            p->doId = doId;
            if (id == dc::DistributedRacePad::k_setState) {
                r.string(p->state, sizeof(p->state)); r.i16();
            } else if (id == dc::DistributedRacePad::k_setRaceZone) p->raceZone = r.u32();
            else if (id == dc::DistributedRacePad::k_setTrackInfo) {
                Bytes b = r.blob(); p->trackCount = uint8_t(std::min<size_t>(b.len / 2, 8));
                for (uint8_t i = 0; i < p->trackCount; ++i) p->trackIds[i] = uint16_t(b.data[2*i] | (b.data[2*i+1] << 8));
            } else skipField(r, id);
            p->active = r.ok();
        } else if (blockClass) {
            RacingBlock* b = nullptr;
            for (RacingBlock& s : racingBlocks_) if (s.active && s.doId == doId) b = &s;
            if (!b) for (RacingBlock& s : racingBlocks_) if (!s.active) { b = &s; break; }
            if (!b) { skipField(r, id); return; }
            b->doId = doId;
            if (id == dc::DistributedStartingBlock::k_setPadDoId) b->padDoId = r.u32();
            else if (id == dc::DistributedStartingBlock::k_setPadLocationId) b->location = r.u8();
            else if (id == dc::DistributedStartingBlock::k_setOccupied) b->occupant = r.u32();
            else if (id == dc::DistributedStartingBlock::k_setPosHpr) {
                b->x=r.i16()/10.0f; b->y=r.i16()/10.0f; b->z=r.i16()/10.0f;
                b->h=r.i16()/10.0f; b->p=r.i16()/10.0f; b->r=r.i16()/10.0f;
            } else skipField(r, id);
            b->active = r.ok();
        } else if (vehicleClass) {
            RacingVehicle* v = nullptr;
            for (RacingVehicle& s : racingVehicles_) if (s.active && s.doId == doId) v = &s;
            if (!v) for (RacingVehicle& s : racingVehicles_) if (!s.active) { v = &s; break; }
            if (!v) { skipField(r, id); return; }
            v->doId = doId;
            switch (id) {
            case dc::DistributedVehicle::k_setOwner: v->owner = r.u32(); break;
            case dc::DistributedVehicle::k_setState: v->state = char(r.i8()); v->driver = r.u32(); break;
            case dc::DistributedVehicle::k_setBodyType: v->body = r.i8(); break;
            case dc::DistributedVehicle::k_setBodyColor: v->bodyColor = r.i8(); break;
            case dc::DistributedVehicle::k_setAccessoryColor: v->accessoryColor = r.i8(); break;
            case dc::DistributedVehicle::k_setEngineBlockType: v->engine = r.i8(); break;
            case dc::DistributedVehicle::k_setSpoilerType: v->spoiler = r.i8(); break;
            case dc::DistributedVehicle::k_setFrontWheelWellType: v->frontWheels = r.i8(); break;
            case dc::DistributedVehicle::k_setBackWheelWellType: v->backWheels = r.i8(); break;
            case dc::DistributedVehicle::k_setRimType: v->rims = r.i8(); break;
            case dc::DistributedVehicle::k_setDecalType: v->decal = r.i8(); break;
            default: skipField(r, id); break;
            }
            v->active = r.ok();
        } else {
            racingRace_.doId = doId; racingRace_.active = true;
            switch (id) {
            case dc::DistributedRace::k_setZoneId: racingRace_.zone = r.u32(); break;
            case dc::DistributedRace::k_setTrackId: racingRace_.track = r.u16(); break;
            case dc::DistributedRace::k_setRaceType: racingRace_.raceType = r.u16(); break;
            case dc::DistributedRace::k_setLapCount: racingRace_.laps = r.u8(); break;
            case dc::DistributedRace::k_setAvatars: {
                Bytes b = r.blob(); racingRace_.numRacers = uint8_t(std::min<size_t>(b.len/4, 8));
                for (uint8_t i=0;i<racingRace_.numRacers;++i) memcpy(&racingRace_.racers[i], b.data+4*i, 4);
                break;
            }
            default: skipField(r, id); break;
            }
        }
    };
    for (uint16_t i=0; i<reqCount && r.ok(); ++i) apply(req[i]);
    if (other && r.ok()) {
        const uint16_t n = r.u16();
        for (uint16_t i=0; i<n && r.ok(); ++i) apply(r.u16());
    }
    if (!r.ok()) return true;
    if (raceClass) pushEvent(EventType::RacingRaceUp, doId);
    else if (padClass) pushEvent(EventType::RacingPadUp, doId);
    else if (blockClass) pushEvent(EventType::RacingBlockUp, doId);
    else pushEvent(EventType::RacingVehicleUp, doId);
    return true;
}

bool Client::handleRacingField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    for (RacingPad& p : racingPads_) if (p.active && p.doId == doId) {
        if (fieldId == dc::DistributedRacePad::k_setState) { r.string(p.state, sizeof(p.state)); r.i16(); }
        else if (fieldId == dc::DistributedRacePad::k_setRaceZone) p.raceZone = r.u32();
        else if (fieldId == dc::DistributedRacePad::k_setTrackInfo) { Bytes b=r.blob(); p.trackCount=uint8_t(std::min<size_t>(b.len/2,8)); for(uint8_t i=0;i<p.trackCount;++i)p.trackIds[i]=uint16_t(b.data[2*i]|(b.data[2*i+1]<<8)); }
        else skipField(r, fieldId);
        if (r.ok()) pushEvent(EventType::RacingPadState, doId);
        return true;
    }
    for (RacingBlock& b : racingBlocks_) if (b.active && b.doId == doId) {
        if (fieldId == dc::DistributedStartingBlock::k_setOccupied) b.occupant=r.u32();
        else if (fieldId == dc::DistributedStartingBlock::k_setPosHpr) { b.x=r.i16()/10.0f;b.y=r.i16()/10.0f;b.z=r.i16()/10.0f;b.h=r.i16()/10.0f;b.p=r.i16()/10.0f;b.r=r.i16()/10.0f; }
        else skipField(r, fieldId);
        if (r.ok()) pushEvent(EventType::RacingBlockState, doId);
        return true;
    }
    for (RacingVehicle& v : racingVehicles_) if (v.active && v.doId == doId) {
        if (fieldId == dc::DistributedVehicle::k_setOwner) v.owner=r.u32();
        else if (fieldId == dc::DistributedVehicle::k_setState) { v.state=char(r.i8());v.driver=r.u32(); }
        else skipField(r, fieldId);
        if (r.ok()) pushEvent(EventType::RacingVehicleState, doId);
        return true;
    }
    if (racingRace_.active && racingRace_.doId == doId) {
        if (fieldId == dc::DistributedRace::k_setTrackId) racingRace_.track=r.u16();
        else if (fieldId == dc::DistributedRace::k_setLapCount) racingRace_.laps=r.u8();
        else if (fieldId == dc::DistributedRace::k_startRace) { racingRace_.startTs=r.i16(); racingRace_.started=true; }
        else if (fieldId == dc::DistributedRace::k_setPlace) { r.u32(); r.i32(); racingRace_.localLap=r.u8(); racingRace_.localPlace=r.u32(); }
        else if (fieldId == dc::DistributedRace::k_goToSpeedway) { skipField(r, fieldId); racingRace_.finished=true; }
        else skipField(r, fieldId);
        if (r.ok()) pushEvent(EventType::RacingRaceState, doId);
        return true;
    }
    return false;
}

bool Client::sendRacingEnter(uint32_t blockDoId, bool paid) {
    if (state_ != State::InZone) return false;
    DatagramWriter w=beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, blockDoId, DistributedStartingBlock, requestEnter); w.u8(paid ? 1 : 0); return finish(w);
}
bool Client::sendRacingExit(uint32_t blockDoId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w=beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, blockDoId, DistributedStartingBlock, requestExit); return finish(w);
}
bool Client::sendRacingMovieDone(uint32_t blockDoId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w=beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, blockDoId, DistributedStartingBlock, movieFinished); return finish(w);
}
bool Client::sendRacingControl(uint32_t vehicleDoId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w=beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, vehicleDoId, DistributedVehicle, requestControl); return finish(w);
}
bool Client::sendRacingPark(uint32_t vehicleDoId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w=beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, vehicleDoId, DistributedVehicle, requestParked); return finish(w);
}
bool Client::sendRacingInput(uint32_t vehicleDoId, int8_t input) {
    (void)vehicleDoId; (void)input;
    return false; // setInput is clrecv in the stock DC; movement uses DistributedSmoothNode.
}
bool Client::sendRacingThrow(uint32_t raceDoId, float x, float y, float z) {
    if (state_ != State::InZone) return false;
    DatagramWriter w=beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, raceDoId, DistributedRace, requestThrow); w.i32(int32_t(x*1000)); w.i32(int32_t(y*1000)); w.i32(int32_t(z*1000)); return finish(w);
}
bool Client::sendRacingGag(uint32_t raceDoId, uint8_t track, uint8_t level, uint8_t count) {
    if (state_ != State::InZone) return false;
    DatagramWriter w=beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, raceDoId, DistributedRace, hasGag); w.u8(track); w.u8(level); w.u8(count); return finish(w);
}

// DistributedPhotoGame
bool Client::sendPhotoScore(uint8_t subjectIndex, const char* pose, float score) {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedPhotoGame, newClientPhotoScore);
    w.u8(subjectIndex);
    w.string(pose ? pose : "");
    w.u32(uint32_t(score * 100.0f));
    return finish(w);
}

bool Client::sendPhotoFilmOut() {
    if (!minigame_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, minigame_.doId, DistributedPhotoGame, filmOut);
    return finish(w);
}

bool Client::sendPurchaseExit() {
    if (!purchase_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, purchase_.doId, PurchaseManager, requestExit);
    return finish(w);
}

bool Client::sendPurchaseInventory() {
    return sendPurchaseInventory(ownStats_.inventory, ownStats_.money);
}

bool Client::sendPurchaseInventory(const uint8_t inv[7][7], int16_t money) {
    if (!purchase_.active) return false;
    uint8_t flat[49];
    for (int t = 0; t < 7; ++t) {
        for (int l = 0; l < 7; ++l) {
            flat[t * 7 + l] = inv[t][l];
            ownStats_.inventory[t][l] = inv[t][l];
        }
    }
    ownStats_.money = money;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, purchase_.doId, PurchaseManager, setInventory);
    w.blob(flat, sizeof(flat));
    w.i16(money);
    w.u8(1);
    return finish(w);
}

// ============================================================== Cog buildings
namespace {
template <typename T>
uint8_t readU32Array(DatagramReader& r, T* out, uint8_t cap) {
    Bytes b = r.blob();
    if (!r.ok()) return 0;
    DatagramReader a(b.data, b.len);
    uint8_t n = 0;
    while (a.remaining() >= 4) {
        uint32_t v = a.u32();
        if (n < cap) out[n++] = v;
    }
    if (!a.ok()) r.fail();
    return n;
}
}

uint32_t Client::findDoor(uint32_t block, uint8_t doorIndex) const {
    for (size_t i = 0; i < numDoors_; ++i) {
        if (doors_[i].block == block && doors_[i].doorIndex == doorIndex) return doors_[i].doId;
    }
    for (size_t i = 0; i < numDoors_; ++i) {
        if (doors_[i].block == block) return doors_[i].doId;
    }
    return 0;
}

const CogBuilding* Client::findCogBuilding(uint32_t doId) const {
    for (const CogBuilding& b : cogBuildings_) if (b.active && b.doId == doId) return &b;
    return nullptr;
}

const CogElevator* Client::findCogElevator(uint32_t doId) const {
    for (const CogElevator& e : cogElevators_) if (e.active && e.doId == doId) return &e;
    return nullptr;
}

bool Client::applyCogBuildingField(CogBuilding& b, DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedBuilding::k_setBlock) {
        b.block = r.u16();
        b.interiorZoneId = r.u32();
        return r.ok();
    }
    if (fieldId == dc::DistributedBuilding::k_setSuitData) {
        b.track = r.i8(); b.difficulty = r.i8(); b.numFloors = r.i8();
        return r.ok();
    }
    if (fieldId == dc::DistributedBuilding::k_setVictorList) {
        b.numVictors = readU32Array(r, b.victors, 8);
        return r.ok();
    }
    if (fieldId == dc::DistributedBuilding::k_setState) {
        char state[24];
        r.string(state, sizeof(state));
        const int16_t ts = r.i16();
        b.stateStartMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
        if (!strcmp(state, "toon")) b.state = CogBuildingState::Toon;
        else if (!strcmp(state, "suit")) b.state = CogBuildingState::Suit;
        else if (!strcmp(state, "becomingSuit")) b.state = CogBuildingState::BecomingSuit;
        else if (!strcmp(state, "becomingToon")) b.state = CogBuildingState::BecomingToon;
        // ---- cogdo exterior states ----
        else if (!strcmp(state, "cogdo") || !strcmp(state, "becomingCogdo") ||
                 !strcmp(state, "becomingCogdoFromCogdo") || !strcmp(state, "waitForVictorsFromCogdo")) b.state = CogBuildingState::Cogdo;
        // ---- end cogdo exterior states ----
        else b.state = CogBuildingState::Other;
        b.cogdoVictory = !strcmp(state, "waitForVictorsFromCogdo"); // cogdo
        return r.ok();
    }
    return skipField(r, fieldId);
}

bool Client::applyCogElevatorField(CogElevator& e, DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedElevator::k_setBldgDoId) e.buildingDoId = r.u32();
    else if (fieldId == dc::DistributedElevator::k_setState) {
        char state[sizeof(e.state)];
        r.string(state, sizeof(state));
        const int16_t ts = r.i16();
        strncpy(e.state, state, sizeof(e.state) - 1);
        e.state[sizeof(e.state) - 1] = 0;
        e.stateStartMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
    } else if (fieldId >= dc::DistributedElevator::k_fillSlot0 &&
               fieldId <= dc::DistributedElevator::k_fillSlot7) {
        const int n = fieldId - dc::DistributedElevator::k_fillSlot0;
        e.slots[n] = r.u32(); e.slotFlags[n] = r.u8();
    } else if (fieldId >= dc::DistributedElevator::k_emptySlot0 &&
               fieldId <= dc::DistributedElevator::k_emptySlot7) {
        // emptySlotN(avId uint32, reason int8, timestamp int16, timeSent int16) in toon.dc.
        const int n = fieldId - dc::DistributedElevator::k_emptySlot0;
        e.slots[n] = 0;
        r.u32();
        r.i8();
        r.i16();
        r.i16();
    } else if (fieldId == dc::DistributedElevator::k_setElevatorTripId) e.tripId = r.u32();
    else if (fieldId == dc::DistributedElevator::k_setMinLaff) e.minLaff = r.u8();
    else if (fieldId == dc::DistributedFactoryElevatorExt::k_setEntranceId ||
             fieldId == dc::DistributedLawOfficeElevatorExt::k_setEntranceId) e.origin = int8_t(r.u8());
    else if (fieldId == dc::DistributedCogKart::k_setPosHpr) {
        e.x = r.i16() / 10.0f, e.y = r.i16() / 10.0f, e.z = r.i16() / 10.0f;
        e.h = r.i16() / 10.0f;
        r.i16(); // P
        r.i16(); // R
        e.hasPose = r.ok();
    } else if (fieldId == dc::DistributedCogKart::k_setCountryClubId) r.u16();
    else if (fieldId == dc::DistributedMintElevatorExt::k_setMintId) {
        // DistributedMintElevatorExt.setMintId: CashbotMintIntA/B/C (12500/12600/12700) -> origin 1/2/0.
        const uint16_t mint = r.u16();
        e.origin = mint == 12500 ? 1 : mint == 12600 ? 2 : mint == 12700 ? 0 : -1;
    } else if (fieldId == dc::DistributedFactoryElevatorExt::k_setFactoryInteriorZone ||
               fieldId == dc::DistributedFactoryElevatorExt::k_setFactoryInteriorZoneForce ||
               fieldId == dc::DistributedMintElevatorExt::k_setMintInteriorZone ||
               fieldId == dc::DistributedMintElevatorExt::k_setMintInteriorZoneForce ||
               fieldId == dc::DistributedLawOfficeElevatorExt::k_setLawOfficeInteriorZone ||
               fieldId == dc::DistributedLawOfficeElevatorExt::k_setLawOfficeInteriorZoneForce ||
               fieldId == dc::DistributedCogKart::k_setCountryClubInteriorZone ||
               fieldId == dc::DistributedCogKart::k_setCountryClubInteriorZoneForce ||
               fieldId == dc::DistributedBossElevator::k_setBossOfficeZone ||
               fieldId == dc::DistributedBossElevator::k_setBossOfficeZoneForce) {
        e.destZone = r.u32();
        e.forcedBossDestination = fieldId == dc::DistributedBossElevator::k_setBossOfficeZoneForce;
        if (r.ok()) pushEvent(EventType::CogElevatorUp, e.doId); // the riders' cue to go
    } else return skipField(r, fieldId);
    return r.ok();
}

bool Client::applyCogInteriorField(CogInterior& i, DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedSuitInterior::k_setZoneId) i.zoneId = r.u32();
    else if (fieldId == dc::DistributedSuitInterior::k_setExtZoneId) i.extZoneId = r.u32();
    else if (fieldId == dc::DistributedSuitInterior::k_setDistBldgDoId) i.buildingDoId = r.u32();
    else if (fieldId == dc::DistributedSuitInterior::k_setNumFloors) i.numFloors = r.i8();
    else if (fieldId == dc::DistributedSuitInterior::k_setToons) {
        i.numToons = readU32Array(r, i.toons, 8); r.u16();
    } else if (fieldId == dc::DistributedSuitInterior::k_setSuits) {
        i.numSuits = readU32Array(r, i.suits, 8);
        i.numSuits = readU32Array(r, i.suitLevels, 8);
        Bytes hp = r.blob();
        DatagramReader a(hp.data, hp.len);
        for (uint8_t n = 0; n < i.numSuits && a.remaining() >= 2; ++n) i.suitHp[n] = a.u16();
        if (!a.ok()) r.fail();
    } else if (fieldId == dc::DistributedSuitInterior::k_setState) {
        r.string(i.state, sizeof(i.state));
        const int16_t ts = r.i16();
        i.stateStartMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
    } else return skipField(r, fieldId);
    return r.ok();
}

bool Client::handleCogBuildingEnter(uint32_t doId, DatagramReader& r, const uint16_t* req,
                                    uint16_t reqCount, bool other) {
    CogBuilding* dst = nullptr;
    for (CogBuilding& b : cogBuildings_) if (b.active && b.doId == doId) dst = &b;
    for (CogBuilding& b : cogBuildings_) if (!dst && !b.active) dst = &b;
    if (!dst) {
        TT_LOG("net: no free slot for building %u", unsigned(doId));
        return false;
    }
    CogBuilding tmp{};
    tmp.doId = doId;
    bool ok = true;
    for (uint16_t n = 0; n < reqCount && ok; ++n) ok = applyCogBuildingField(tmp, r, req[n]);
    if (ok && other) {
        const uint16_t count = r.u16();
        for (uint16_t n = 0; n < count && ok; ++n) ok = applyCogBuildingField(tmp, r, r.u16());
    }
    if (!ok || !r.ok()) return false;
    tmp.active = true;
    *dst = tmp;
    if (tmp.state != CogBuildingState::Toon) {
        TT_LOG("net: building %u block %u state %d", unsigned(doId), unsigned(tmp.block), int(tmp.state));
    }
    pushEvent(EventType::CogBuildingUp, doId);
    return true;
}

bool Client::handleCogElevatorEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                                    uint16_t reqCount, bool other) {
    CogElevator* dst = nullptr;
    for (CogElevator& e : cogElevators_) if (e.active && e.doId == doId) dst = &e;
    for (CogElevator& e : cogElevators_) if (!dst && !e.active) dst = &e;
    if (!dst) {
        TT_LOG("net: no free slot for elevator %u (class %u)", unsigned(doId), unsigned(classId));
        return false;
    }
    CogElevator tmp{}; tmp.doId = doId; tmp.classId = classId;
    bool ok = true;
    for (uint16_t n = 0; n < reqCount && ok; ++n) ok = applyCogElevatorField(tmp, r, req[n]);
    if (ok && other) {
        const uint16_t count = r.u16();
        for (uint16_t n = 0; n < count && ok; ++n) ok = applyCogElevatorField(tmp, r, r.u16());
    }
    if (!ok || !r.ok()) {
        TT_LOG("net: elevator %u (class %u) failed to parse", unsigned(doId), unsigned(classId));
        return false;
    }
    tmp.active = true; *dst = tmp; pushEvent(EventType::CogElevatorUp, doId);
    TT_LOG("net: elevator %u class %u building %u state %s", unsigned(doId), unsigned(classId),
           unsigned(tmp.buildingDoId), tmp.state);
    return true;
}

bool Client::handleCogInteriorEnter(uint32_t doId, DatagramReader& r, const uint16_t* req,
                                    uint16_t reqCount, bool other) {
    CogInterior tmp{}; tmp.doId = doId;
    bool ok = true;
    for (uint16_t n = 0; n < reqCount && ok; ++n) ok = applyCogInteriorField(tmp, r, req[n]);
    if (ok && other) {
        const uint16_t count = r.u16();
        for (uint16_t n = 0; n < count && ok; ++n) ok = applyCogInteriorField(tmp, r, r.u16());
    }
    if (!ok || !r.ok()) return false;
    tmp.active = true; cogInterior_ = tmp; pushEvent(EventType::CogInteriorUp, doId);
    return true;
}

bool Client::handleCogField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    for (CogBuilding& b : cogBuildings_) if (b.active && b.doId == doId) {
        if (applyCogBuildingField(b, r, fieldId)) pushEvent(EventType::CogBuildingState, doId);
        return true;
    }
    for (CogElevator& e : cogElevators_) if (e.active && e.doId == doId) {
        if (applyCogElevatorField(e, r, fieldId)) pushEvent(EventType::CogElevatorUp, doId);
        return true;
    }
    if (cogInterior_.active && cogInterior_.doId == doId) {
        if (applyCogInteriorField(cogInterior_, r, fieldId)) pushEvent(EventType::CogInteriorState, doId);
        return true;
    }
    return false;
}

void Client::clearCogObjects() {
    clearCogdo(); // cogdo lifecycle
    for (CogBuilding& b : cogBuildings_) {
        if (b.active) pushEvent(EventType::CogBuildingGone, b.doId);
        b = CogBuilding{};
    }
    for (CogElevator& e : cogElevators_) e = CogElevator{};
    cogInterior_ = CogInterior{};
    clearLevelObjects();
}

bool Client::sendCogElevatorBoard(uint32_t doId) {
    if (state_ != State::InZone || !findCogElevator(doId)) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedElevator, requestBoard);
    return finish(w);
}

bool Client::sendCogElevatorExit(uint32_t doId) {
    if (state_ != State::InZone || !findCogElevator(doId)) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedElevator, requestExit);
    return finish(w);
}

bool Client::sendCogInteriorAvatarJoined(uint32_t doId) {
    if (!cogInterior_.active || cogInterior_.doId != doId) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedSuitInterior, setAvatarJoined);
    return finish(w);
}

bool Client::sendCogInteriorElevatorDone(uint32_t doId) {
    if (!cogInterior_.active || cogInterior_.doId != doId) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedSuitInterior, elevatorDone);
    return finish(w);
}

bool Client::sendCogInteriorReserveJoinDone(uint32_t doId) {
    if (!cogInterior_.active || cogInterior_.doId != doId) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedSuitInterior, reserveJoinDone);
    return finish(w);
}

bool Client::applyEstateField(Estate& e, DatagramReader& r, uint16_t fieldId) {
        if (fieldId == dc::DistributedEstate::k_setEstateType) e.estateType = r.u8();
        else if (fieldId == dc::DistributedEstate::k_setClosestHouse) e.closestHouse = r.u8();
        else if (fieldId == dc::DistributedEstate::k_setEstateReady) e.ready = true;
        else if (fieldId == dc::DistributedEstate::k_setSlot0ToonId) e.slotToons[0] = r.u32();
        else if (fieldId == dc::DistributedEstate::k_setSlot1ToonId) e.slotToons[1] = r.u32();
        else if (fieldId == dc::DistributedEstate::k_setSlot2ToonId) e.slotToons[2] = r.u32();
        else if (fieldId == dc::DistributedEstate::k_setSlot3ToonId) e.slotToons[3] = r.u32();
        else if (fieldId == dc::DistributedEstate::k_setSlot4ToonId) e.slotToons[4] = r.u32();
        else if (fieldId == dc::DistributedEstate::k_setSlot5ToonId) e.slotToons[5] = r.u32();
        else if (fieldId == dc::DistributedEstate::k_setDecorData) { r.blob(); }
        else return skipField(r, fieldId);
        return r.ok();
}

bool Client::applyHouseField(EstateHouse& h, DatagramReader& r, uint16_t fieldId) {
        if (fieldId == dc::DistributedHouse::k_setHousePos) h.pos = r.u8();
        else if (fieldId == dc::DistributedHouse::k_setHouseType) h.type = r.u8();
        else if (fieldId == dc::DistributedHouse::k_setGardenPos) h.gardenPos = r.u8();
        else if (fieldId == dc::DistributedHouse::k_setAvatarId) h.avatarId = r.u32();
        else if (fieldId == dc::DistributedHouse::k_setName) r.string(h.name, sizeof(h.name));
        else if (fieldId == dc::DistributedHouse::k_setColor) h.color = r.u8();
        else if (fieldId == dc::DistributedHouse::k_setHouseReady) {}
        else if (fieldId == dc::DistributedHouse::k_setInteriorItems) r.blob();
        else return skipField(r, fieldId);
        return r.ok();
}

bool Client::applyHouseInteriorField(HouseInterior& h, DatagramReader& r, uint16_t fieldId) {
        if (fieldId == dc::DistributedHouseInterior::k_setHouseId) h.houseId = r.u32();
        else if (fieldId == dc::DistributedHouseInterior::k_setHouseIndex) h.houseIndex = r.u8();
        else if (fieldId == dc::DistributedHouseInterior::k_setWallpaper ||
                 fieldId == dc::DistributedHouseInterior::k_setWindows) r.blob();
        else return skipField(r, fieldId);
        return r.ok();
}

bool Client::applyFurnitureField(EstateFurniture& f, DatagramReader& r, uint16_t fieldId) {
        if (fieldId == dc::DistributedFurnitureItem::k_setItem) { f.itemId = r.u32(); r.blob(); }
        else if (fieldId == dc::DistributedFurnitureItem::k_setMode) { f.mode = r.u8(); r.u32(); }
        else if (fieldId == dc::DistributedSmoothNode::k_setComponentX) f.x = r.i16() / 10.0f;
        else if (fieldId == dc::DistributedSmoothNode::k_setComponentY) f.y = r.i16() / 10.0f;
        else if (fieldId == dc::DistributedSmoothNode::k_setComponentZ) f.z = r.i16() / 10.0f;
        else if (fieldId == dc::DistributedSmoothNode::k_setComponentH) f.h = r.i16() / 10.0f;
        else if (fieldId == dc::DistributedSmoothNode::k_setComponentP) f.p = r.i16() / 10.0f;
        else if (fieldId == dc::DistributedSmoothNode::k_setComponentR) f.r = r.i16() / 10.0f;
        else return skipField(r, fieldId);
        return r.ok();
}

bool Client::applyPetField(Pet& p, DatagramReader& r, uint16_t fieldId) {
        if (fieldId == dc::DistributedPet::k_setOwnerId) p.ownerId = r.u32();
        else if (fieldId == dc::DistributedPet::k_setPetName) r.string(p.name, sizeof(p.name));
        else if (fieldId == dc::DistributedPet::k_setTraitSeed) p.traitSeed = r.u32();
        else if (fieldId == dc::DistributedPet::k_setSafeZone) p.safeZone = r.u32();
        else if (fieldId == dc::DistributedPet::k_setHead) p.head = r.i8();
        else if (fieldId == dc::DistributedPet::k_setEars) p.ears = r.i8();
        else if (fieldId == dc::DistributedPet::k_setNose) p.nose = r.i8();
        else if (fieldId == dc::DistributedPet::k_setTail) p.tail = r.i8();
        else if (fieldId == dc::DistributedPet::k_setBodyTexture) p.body = r.i8();
        else if (fieldId == dc::DistributedPet::k_setColor) p.color = r.i8();
        else if (fieldId == dc::DistributedPet::k_setColorScale) p.colorScale = r.i8();
        else if (fieldId == dc::DistributedPet::k_setEyeColor) p.eyeColor = r.i8();
        else if (fieldId == dc::DistributedPet::k_setGender) p.gender = r.i8();
        else if (fieldId == dc::DistributedSmoothNode::k_setComponentX) p.x = r.i16() / 10.0f;
        else if (fieldId == dc::DistributedSmoothNode::k_setComponentY) p.y = r.i16() / 10.0f;
        else if (fieldId == dc::DistributedSmoothNode::k_setComponentZ) p.z = r.i16() / 10.0f;
        else if (fieldId == dc::DistributedSmoothNode::k_setComponentH) p.h = r.i16() / 10.0f;
        else return skipField(r, fieldId);
        return r.ok();
}

bool Client::handleEstateEnter(uint32_t doId, uint16_t classId, DatagramReader& r,
                                   const uint16_t* req, uint16_t reqCount, bool other) {
        auto parse = [&](auto& object, auto apply) {
            bool ok = true;
            for (uint16_t n = 0; n < reqCount && ok; ++n) ok = apply(object, r, req[n]);
            if (ok && other) { const uint16_t count = r.u16(); for (uint16_t n = 0; n < count && ok; ++n) ok = apply(object, r, r.u16()); }
            return ok && r.ok();
        };
        if (classId == dc::DistributedEstate::kClassId) {
            Estate e{}; e.doId = doId;
            if (!parse(e, [this](Estate& o, DatagramReader& rr, uint16_t id) { return applyEstateField(o, rr, id); })) return false;
            e.active = true; estate_ = e; pushEvent(EventType::EstateUp, doId); return true;
        }
        if (classId == dc::DistributedHouseInterior::kClassId) {
            HouseInterior h{}; h.doId = doId;
            if (!parse(h, [this](HouseInterior& o, DatagramReader& rr, uint16_t id) { return applyHouseInteriorField(o, rr, id); })) return false;
            h.active = true; houseInterior_ = h; pushEvent(EventType::HouseInteriorUp, doId); return true;
        }
        if (classId == dc::DistributedHouse::kClassId) {
            EstateHouse* dst = nullptr;
            for (EstateHouse& h : estate_.houses) if (h.active && h.doId == doId) dst = &h;
            for (EstateHouse& h : estate_.houses) if (!dst && !h.active) dst = &h;
            if (!dst) return false;
            EstateHouse h{}; h.doId = doId;
            if (!parse(h, [this](EstateHouse& o, DatagramReader& rr, uint16_t id) { return applyHouseField(o, rr, id); })) return false;
            h.active = true; *dst = h;
            pushEvent(EventType::HouseUp, doId); return true;
        }
        if (classHasField(*findClass(classId), dc::DistributedPet::k_setHead)) {
            Pet* dst = nullptr; for (Pet& p : pets_) if (p.active && p.doId == doId) dst = &p;
            for (Pet& p : pets_) if (!dst && !p.active) dst = &p;
            if (!dst) return false;
            Pet p{}; p.doId = doId;
            if (!parse(p, [this](Pet& o, DatagramReader& rr, uint16_t id) { return applyPetField(o, rr, id); })) return false;
            p.active = true; *dst = p;
            pushEvent(EventType::PetUp, doId); return true;
        }
        EstateFurniture* dst = nullptr;
        for (EstateFurniture& f : furniture_) if (f.active && f.doId == doId) dst = &f;
        for (EstateFurniture& f : furniture_) if (!dst && !f.active) dst = &f;
        if (!dst) return false;
        EstateFurniture f{}; f.doId = doId;
        if (!parse(f, [this](EstateFurniture& o, DatagramReader& rr, uint16_t id) { return applyFurnitureField(o, rr, id); })) return false;
        f.active = true; *dst = f;
        pushEvent(EventType::FurnitureUp, doId); return true;
}

bool Client::handleEstateField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
        if (estate_.active && estate_.doId == doId) { if (applyEstateField(estate_, r, fieldId)) pushEvent(EventType::EstateState, doId); return true; }
        if (houseInterior_.active && houseInterior_.doId == doId) { applyHouseInteriorField(houseInterior_, r, fieldId); return true; }
        for (EstateHouse& h : estate_.houses) if (h.active && h.doId == doId) { applyHouseField(h, r, fieldId); pushEvent(EventType::HouseState, doId); return true; }
        for (EstateFurniture& f : furniture_) if (f.active && f.doId == doId) { applyFurnitureField(f, r, fieldId); pushEvent(EventType::FurnitureState, doId); return true; }
        for (Pet& p : pets_) if (p.active && p.doId == doId) { applyPetField(p, r, fieldId); pushEvent(EventType::PetState, doId); return true; }
        return false;
}

void Client::clearEstateObjects() {
        estateManagerDoId_ = 0; estateZone_ = 0; estate_ = Estate{}; houseInterior_ = HouseInterior{};
        closet_ = Closet{}; mailboxDoId_ = 0; bankDoId_ = 0; houseDoorDoId_ = 0;
        for (EstateFurniture& f : furniture_) f = EstateFurniture{};
        for (Pet& p : pets_) p = Pet{};
}

bool Client::requestHome() {
        if (state_ != State::InZone || !estateManagerDoId_) return false;
        DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
        TT_NET_UPDATE(w, estateManagerDoId_, EstateManager, getEstateZone);
        w.u32(avatarId_); w.string(ownName_);
        return finish(w);
}

bool Client::sendEstateClientReady(uint32_t doId) {
        if (state_ != State::InZone) return false;
        DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
        TT_NET_UPDATE(w, doId, DistributedEstate, setClientReady);
        return finish(w);
}

bool Client::sendHouseEnter(uint32_t doId) {
        if (state_ != State::InZone) return false;
        DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
        TT_NET_UPDATE(w, doId, DistributedHouseDoor, requestEnter);
        return finish(w);
}

bool Client::sendHouseExit(uint32_t doId) {
        if (state_ != State::InZone) return false;
        DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
        TT_NET_UPDATE(w, doId, DistributedHouseDoor, requestExit);
        return finish(w);
}

bool Client::sendClosetEnter(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedCloset, enterAvatar);
    return finish(w);
}

bool Client::sendClosetDNA(uint32_t doId, const uint8_t dna[kDnaSize], int8_t finished, uint8_t which) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedCloset, setDNA);
    w.blob(dna, kDnaSize); w.i8(finished); w.u8(which);
    return finish(w);
}

bool Client::sendClosetExit(uint32_t doId) {
    (void)doId;
    return state_ == State::InZone;
}

bool Client::sendMailboxEnter(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedMailbox, avatarEnter);
    return finish(w);
}

bool Client::sendMailboxExit(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedMailbox, avatarExit);
    return finish(w);
}

bool Client::sendBankEnter(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedBank, avatarEnter);
    return finish(w);
}

bool Client::sendBankExit(uint32_t doId) {
    (void)doId;
    return state_ == State::InZone;
}

} // namespace tt::net
