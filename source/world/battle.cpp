#include "world/battle.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "ui/gag_data.h"
#include "world/battle_fx.h"
#include "world/suit_tables.h"
// [cog attacks]
#include "world/cog_attack_tables.h"
#include "world/cog_attacks.h"

namespace tt::world {

namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kPi = 3.14159265f;
constexpr uint32_t kRequestTimeoutMs = 4000;
constexpr uint32_t kRequestCooldownMs = 3000;
constexpr uint32_t kFaceOffTauntMs = 3500;     // BattleBase.FACEOFF_TAUNT_T
constexpr float kSuitSpeed = 4.8f;             // BattleBase.suitSpeed
constexpr float kToonSpeed = 8.0f;             // BattleBase.toonSpeed
constexpr float kRunAwaySec = 3.3f;            // BattleBase.TOON_RUN_T
constexpr float kRunAwayDistance = 25.0f;
constexpr uint32_t kInputTimeoutMs = 20000;    // CLIENT_INPUT_TIMEOUT
constexpr uint32_t kRewardAutoMs = 12000;
constexpr float kJoinRadius = 9.0f;            // DistributedBattleBase battle lockout tube
constexpr float kFlashSec = 0.35f;
constexpr float kDieSec = 0.6f;     // the final shrink (stands in for the gear explosion)
constexpr float kLoseSec = 5.4f;    // MovieUtil: the lose clip plays, kapow at 5.4 of SUIT_LOSE_DURATION 6
constexpr float kToonHeight = 4.0f;

// BattleBase.suitPoints / suitPendingPoints / toonPoints / toonPendingPoints: (x, y, heading)
// relative to the battle node.
struct Slot {
    float x, y, h;
};
constexpr Slot kSuitPoints[4][4] = {
    {{0, 5, 179}},
    {{2, 5.3f, 170}, {-2, 5.3f, 180}},
    {{4, 5.2f, 170}, {0, 6, 179}, {-4, 5.2f, 190}},
    {{6, 4.4f, 160}, {2, 6.3f, 170}, {-2, 6.3f, 190}, {-6, 4.4f, 200}},
};
constexpr Slot kSuitPending[4] = {{-4, 8.2f, 190}, {0, 9, 179}, {4, 8.2f, 170}, {8, 3.2f, 160}};
constexpr Slot kToonPoints[4][4] = {
    {{0, -6, 0}},
    {{1.5f, -6.5f, 5}, {-1.5f, -6.5f, -5}},
    {{3, -6.75f, 5}, {0, -7, 0}, {-3, -6.75f, -5}},
    {{4.5f, -7, 10}, {1.5f, -7.5f, 5}, {-1.5f, -7.5f, -5}, {-4.5f, -7, -10}},
};
constexpr Slot kToonPending[4] = {{-3, -8, -5}, {0, -9, 0}, {3, -8, 5}, {5.5f, -5.5f, 20}};

constexpr uint32_t kColorDamage = 0xFF2020E0; // ABGR, as ui::Balloons takes it
constexpr uint32_t kColorHeal = 0xFF20A020;
constexpr uint32_t kColorMiss = 0xFF707070;
constexpr uint32_t kColorTaunt = 0xFF2030A0;

int popcount8(unsigned m) {
    int n = 0;
    for (; m; m >>= 1) n += int(m & 1u);
    return n;
}

// Rank of bit i among the set bits of mask.
int rankOf(unsigned mask, int i) { return popcount8(mask & ((1u << i) - 1u)); }

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

float headingTo(float fromX, float fromY, float toX, float toY) {
    const float dx = toX - fromX, dy = toY - fromY;
    if (dx * dx + dy * dy < 1e-6f) return 0.0f;
    return atan2f(-dx, dy) / kDegToRad;
}

const SuitInfo* findSuitType(const char* code) {
    for (const SuitInfo& s : kSuitTypes) {
        if (!strcmp(s.code, code)) return &s;
    }
    return nullptr;
}

} // namespace

bool attackAffectsGroup(int track, int level) {
    // ToontownBattleGlobals.AvPropTargetCat[AvPropTarget[track]][level]
    static const uint8_t kCat[4][7] = {{0, 1, 0, 1, 0, 1, 1}, {0, 0, 0, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 1}};
    static const uint8_t kTarget[7] = {0, 3, 0, 2, 3, 3, 3};
    if (track < 0 || track > 6 || level < 0 || level > 6) return false;
    return kCat[kTarget[track]][level] != 0;
}

void Battle::reset() {
    BattleFx* fx = battleFx_;
    *this = Battle();
    battleFx_ = fx;
    if (battleFx_) battleFx_->reset();
}

float Battle::actorHeight(uint32_t doId) const {
    for (int i = 0; i < net::kBattleSuits; ++i)
        if (suits_[i].used && suits_[i].doId == doId) return suits_[i].height;
    for (int i = 0; i < net::kBattleToons; ++i)
        if (toons_[i].used && toons_[i].doId == doId) return toons_[i].height;
    return 0.0f;
}

bool Battle::actorPos(uint32_t doId, float* x, float* y, float* z, float* h, bool* isToon) const {
    for (int i = 0; i < net::kBattleSuits; ++i) {
        if (suits_[i].used && suits_[i].doId == doId) {
            if (x) *x = suits_[i].x;
            if (y) *y = suits_[i].y;
            if (z) *z = suits_[i].z;
            if (h) *h = suits_[i].h;
            if (isToon) *isToon = false;
            return true;
        }
    }
    for (int i = 0; i < net::kBattleToons; ++i) {
        if (toons_[i].used && toons_[i].doId == doId) {
            if (x) *x = toons_[i].x;
            if (y) *y = toons_[i].y;
            if (z) *z = toons_[i].z;
            if (h) *h = toons_[i].h;
            if (isToon) *isToon = true;
            return true;
        }
    }
    return false;
}

// [cog attacks]
const char* Battle::actorCogCode(uint32_t doId) const {
    for (int i = 0; i < net::kBattleSuits; ++i) {
        if (suits_[i].used && suits_[i].doId == doId) {
            return suits_[i].cogCode;
        }
    }
    return "";
}

// ============================================================ geometry
void Battle::toWorld(float lx, float ly, float* wx, float* wy) const {
    const float c = cosf(bh_ * kDegToRad), s = sinf(bh_ * kDegToRad);
    *wx = bx_ + lx * c - ly * s;
    *wy = by_ + lx * s + ly * c;
}

void Battle::fromWorld(float wx, float wy, float* lx, float* ly) const {
    const float c = cosf(bh_ * kDegToRad), s = sinf(bh_ * kDegToRad);
    const float dx = wx - bx_, dy = wy - by_;
    *lx = dx * c + dy * s;
    *ly = -dx * s + dy * c;
}

void Battle::cameraEye(float* x, float* y, float* z) const {
    float ex = 0, ey = 0, ez = 0, tx = 0, ty = 0, tz = 0;
    camera_.sample(nowMs_, *this, &ex, &ey, &ez, &tx, &ty, &tz);
    *x = ex;
    *y = ey;
    *z = ez;
}

void Battle::cameraTarget(float* x, float* y, float* z) const {
    float ex = 0, ey = 0, ez = 0, tx = 0, ty = 0, tz = 0;
    camera_.sample(nowMs_, *this, &ex, &ey, &ez, &tx, &ty, &tz);
    *x = tx;
    *y = ty;
    *z = tz;
}

// ============================================================ requests
bool Battle::requestBattle(net::Client& client, uint32_t cogDoId, float cogX, float cogY, float cogZ, float cogH,
                           float toonX, float toonY, float toonZ, float toonH, uint32_t nowMs) {
    if (phase_ != BattlePhase::Idle || int32_t(nowMs - cooldownUntilMs_) < 0) return false;
    if (!client.sendRequestBattle(cogDoId, cogX, cogY, cogZ, cogH, 0.0f, 0.0f)) return false;
    phase_ = BattlePhase::Requested;
    requestMs_ = nowMs;
    reqX_ = toonX, reqY_ = toonY, reqZ_ = toonZ, reqH_ = toonH;
    joining_ = false;
    return true;
}

bool Battle::requestLevelBattle(net::Client& client, uint32_t cogDoId, float localX, float localY, float localZ,
                                float localH, float toonX, float toonY, float toonZ, float toonH, uint32_t nowMs) {
    if (phase_ != BattlePhase::Idle || int32_t(nowMs - cooldownUntilMs_) < 0) return false;
    if (!client.sendLevelSuitBattle(cogDoId, localX, localY, localZ, localH)) return false;
    phase_ = BattlePhase::Requested;
    requestMs_ = nowMs;
    reqX_ = toonX, reqY_ = toonY, reqZ_ = toonZ, reqH_ = toonH;
    joining_ = false;
    return true;
}

static bool joinable(const net::Battle& b) {
    return b.numToons < net::kBattleToons &&
           (b.state == net::BattleStateId::WaitForInput || b.state == net::BattleStateId::PlayMovie ||
            b.state == net::BattleStateId::FaceOff || b.state == net::BattleStateId::WaitForJoin);
}

bool Battle::requestJoin(net::Client& client, uint32_t battleId, float toonX, float toonY, float toonZ, float toonH,
                         uint32_t nowMs) {
    if (phase_ != BattlePhase::Idle || int32_t(nowMs - joinCooldownMs_) < 0 || !client.inZone()) return false;
    const net::Battle* b = client.findBattle(battleId);
    if (!b || b->doId == doneBattle_ || !joinable(*b)) return false;
    // Position relative to the battle node (setPosition + headsUp(initialSuitPos)).
    const float dx = toonX - b->x, dy = toonY - b->y;
    const float ddx = b->sx - b->x, ddy = b->sy - b->y;
    const float h = (ddx * ddx + ddy * ddy > 1e-4f) ? atan2f(-ddx, ddy) : 0.0f;
    const float c = cosf(h), s = sinf(h);
    const float lx = dx * c + dy * s, ly = -dx * s + dy * c;
    if (!client.sendBattleRequestJoin(b->doId, lx, ly, toonZ - b->z)) return false;
    phase_ = BattlePhase::Requested;
    requestMs_ = nowMs;
    reqX_ = toonX, reqY_ = toonY, reqZ_ = toonZ, reqH_ = toonH;
    joining_ = true;
    return true;
}

void Battle::checkJoin(net::Client& client, float toonX, float toonY, float toonZ, float toonH, uint32_t nowMs) {
    if (phase_ != BattlePhase::Idle || int32_t(nowMs - joinCooldownMs_) < 0 || !client.inZone()) return;
    for (int i = 0; i < net::kMaxBattles; ++i) {
        const net::Battle& b = client.battleSlot(i);
        if (!b.active || b.doId == doneBattle_ || !joinable(b)) continue;
        const float dx = toonX - b.x, dy = toonY - b.y;
        if (dx * dx + dy * dy > kJoinRadius * kJoinRadius) continue;
        requestJoin(client, b.doId, toonX, toonY, toonZ, toonH, nowMs);
        return;
    }
}

void Battle::handleEvent(const net::Event& ev, net::Client& client, uint32_t nowMs) {
    (void)client;
    using net::EventType;
    switch (ev.type) {
    case EventType::SuitDenied:
        // DistributedSuitBase.denyBattle: back to walking.
        if (phase_ == BattlePhase::Requested && !joining_) {
            phase_ = BattlePhase::Idle;
            cooldownUntilMs_ = nowMs + kRequestCooldownMs;
            message_ = "That cog is too busy to fight right now.";
        }
        break;
    case EventType::BattleJoinDenied:
        if (phase_ == BattlePhase::Requested && joining_) {
            phase_ = BattlePhase::Idle;
            joinCooldownMs_ = nowMs + 5000;
            message_ = "That battle is full.";
        }
        break;
    default:
        break;
    }
}

const char* Battle::popMessage() {
    const char* m = message_;
    message_ = nullptr;
    return m;
}

bool Battle::popWentSad() {
    const bool s = wentSad_;
    wentSad_ = false;
    return s;
}

// ============================================================ actors
Battle::Actor* Battle::findActor(uint32_t doId) {
    for (Actor& a : suits_) {
        if (a.used && a.doId == doId) return &a;
    }
    for (Actor& a : toons_) {
        if (a.used && a.doId == doId) return &a;
    }
    return nullptr;
}

int Battle::memberIndex(const uint32_t* list, int n, uint32_t doId) const {
    for (int i = 0; i < n; ++i) {
        if (list[i] == doId) return i;
    }
    return -1;
}

void Battle::goTo(Actor& a, float tx, float ty, float th, float speed, uint32_t startMs, float durOverride) {
    a.fx = a.x, a.fy = a.y, a.fh = a.h;
    a.tx = tx, a.ty = ty, a.th = th;
    const float dx = tx - a.x, dy = ty - a.y;
    const float dist = sqrtf(dx * dx + dy * dy);
    a.moveDur = durOverride > 0.0f ? durOverride : (speed > 0.0f ? dist / speed : 0.0f);
    a.moveStartMs = startMs;
    a.hasTarget = true;
}

void Battle::begin(net::Client& client, const net::Battle& nb, const CogPoseSource& cogs, uint32_t nowMs) {
    (void)cogs;
    battleId_ = nb.doId;
    localId_ = client.avatarId();
    bx_ = nb.x, by_ = nb.y, bz_ = nb.z;
    const float dx = nb.sx - nb.x, dy = nb.sy - nb.y;
    bh_ = (dx * dx + dy * dy > 1e-4f) ? atan2f(-dx, dy) / kDegToRad : 0.0f;
    if (phase_ != BattlePhase::Requested) {
        float x = bx_, y = by_ - 12.0f, z = bz_;
        client.avatarPos(localId_, &x, &y, &z);
        reqX_ = x, reqY_ = y, reqZ_ = z, reqH_ = bh_;
    }
    for (Actor& a : suits_) a = Actor();
    for (Actor& a : toons_) a = Actor();
    numPopups_ = 0;
    seenMembers_ = seenState_ = seenAdjust_ = seenMovie_ = 0;
    state_ = net::BattleStateId::None;
    chose_ = timeoutSent_ = running_ = false;
    faceOffSent_ = false;
    adjustPending_ = false;
    rewardSent_ = false;
    movieSent_ = false;
    moviePlaying_ = false;
    tauntShown_ = false;
    numFx_ = fxCursor_ = 0;
    phase_ = BattlePhase::Waiting;
    TT_LOG("battle: joined %u at %.1f,%.1f heading %.0f (%d cogs, %d toons)", unsigned(battleId_), double(bx_),
           double(by_), double(bh_), int(nb.numSuits), int(nb.numToons));
    (void)nowMs;
}

void Battle::leave(net::Client& client, bool sad) {
    TT_LOG("battle: left %u%s", unsigned(battleId_), sad ? " (sad)" : "");
    doneBattle_ = battleId_;
    battleId_ = 0;
    phase_ = BattlePhase::Idle;
    cooldownUntilMs_ = nowMs_ + 1500;
    joinCooldownMs_ = nowMs_ + 2500;
    for (Actor& a : suits_) a = Actor();
    for (Actor& a : toons_) a = Actor();
    numSuitPoses_ = numToonPoses_ = 0;
    numPopups_ = 0;
    numFx_ = fxCursor_ = 0;
    moviePlaying_ = false;
    wentSad_ = sad;
    (void)client;
}

void Battle::syncMembers(net::Client& client, const net::Battle& nb, const CogPoseSource& cogs, uint32_t nowMs) {
    Actor newSuits[net::kBattleSuits];
    Actor newToons[net::kBattleToons];
    for (int i = 0; i < nb.numSuits; ++i) {
        const uint32_t id = nb.suits[i];
        const int old = memberIndex_(suits_, id);
        if (old >= 0) {
            newSuits[i] = suits_[old];
            continue;
        }
        Actor& a = newSuits[i];
        a.used = true;
        a.isToon = false;
        a.doId = id;
        float x = bx_, y = by_, h = 0.0f, z = bz_;
        bool placed = false;
        if (i == 0 && nb.state == net::BattleStateId::FaceOff) {
            x = nb.sx, y = nb.sy; // the cog's pose at contact
            placed = true;
        } else if (cogs.pose && cogs.pose(cogs.user, id, &x, &y, &z, &h)) {
            placed = true;
        }
        if (!placed) toWorld(0.0f, 14.0f, &x, &y);
        a.x = x, a.y = y, a.z = bz_;
        a.h = headingTo(x, y, bx_, by_);
        net::Client::CogIdentity ns;
        const SuitInfo* info = client.cogIdentity(id, &ns) && ns.hasDna ? findSuitType(ns.name) : nullptr;
        a.height = info ? info->height : 5.0f;
        // [cog attacks]
        if (ns.hasDna && ns.name[0]) {
            snprintf(a.cogCode, sizeof(a.cogCode), "%s", ns.name);
        }
    }
    for (int i = 0; i < nb.numToons; ++i) {
        const uint32_t id = nb.toons[i];
        const int old = memberIndex_(toons_, id);
        if (old >= 0) {
            newToons[i] = toons_[old];
            continue;
        }
        Actor& a = newToons[i];
        a.used = true;
        a.isToon = true;
        a.doId = id;
        float x = bx_, y = by_ - 12.0f, h = 0.0f;
        if (id == localId_) {
            x = reqX_, y = reqY_, h = reqH_;
        } else if (const net::RemoteToon* t = client.findToon(id)) {
            x = t->x, y = t->y, h = t->h;
        } else {
            toWorld(0.0f, -14.0f, &x, &y);
        }
        a.x = x, a.y = y, a.z = bz_, a.h = h;
        a.height = kToonHeight;
    }
    for (int i = 0; i < net::kBattleSuits; ++i) suits_[i] = i < nb.numSuits ? newSuits[i] : Actor();
    for (int i = 0; i < net::kBattleToons; ++i) toons_[i] = i < nb.numToons ? newToons[i] : Actor();

    // [cog attacks] Preload Cog attack props for active Cogs
#ifdef __3DS__
    if (battleFx_) {
        const char* codes[net::kBattleSuits];
        int count = 0;
        for (int i = 0; i < nb.numSuits; ++i) {
            if (suits_[i].used && suits_[i].cogCode[0]) {
                codes[count++] = suits_[i].cogCode;
            }
        }
        battleFx_->cogAttacks().preloadForBattle(codes, count);
    }
#endif

    // Everyone with the local toon in the fight answers joinDone for each joiner (cog or toon).
    for (int i = 0; i < nb.numSuits; ++i) {
        Actor& a = suits_[i];
        const bool joiningNow = (nb.suitsJoining >> i) & 1;
        if (joiningNow && !a.joinSent) a.joinPending = true;
        if (!joiningNow) a.joinPending = false;
    }
    for (int i = 0; i < nb.numToons; ++i) {
        Actor& a = toons_[i];
        const bool joiningNow = (nb.toonsJoining >> i) & 1;
        if (joiningNow && !a.joinSent) a.joinPending = true;
        if (!joiningNow) a.joinPending = false;
    }
    (void)nowMs;
}

// Index of the actor with this doId in a four-slot array, or -1.
int Battle::memberIndex_(const Actor* arr, uint32_t doId) {
    for (int i = 0; i < 4; ++i) {
        if (arr[i].used && arr[i].doId == doId) return i;
    }
    return -1;
}

void Battle::retarget(const net::Battle& nb, uint32_t nowMs) {
    const bool faceOff = state_ == net::BattleStateId::FaceOff;
    for (int i = 0; i < nb.numSuits; ++i) {
        Actor& a = suits_[i];
        if (!a.used) continue;
        Slot slot;
        const unsigned active = nb.suitsActive;
        bool timed = false;
        if (faceOff && i == 0) {
            slot = kSuitPoints[0][0];
            timed = true;
        } else if ((active >> i) & 1) {
            const int n = popcount8(active), r = rankOf(active, i);
            slot = kSuitPoints[(n > 4 ? 4 : n) - 1][r > 3 ? 3 : r];
        } else {
            const unsigned waiting = ~active & ((1u << nb.numSuits) - 1u);
            const int r = rankOf(waiting, i);
            slot = kSuitPending[r > 3 ? 3 : r];
        }
        float wx, wy;
        toWorld(slot.x, slot.y, &wx, &wy);
        const float wh = bh_ + slot.h;
        if (a.hasTarget && fabsf(a.tx - wx) < 0.05f && fabsf(a.ty - wy) < 0.05f) continue;
        if (timed) {
            // FaceOff: the cog taunts for 3.5 s facing the toon, then walks to its slot over the time it
            // takes it to cover calcFaceoffTime (BattleBase.py:252).
            const float cdx = bx_ - a.x, cdy = by_ - a.y;
            const float toCenter = sqrtf(cdx * cdx + cdy * cdy);
            float dur = fabsf(toCenter - 6.0f) / kSuitSpeed;
            if (dur < 1e-3f) dur = 1e-3f;
            goTo(a, wx, wy, wh, kSuitSpeed, faceOffStartMs_ + kFaceOffTauntMs, dur);
            faceOffEndMs_ = faceOffStartMs_ + kFaceOffTauntMs + uint32_t(dur * 1000.0f);
            if (toons_[0].used) { // suit.headsUp(toon) while it taunts
                a.faceH = headingTo(a.x, a.y, toons_[0].x, toons_[0].y);
                a.faceUntil = a.moveStartMs;
            }
        } else {
            goTo(a, wx, wy, wh, kSuitSpeed, nowMs, 0.0f);
        }
        if (a.joinPending) a.joinAtMs = a.moveStartMs + uint32_t(a.moveDur * 1000.0f) + 200u;
    }
    for (int i = 0; i < nb.numToons; ++i) {
        Actor& a = toons_[i];
        if (!a.used) continue;
        Slot slot;
        const unsigned active = nb.toonsActive;
        bool timed = false, runAway = false;
        if ((nb.toonsRunning >> i) & 1) {
            // __makeToonRun: leave the fight at a run, the AI removes us after TOON_RUN_T.
            slot = kToonPoints[0][0];
            slot.y -= kRunAwayDistance;
            slot.h = 180.0f;
            runAway = true;
        } else if (faceOff && i == 0) {
            slot = kToonPoints[0][0];
            timed = true;
        } else if ((active >> i) & 1) {
            const int n = popcount8(active), r = rankOf(active, i);
            slot = kToonPoints[(n > 4 ? 4 : n) - 1][r > 3 ? 3 : r];
        } else {
            const unsigned waiting = ~active & ~unsigned(nb.toonsRunning) & ((1u << nb.numToons) - 1u);
            const int r = rankOf(waiting, i);
            slot = kToonPending[r > 3 ? 3 : r];
        }
        float wx, wy;
        toWorld(slot.x, slot.y, &wx, &wy);
        const float wh = bh_ + slot.h;
        if (a.hasTarget && fabsf(a.tx - wx) < 0.05f && fabsf(a.ty - wy) < 0.05f) continue;
        if (timed) {
            // The toon runs to its slot in the same time as the cog walks to its own.
            const Actor& s = suits_[0];
            const float cdx = bx_ - s.x, cdy = by_ - s.y;
            float dur = fabsf(sqrtf(cdx * cdx + cdy * cdy) - 6.0f) / kSuitSpeed;
            if (dur < 1e-3f) dur = 1e-3f;
            goTo(a, wx, wy, wh, kToonSpeed, faceOffStartMs_ + kFaceOffTauntMs, dur);
            if (!s.used) faceOffEndMs_ = faceOffStartMs_ + kFaceOffTauntMs + uint32_t(dur * 1000.0f);
            else { // toon.headsUp(suit)
                a.faceH = headingTo(a.x, a.y, s.x, s.y);
                a.faceUntil = a.moveStartMs;
            }
        } else if (runAway) {
            goTo(a, wx, wy, wh, kToonSpeed, nowMs, kRunAwaySec);
        } else {
            goTo(a, wx, wy, wh, kToonSpeed, nowMs, 0.0f);
        }
        if (a.joinPending) a.joinAtMs = a.moveStartMs + uint32_t(a.moveDur * 1000.0f) + 200u;
    }
}

// ============================================================ state machine
void Battle::onState(net::Client& client, const net::Battle& nb, uint32_t nowMs) {
    state_ = nb.state;
    bool localActive = false, localRunning = false;
    const int li = memberIndex(nb.toons, nb.numToons, localId_);
    if (li >= 0) {
        localActive = (nb.toonsActive >> li) & 1;
        localRunning = (nb.toonsRunning >> li) & 1;
    }
    TT_LOG("battle: state %s (active %d)", nb.stateName, int(localActive));
    switch (nb.state) {
    case net::BattleStateId::FaceOff:
        phase_ = BattlePhase::FaceOff;
        faceOffStartMs_ = nowMs;
        faceOffSent_ = false;
        camera_.startFaceOff(nb.numSuits > 0 ? nb.suits[0] : 0, nowMs, 2500);
        // The cog and the first toon are placed by retarget() (it needs the state set above).
        for (Actor& a : suits_) a.hasTarget = false;
        for (Actor& a : toons_) a.hasTarget = false;
        break;
    case net::BattleStateId::WaitForInput:
        chose_ = timeoutSent_ = false;
        moviePlaying_ = false;
        camera_.startWaitForInput();
        if (localRunning) {
            phase_ = BattlePhase::Waiting;
        } else if (localActive) {
            phase_ = BattlePhase::Input;
            inputEndMs_ = nb.stateMs + kInputTimeoutMs;
            autoAtMs_ = nowMs + 700;
        } else {
            phase_ = BattlePhase::Waiting; // pending: joined mid-round, active from the next one
        }
        break;
    case net::BattleStateId::MakeMovie:
        phase_ = BattlePhase::Chosen;
        break;
    case net::BattleStateId::PlayMovie:
        phase_ = BattlePhase::Movie;
        startMovie(client, nb, nowMs);
        break;
    case net::BattleStateId::Reward:
        phase_ = BattlePhase::Reward;
        rewardStartMs_ = nowMs;
        rewardSent_ = false;
        moviePlaying_ = false;
        camera_.startReward(localId_, nowMs, 4000);
        break;
    case net::BattleStateId::Resume:
        leave(client, client.ownStats().hp <= 0);
        break;
    default:
        if (phase_ != BattlePhase::Reward) phase_ = BattlePhase::Waiting;
        break;
    }
}

void Battle::startMovie(net::Client& client, const net::Battle& nb, uint32_t nowMs) {
    (void)client;
    movieSent_ = false;
    numFx_ = fxCursor_ = 0;
    movieStartMs_ = nowMs;
    if (!nb.movie.active) {
        // Nothing was chosen by anyone (everyone timed out or passed): nothing to play.
        moviePlaying_ = true;
        movieEndMs_ = nowMs + 400;
        return;
    }
    buildMovie(nb.movie);
    camera_.buildMovie(battleId_, ++round_, *this, nb.movie, movieStartMs_);
    moviePlaying_ = true;
}

const char* const kBattleClipNames[kNumBattleClips] = {
    "throw", "pushbutton", "cringe", "sidestep-left", "victory", "lose",
    // [cog attacks]
    "conked", "sidestep-right", "duck", "slip-forward", "slip-backward"
};

void Battle::addFx(uint32_t at, uint8_t kind, uint32_t who, uint32_t other, int value, int track, int level) {
    if (numFx_ >= kMaxFx) return;
    fx_[numFx_++] = Fx{at, kind, who, other, int16_t(value), int8_t(track), int8_t(level)};
}

// Plays the round in the order Movie.py does (toon tracks in track order, levels ascending, then
// the cogs).
void Battle::buildMovie(const net::BattleMovie& m) {
    uint32_t t = 300;
    int order[4];
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        const net::BattleToonAttack& a = m.toonAttacks[i];
        if (a.track < 0 || a.track > 6 || a.toonIdx < 0 || a.toonIdx >= m.numToons) continue;
        int pos = n++;
        while (pos > 0) {
            const net::BattleToonAttack& p = m.toonAttacks[order[pos - 1]];
            if (p.track < a.track || (p.track == a.track && p.level <= a.level)) break;
            order[pos] = order[pos - 1];
            --pos;
        }
        order[pos] = i;
    }
    for (int k = 0; k < n; ++k) {
        const net::BattleToonAttack& a = m.toonAttacks[order[k]];
        const uint32_t who = m.toons[a.toonIdx];
        uint32_t target = 0;
        if (a.target >= 0) {
            if (a.track == kTrackHeal && a.target < m.numToons) target = m.toons[a.target];
            else if (a.track != kTrackHeal && a.target < m.numSuits) target = m.suits[a.target];
        }

        uint32_t attackDur = 2200;
        uint32_t hitAt = t + 1500;
        if (a.track == kTrackHeal)        { attackDur = 3200; hitAt = t + 2000; }
        else if (a.track == kTrackTrap)   { attackDur = 3500; hitAt = t + 2500; }
        else if (a.track == kTrackLure)   { attackDur = 4500; hitAt = t + 3000; }
        else if (a.track == kTrackSound)  { attackDur = 3500; hitAt = t + 2500; }
        else if (a.track == kTrackThrow)  { attackDur = 3500; hitAt = t + 3000; }
        else if (a.track == kTrackSquirt) { attackDur = 3300; hitAt = t + 2700; }
        else if (a.track == kTrackDrop)   { attackDur = 4500; hitAt = t + 3800; }

        addFx(t, kFxAttack, who, target, attackDur, a.track, a.level);
        bool any = false;
        if (a.track == kTrackHeal) {
            for (int j = 0; j < a.numHp && j < m.numToons; ++j) {
                if (a.hp[j] > 0) {
                    addFx(hitAt, kFxHeal, m.toons[j], who, a.hp[j], a.track, a.level);
                    any = true;
                }
            }
        } else {
            for (int j = 0; j < a.numHp && j < m.numSuits; ++j) {
                if (a.hp[j] > 0) {
                    addFx(hitAt, kFxHit, m.suits[j], who, a.hp[j], a.track, a.level);
                    any = true;
                }
            }
            if (!any && a.target >= 0 && a.target < m.numSuits) addFx(hitAt, kFxMiss, m.suits[a.target], who, 0, a.track, a.level);
        }
        uint32_t dur = attackDur;
        for (int j = 0; j < m.numSuits; ++j) {
            if ((a.died >> j) & 1) {
                addFx(hitAt + 500, kFxDie, m.suits[j], 0, 0);
                dur = (hitAt - t) + 500 + uint32_t((kLoseSec + kDieSec) * 1000.0f);
            }
        }
        t += dur;
    }
    for (int i = 0; i < 4; ++i) {
        const net::BattleSuitAttack& s = m.suitAttacks[i];
        if (s.suitIdx < 0 || s.suitIdx >= m.numSuits || s.attack < 0) continue;
        const uint32_t who = m.suits[s.suitIdx];
        const uint32_t target = (s.target >= 0 && s.target < m.numToons) ? m.toons[s.target] : 0;

        // [cog attacks] Exact duration and timing from Cog attack tables
        uint32_t attackDur = 2000;
        uint32_t hitAt = t + 1200;
        uint32_t missAt = t + 1000;
        const char* code = actorCogCode(who);
        const int atkId = getCogAttackId(code, s.attack);
        const CogAttackInfo* info = getCogAttackInfo(atkId);
        if (info) {
            const SuitInfo* sInfo = findSuitType(code);
            const char body = sInfo ? sInfo->body : 'a';
            const int bodyIdx = body == 'b' ? 1 : (body == 'c' ? 2 : 0);
            attackDur = info->durationMs[bodyIdx];
            hitAt = t + (info->hitDelayMs > 0 ? info->hitDelayMs : 1200);
            missAt = t + (info->dodgeDelayMs > 0 ? info->dodgeDelayMs : 1000);
        }

        addFx(t, kFxAttack, who, target, attackDur, -1, s.attack);
        bool any = false;
        for (int j = 0; j < s.numHp && j < m.numToons; ++j) {
            if ((s.target < 0 || s.target == j) && s.hp[j] > 0) {
                addFx(hitAt, kFxHit, m.toons[j], who, s.hp[j], -1, s.attack);
                any = true;
            }
        }
        if (!any && s.target >= 0 && s.target < m.numToons) addFx(missAt, kFxMiss, m.toons[s.target], who, 0, -1, s.attack);
        t += attackDur;
    }
    movieEndMs_ = movieStartMs_ + t + 300;
    TT_LOG("battle: movie: %d effects, %u ms", numFx_, unsigned(t + 300));
}

void Battle::addPopup(const Actor& a, const char* text, uint32_t color, uint32_t nowMs, uint32_t lifeMs, float rise) {
    int slot = -1;
    if (numPopups_ < kMaxPopups) {
        slot = numPopups_++;
    } else {
        slot = 0; // replace the oldest
        for (int i = 1; i < numPopups_; ++i) {
            if (popupStartMs_[i] < popupStartMs_[slot]) slot = i;
        }
    }
    BattlePopup& p = popups_[slot];
    p.x = a.x, p.y = a.y, p.z = a.z + a.height + 0.8f;
    p.age = 0.0f;
    p.rise = rise;
    snprintf(p.text, sizeof(p.text), "%s", text);
    p.color = color;
    popupStartMs_[slot] = nowMs;
    popupLifeMs_[slot] = lifeMs;
}

void Battle::runFx(uint32_t nowMs) {
    while (fxCursor_ < numFx_ && int32_t(nowMs - (movieStartMs_ + fx_[fxCursor_].at)) >= 0) {
        const Fx& f = fx_[fxCursor_++];
        Actor* a = findActor(f.who);
        if (!a) continue;
        char text[16];
        switch (f.kind) {
        case kFxAttack: {
            a->attackStart = nowMs;
            a->attackEnd = nowMs + uint32_t(f.value);
            // [cog attacks] Gag track (>=0) or suit attack index (0..5)
            a->attackTrack = f.track >= 0 ? f.track : f.level;
            a->clipStartMs = nowMs;
            if (const Actor* t = f.other ? findActor(f.other) : nullptr) {
                a->faceH = headingTo(a->x, a->y, t->x, t->y);
                a->faceUntil = a->attackEnd + 300;
            }
            if (battleFx_) {
                uint32_t hitMs = nowMs + 1500;
                if (f.track < 0) {
                    // [cog attacks]
                    const char* code = a->cogCode;
                    const int atkId = getCogAttackId(code, f.level);
                    const CogAttackInfo* info = getCogAttackInfo(atkId);
                    hitMs = nowMs + (info && info->hitDelayMs > 0 ? info->hitDelayMs : 1200);
                } else if (f.track == kTrackHeal) hitMs = nowMs + 2000;
                else if (f.track == kTrackTrap) hitMs = nowMs + 2500;
                else if (f.track == kTrackLure) hitMs = nowMs + 3000;
                else if (f.track == kTrackSound) hitMs = nowMs + 2500;
                else if (f.track == kTrackThrow) hitMs = nowMs + 3000;
                else if (f.track == kTrackSquirt) hitMs = nowMs + 2700;
                else if (f.track == kTrackDrop) hitMs = nowMs + 3800;
                battleFx_->onAttack(f.who, f.other, f.track, f.level, nowMs, f.value, hitMs, this);
            }
            break;
        }
        case kFxHit:
            if (!a->isToon) a->damage += f.value;
            // [cog attacks] Reaction clip for toons
            if (a->isToon) {
                const Actor* by = f.other ? findActor(f.other) : nullptr;
                const char* code = by ? by->cogCode : "";
                const int atkId = getCogAttackId(code, f.level);
                const CogAttackInfo* info = getCogAttackInfo(atkId);
                if (info) {
                    if (!strcmp(info->toonHitAnim, "conked")) a->reactClip = kClipConked;
                    else if (!strcmp(info->toonHitAnim, "slip-forward")) a->reactClip = kClipSlipForward;
                    else if (!strcmp(info->toonHitAnim, "slip-backward")) a->reactClip = kClipSlipBackward;
                    else if (!strcmp(info->toonHitAnim, "duck")) a->reactClip = kClipDuck;
                    else a->reactClip = kClipCringe;
                } else {
                    a->reactClip = kClipCringe;
                }
            }
            a->reactUntil = nowMs + 1500, a->reactHit = true; // toons cringe; cogs play the gag's react
            a->clipStartMs = nowMs;
            if (!a->isToon) {
                const Actor* by = f.other ? findActor(f.other) : nullptr;
                a->reactTrack = by ? by->attackTrack : -1;
            }
            a->flashEnd = nowMs + uint32_t(kFlashSec * 1000.0f);
            snprintf(text, sizeof(text), "-%d", int(f.value));
            addPopup(*a, text, kColorDamage, nowMs);
            if (battleFx_) battleFx_->onHit(f.who, f.other, f.track, f.level, f.value, false, this);
            break;
        case kFxHeal:
            snprintf(text, sizeof(text), "+%d", int(f.value));
            addPopup(*a, text, kColorHeal, nowMs);
            if (battleFx_) battleFx_->onHeal(f.who, f.other, f.value);
            break;
        case kFxMiss:
            addPopup(*a, "Miss", kColorMiss, nowMs);
            // [cog attacks] Reaction clip for toons
            if (a->isToon) {
                const Actor* by = f.other ? findActor(f.other) : nullptr;
                const char* code = by ? by->cogCode : "";
                const int atkId = getCogAttackId(code, f.level);
                const CogAttackInfo* info = getCogAttackInfo(atkId);
                if (info) {
                    if (!strcmp(info->toonMissAnim, "sidestep-right")) a->reactClip = kClipSidestepRight;
                    else if (!strcmp(info->toonMissAnim, "duck")) a->reactClip = kClipDuck;
                    else a->reactClip = kClipSidestep;
                } else {
                    a->reactClip = kClipSidestep;
                }
            }
            a->reactUntil = nowMs + 1200, a->reactHit = false; // dodge: sidestep
            a->clipStartMs = nowMs;
            if (battleFx_) battleFx_->onHit(f.who, f.other, f.track, f.level, 0, true, this);
            break;
        case kFxDie:
            a->dying = true;
            a->dieStart = nowMs;
            a->flashEnd = 0;
            if (battleFx_) battleFx_->onDie(f.who, nowMs);
            break;
        default: break;
        }
    }
}

// ============================================================ per frame
void Battle::autoPick(net::Client& client, const net::Battle& nb) {
    const net::OwnStats& st = client.ownStats();
    // TownBattle.checkHealTrapLure: a lone toon cannot toon-up (toons never heal themselves).
    uint32_t healTarget = 0;
    for (int i = 0; i < nb.numToons && !healTarget; ++i) {
        if (nb.toons[i] != localId_) healTarget = nb.toons[i];
    }
    auto usable = [&](int track, int level) {
        if (track == kTrackHeal && !healTarget) return false;
        return st.trackAccess[track] && level < gags::levelsUnlocked(track, st.experience[track]) &&
               st.inventory[track][level] > 0;
    };
    BattleAction act;
    act.kind = BattleAction::Pass;
    if (forcedTrack_ >= 0 && forcedTrack_ <= 6) {
        for (int level = 6; level >= 0 && act.kind == BattleAction::Pass; --level) {
            if (!usable(forcedTrack_, level)) continue;
            act.kind = BattleAction::Attack;
            act.track = forcedTrack_;
            act.level = level;
            if (forcedTrack_ == kTrackHeal) {
                act.target = healTarget;
            } else {
                for (int i = 0; i < nb.numSuits; ++i) {
                    if ((nb.suitsActive >> i) & 1) {
                        act.target = nb.suits[i];
                        break;
                    }
                }
                if (act.target == 0 && !attackAffectsGroup(forcedTrack_, level)) act.kind = BattleAction::Pass;
            }
        }
    }

    // Throw, squirt, sound, drop, then toon-up; against several cogs sound first (it hits them
    // all). Trap and lure do no damage on their own, so autoplay only uses them when forced.
    static const int kTrackOrder[5] = {4, 5, 3, 6, 0};
    static const int kGroupOrder[5] = {3, 4, 5, 6, 0};
    int active = 0;
    for (int i = 0; i < nb.numSuits; ++i) active += (nb.suitsActive >> i) & 1;
    const int* order = active > 1 ? kGroupOrder : kTrackOrder;
    for (int k = 0; k < 5 && act.kind == BattleAction::Pass; ++k) {
        const int track = order[k];
        for (int level = 6; level >= 0 && act.kind == BattleAction::Pass; --level) { // strongest first
            if (!usable(track, level)) continue;
            act.kind = BattleAction::Attack;
            act.track = track;
            act.level = level;
            if (track == kTrackHeal) {
                act.target = healTarget;
            } else {
                for (int i = 0; i < nb.numSuits; ++i) {
                    if ((nb.suitsActive >> i) & 1) {
                        act.target = nb.suits[i];
                        break;
                    }
                }
                if (act.target == 0 && !attackAffectsGroup(track, level)) act.kind = BattleAction::Pass;
            }
        }
    }
    perform(client, act, nowMs_);
}

void Battle::perform(net::Client& client, const BattleAction& a, uint32_t nowMs) {
    const uint32_t id = battleId_;
    if (id == 0) return;
    switch (a.kind) {
    case BattleAction::Attack:
        if (phase_ != BattlePhase::Input || chose_) return;
        if (client.sendBattleRequestAttack(id, int8_t(a.track), int8_t(a.level),
                                           attackAffectsGroup(a.track, a.level) ? -1 : int32_t(a.target))) {
            chose_ = true;
            phase_ = BattlePhase::Chosen;
        }
        break;
    case BattleAction::Pass:
        if (phase_ != BattlePhase::Input || chose_) return;
        if (client.sendBattleRequestAttack(id, int8_t(kAttackPass), -1, -1)) {
            chose_ = true;
            phase_ = BattlePhase::Chosen;
        }
        break;
    case BattleAction::Run:
        if (phase_ != BattlePhase::Input || chose_) return;
        if (client.sendBattleRequestRun(id)) {
            chose_ = true;
            running_ = true;
            phase_ = BattlePhase::Chosen;
        }
        break;
    case BattleAction::Continue:
        if (phase_ != BattlePhase::Reward || rewardSent_) return;
        if (client.sendBattleRewardDone(id)) rewardSent_ = true;
        break;
    default:
        break;
    }
    (void)nowMs;
}

void Battle::update(net::Client& client, uint32_t nowMs, float dt, const CogPoseSource& cogs) {
    (void)dt;
    nowMs_ = nowMs;
    const net::Battle* nb = client.localBattle();
    if (nb && nb->doId == doneBattle_) nb = nullptr;
    if (doneBattle_ != 0 && !client.findBattle(doneBattle_)) doneBattle_ = 0;
    if (!nb) {
        if (phase_ == BattlePhase::Requested) {
            if (nowMs - requestMs_ > kRequestTimeoutMs) {
                TT_LOG("battle: the AI never answered the request");
                phase_ = BattlePhase::Idle;
                cooldownUntilMs_ = nowMs + kRequestCooldownMs;
                joinCooldownMs_ = nowMs + kRequestCooldownMs;
            }
        } else if (phase_ != BattlePhase::Idle) {
            // Our membership ended without a Resume we saw: we ran, were defeated or the object went.
            leave(client, client.ownStats().hp <= 0);
        }
        return;
    }
    if (battleId_ != nb->doId) begin(client, *nb, cogs, nowMs);

    if (nb->membersSeq != seenMembers_) {
        seenMembers_ = nb->membersSeq;
        syncMembers(client, *nb, cogs, nowMs);
        // Pending -> active in the middle of WaitForInput (the AI's adjust finished): choose now.
        const int li = memberIndex(nb->toons, nb->numToons, localId_);
        if (li >= 0 && state_ == net::BattleStateId::WaitForInput) {
            const bool active = (nb->toonsActive >> li) & 1, runningNow = (nb->toonsRunning >> li) & 1;
            if (active && !runningNow && phase_ == BattlePhase::Waiting) {
                phase_ = BattlePhase::Input;
                chose_ = timeoutSent_ = false;
                inputEndMs_ = nowMs + kInputTimeoutMs; // startTimer() with no elapsed time
                autoAtMs_ = nowMs + 700;
            } else if (runningNow && phase_ == BattlePhase::Input) {
                phase_ = BattlePhase::Waiting;
            }
        }
        if (li >= 0 && ((nb->toonsRunning >> li) & 1)) running_ = true;
    }
    if (nb->stateSeq != seenState_) {
        seenState_ = nb->stateSeq;
        onState(client, *nb, nowMs);
        if (phase_ == BattlePhase::Idle) return; // Resume
    }
    retarget(*nb, nowMs);
    if (nb->adjustSeq != seenAdjust_) {
        seenAdjust_ = nb->adjustSeq;
        // adjust(): the members move to their new slots (retarget did it); answer when they arrive.
        uint32_t end = nowMs + 300;
        for (const Actor& a : suits_) {
            if (a.used && a.hasTarget) {
                const uint32_t e = a.moveStartMs + uint32_t(a.moveDur * 1000.0f) + 100u;
                if (int32_t(e - end) > 0) end = e;
            }
        }
        for (const Actor& a : toons_) {
            if (a.used && a.hasTarget) {
                const uint32_t e = a.moveStartMs + uint32_t(a.moveDur * 1000.0f) + 100u;
                if (int32_t(e - end) > 0) end = e;
            }
        }
        adjustDoneAtMs_ = end;
        adjustPending_ = true;
    }

    // FaceOff: the first toon reports when the walk-in is over.
    if (phase_ == BattlePhase::FaceOff) {
        if (!tauntShown_ && suits_[0].used) {
            addPopup(suits_[0], "Hold it, Toon!", kColorTaunt, nowMs, kFaceOffTauntMs - 300, 0.0f);
            tauntShown_ = true;
        }
        if (!faceOffSent_ && nb->numToons > 0 && nb->toons[0] == localId_ && int32_t(nowMs - faceOffEndMs_) >= 0) {
            if (client.sendBattleFaceOffDone(battleId_)) faceOffSent_ = true;
        }
    }
    // WaitForInput: the 20 s countdown; the AI is waiting for requestAttack / toonRequestRun / timeout.
    if (phase_ == BattlePhase::Input) {
        if (autoPlay_ && int32_t(nowMs - autoAtMs_) >= 0) {
            autoPick(client, *nb);
        } else if (int32_t(nowMs - inputEndMs_) >= 0 && !timeoutSent_) {
            timeoutSent_ = true;
            client.sendBattleTimeout(battleId_);
            chose_ = true;
            phase_ = BattlePhase::Chosen;
        }
    }
    if (adjustPending_ && int32_t(nowMs - adjustDoneAtMs_) >= 0) {
        client.sendBattleAdjustDone(battleId_);
        adjustPending_ = false;
    }
    // Everyone in the fight acknowledges each joiner's walk (suits and toons).
    for (Actor& a : suits_) {
        if (a.used && a.joinPending && !a.joinSent && int32_t(nowMs - a.joinAtMs) >= 0) {
            client.sendBattleJoinDone(battleId_, a.doId);
            a.joinSent = true;
            a.joinPending = false;
        }
    }
    for (Actor& a : toons_) {
        if (a.used && a.joinPending && !a.joinSent && int32_t(nowMs - a.joinAtMs) >= 0) {
            client.sendBattleJoinDone(battleId_, a.doId);
            a.joinSent = true;
            a.joinPending = false;
        }
    }
    if (moviePlaying_) {
        for (Actor& a : suits_) { // a fresh setHP replaces what our movies took off
            net::Client::CogIdentity s;
            if (a.used && client.cogIdentity(a.doId, &s) && s.hp != a.baseHp) {
                a.baseHp = s.hp;
                a.damage = 0;
            }
        }
        runFx(nowMs);
        if (!movieSent_ && int32_t(nowMs - movieEndMs_) >= 0) {
            client.sendBattleMovieDone(battleId_);
            movieSent_ = true;
        }
    }
    if (phase_ == BattlePhase::Reward && !rewardSent_) {
        const uint32_t wait = autoPlay_ ? 2500u : kRewardAutoMs;
        if (nowMs - rewardStartMs_ >= wait) {
            if (client.sendBattleRewardDone(battleId_)) rewardSent_ = true;
        }
    }
    computePoses(nowMs);
}

void Battle::computePoses(uint32_t nowMs) {
    auto evaluate = [&](Actor& a, BattleActorPose* out, bool isToon) {
        float u = 1.0f;
        if (a.hasTarget) {
            const float elapsed = float(int32_t(nowMs - a.moveStartMs)) / 1000.0f;
            u = a.moveDur > 1e-4f ? clamp01(elapsed / a.moveDur) : (elapsed >= 0.0f ? 1.0f : 0.0f);
            a.x = a.fx + (a.tx - a.fx) * u;
            a.y = a.fy + (a.ty - a.fy) * u;
            const float dx = a.tx - a.fx, dy = a.ty - a.fy;
            const bool started = elapsed >= 0.0f;
            a.moving = started && u < 1.0f && dx * dx + dy * dy > 0.01f;
            if (a.moving) a.h = headingTo(a.fx, a.fy, a.tx, a.ty);
            else if (u >= 1.0f) a.h = a.th;
        }
        a.z = bz_;
        float h = a.h;
        if (int32_t(nowMs - a.faceUntil) < 0 && !a.moving) h = a.faceH;
        float hop = 0.0f, scale = 1.0f;
        const bool attacking = int32_t(nowMs - a.attackStart) >= 0 && int32_t(a.attackEnd - nowMs) > 0;
        if (attacking) {
            const float au = float(int32_t(nowMs - a.attackStart)) / float(int32_t(a.attackEnd - a.attackStart));
            const float bump = sinf(kPi * clamp01(au));
            hop = bump * (isToon ? 0.5f : 0.9f);
            if (!isToon) scale = 1.0f + 0.2f * bump;
        }
        float flash = clamp01(float(int32_t(a.flashEnd - nowMs)) / (kFlashSec * 1000.0f));
        if (a.flashEnd == 0 || int32_t(a.flashEnd - nowMs) < 0) flash = 0.0f;
        bool hidden = a.hidden;
        if (a.dying) {
            const float du = (float(int32_t(nowMs - a.dieStart)) / 1000.0f - kLoseSec) / kDieSec;
            if (du >= 1.0f) hidden = true;
            if (du > 0.0f) scale *= 1.0f - clamp01(du), flash = 1.0f;
        }
        out->doId = a.doId;
        out->x = a.x, out->y = a.y, out->z = a.z, out->h = h;
        if (!isToon && battleFx_) {
            const float lureOffset = battleFx_->movies().getLureOffset(a.doId);
            if (lureOffset > 0.01f) {
                const float rad = h * kDegToRad;
                out->x -= sinf(rad) * lureOffset;
                out->y += cosf(rad) * lureOffset;
            }
        }
        out->hop = hop;
        out->scale = scale;
        out->flash = flash;
        out->hidden = hidden;
        if (isToon) {
            // MovieThrow: pie-throw; MovieSquirt (flower): press-button; others: wave for now.
            uint8_t anim = a.moving ? kBattleAnimRun : kBattleAnimNeutral;
            if (attacking) {
                anim = a.attackTrack == kTrackThrow    ? uint8_t(kBattleAnimClip0 + kClipThrow)
                       : a.attackTrack == kTrackSquirt ? uint8_t(kBattleAnimClip0 + kClipPushButton)
                                                       : kBattleAnimAttack;
            } else if (a.reactUntil && int32_t(a.reactUntil - nowMs) > 0) {
                // [cog attacks] Reaction clip
                const int clip = a.reactClip >= 0 ? a.reactClip : (a.reactHit ? int(kClipCringe) : int(kClipSidestep));
                anim = uint8_t(kBattleAnimClip0 + clip);
            } else if (phase_ == BattlePhase::Reward) {
                anim = kBattleAnimClip0 + kClipVictory;
            }
            out->anim = anim;
        } else {
            // MovieThrow/MovieSquirt: pie-small-react / squirt-small-react; MovieSuitAttacks: the
            // attack's clip; a dodge sidesteps; death plays lose (Suit.py loseActor, simplified).
            uint8_t anim = a.moving ? uint8_t(kSuitWalk) : uint8_t(kSuitNeutral);
            if (a.dying) anim = kSuitAnimLose;
            else if (attacking) anim = kSuitAnimAttack;
            else if (a.reactUntil && int32_t(a.reactUntil - nowMs) > 0)
                anim = !a.reactHit ? kSuitAnimDodge : a.reactTrack == kTrackSquirt ? kSuitAnimReactSquirt : kSuitAnimReactPie;
            out->anim = anim;
        }
        out->clipArg = a.attackTrack;
        out->clipStartMs = a.dying ? a.dieStart : a.clipStartMs;
    };
    numSuitPoses_ = numToonPoses_ = 0;
    for (Actor& a : suits_) {
        if (a.used) evaluate(a, &suitPoses_[numSuitPoses_++], false);
    }
    for (Actor& a : toons_) {
        if (a.used) evaluate(a, &toonPoses_[numToonPoses_++], true);
    }
    // Age the floating numbers.
    int w = 0;
    for (int i = 0; i < numPopups_; ++i) {
        const uint32_t age = nowMs - popupStartMs_[i];
        if (age >= popupLifeMs_[i]) continue;
        popups_[i].age = float(age) / 1000.0f;
        if (w != i) {
            popups_[w] = popups_[i];
            popupStartMs_[w] = popupStartMs_[i];
            popupLifeMs_[w] = popupLifeMs_[i];
        }
        ++w;
    }
    numPopups_ = w;
}

bool Battle::localPose(BattleActorPose* out) const {
    for (int i = 0; i < numToonPoses_; ++i) {
        if (toonPoses_[i].doId == localId_) {
            *out = toonPoses_[i];
            return true;
        }
    }
    return false;
}

// ============================================================ panel data
void Battle::fillView(const net::Client& client, uint32_t nowMs, BattleView* v) const {
    *v = BattleView();
    v->phase = phase_;
    const net::Battle* nb = battleId_ ? client.findBattle(battleId_) : nullptr;
    switch (phase_) {
    case BattlePhase::Requested: v->status = "Getting ready to fight..."; break;
    case BattlePhase::FaceOff: v->status = "The cog wants a fight!"; break;
    case BattlePhase::Waiting: v->status = running_ ? "Running away!" : "Waiting for the next round..."; break;
    case BattlePhase::Input: v->status = "Choose a gag!"; break;
    case BattlePhase::Chosen: v->status = running_ ? "Running away!" : "Waiting for the other toons..."; break;
    case BattlePhase::Movie: v->status = "The round is playing out..."; break;
    case BattlePhase::Reward: v->status = "You beat the cogs!"; break;
    default: break;
    }
    if (phase_ == BattlePhase::Input) {
        int ms = int(int32_t(inputEndMs_ - nowMs));
        if (ms < 0) ms = 0;
        v->secondsLeft = (ms + 999) / 1000;
        if (v->secondsLeft > int(kInputTimeoutMs / 1000)) v->secondsLeft = int(kInputTimeoutMs / 1000);
    }
    v->canRun = phase_ == BattlePhase::Input;
    v->rewardSent = rewardSent_;
    if (!nb) return;
    for (int i = 0; i < nb->numSuits && v->numCogs < net::kBattleSuits; ++i) {
        BattleCogView& c = v->cogs[v->numCogs++];
        c.doId = nb->suits[i];
        c.active = (nb->suitsActive >> i) & 1;
        c.lured = (nb->suitsLured >> i) & 1;
        c.trapped = nb->suitTrap[i] >= 0;
        snprintf(c.name, sizeof(c.name), "Cog");
        c.level = 0;
        c.hp = c.maxHp = 0;
        net::Client::CogIdentity ident;
        if (client.cogIdentity(c.doId, &ident)) {
            const net::Client::CogIdentity* s = &ident;
            c.hp = s->hp;
            for (const Actor& a : suits_) {
                if (a.used && a.doId == c.doId && a.baseHp == s->hp) c.hp = s->hp - a.damage;
            }
            if (c.hp < 0) c.hp = 0;
            if (const SuitInfo* info = s->hasDna ? findSuitType(s->name) : nullptr) {
                snprintf(c.name, sizeof(c.name), "%s", info->name);
                c.level = info->baseLevel + s->levelDist + 1;
                if (s->levelDist >= 0 && s->levelDist < 5) c.maxHp = info->hp[s->levelDist];
            }
            if (c.maxHp < c.hp) c.maxHp = c.hp;
        }
    }
    for (int i = 0; i < nb->numToons && v->numToons < net::kBattleToons; ++i) {
        BattleToonView& t = v->toons[v->numToons++];
        t.doId = nb->toons[i];
        t.active = (nb->toonsActive >> i) & 1;
        t.running = (nb->toonsRunning >> i) & 1;
        t.local = t.doId == localId_;
        const char* name = client.avatarName(t.doId);
        snprintf(t.name, sizeof(t.name), "%s", name && name[0] ? name : "Toon");
    }
    // Experience earned this battle (setBattleExperience block for our toon).
    // RewardPanel shows a bar for every track the toon has, gained experience or not.
    const net::OwnStats& own = client.ownStats();
    for (const net::BattleExpToon& e : nb->exp.toons) {
        if (e.id < 0 || uint32_t(e.id) != localId_) continue;
        for (int t = 0; t < 7; ++t) {
            if (e.earnedExp[t] > 0 || own.trackAccess[t]) {
                BattleRewardRow& r = v->reward[v->numRewardRows++];
                r.track = t;
                r.earned = e.earnedExp[t];
                r.orig = e.origExp[t];
            }
        }
    }
    v->cogsDefeated = nb->exp.numDeathGroups;
}

} // namespace tt::world
