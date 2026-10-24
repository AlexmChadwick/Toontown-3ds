#include "game/minigame_session.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "net/dc_generated.h"

namespace tt::game {

namespace {

constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";
constexpr u16 kCatchGameId = 8;     // ToontownGlobals.CatchGameId
constexpr float kRulesSec = 4.0f;   // our rules screen (upstream: up to 16 s or the Play button)
constexpr float kToonCatchRadius = 0.7f; // DistributedCatchGame: legs/head/hand spheres
constexpr float kToonReach = 3.5f;       // highest of those spheres above the feet (head)
constexpr float kStunSec = 1.5f;         // CatchGameToonSD fallForward

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

void MinigameSession::setPhase(Phase p) {
    phase_ = p;
    phaseMs_ = 0;
}

void MinigameSession::shutdown() {
    unloadCatch();
    phase_ = Phase::Off;
}

void MinigameSession::begin(net::Client& client, u32 zone, u16 gameId, u32 homeHood, u32 nowMs) {
    unloadCatch();
    zone_ = zone;
    gameId_ = gameId;
    home_ = homeHood;
    isCatch_ = gameId == kCatchGameId;
    joinedSent_ = readySent_ = doneSent_ = exitedSent_ = purchaseExitSent_ = false;
    fruitsCaught_ = anvilsHit_ = 0;
    returnHood_ = 0;
    // Trolley.handlePlayMinigame: setParent(SPHidden), then the hood's quiet zone hand-off.
    client.sendSetParent(1);
    client.changeZone(zone);
    setPhase(Phase::Joining);
    phaseMs_ = nowMs;
    snprintf(title_, sizeof(title_), "%s", isCatch_ ? "Catching Game" : "Trolley game");
    snprintf(lines_[0], sizeof(lines_[0]), "Off to the game...");
    lines_[1][0] = lines_[2][0] = 0;
    TT_LOG("minigame: game %u in zone %lu (home %lu)", unsigned(gameId), static_cast<unsigned long>(zone),
           static_cast<unsigned long>(homeHood));
}

void MinigameSession::loadCatch(int numPlayers) {
    char path[96];
    snprintf(path, sizeof(path), "%scatch_ground_%d.ttm", kDir, numPlayers < 2 ? 2 : numPlayers > 4 ? 4 : numPlayers);
    ground_.load(path);
    for (int i = 0; i < catchgame::kNumDropTypes; i++) {
        snprintf(path, sizeof(path), "%scatch_%s.ttm", kDir, catchgame::dropTypeName(i));
        objects_[i].load(path);
    }
}

void MinigameSession::unloadCatch() {
    C3D_FrameSync();
    ground_.unload();
    for (auto& o : objects_) o.unload();
}

void MinigameSession::handleEvent(const net::Event& ev, net::Client& client, u32 nowMs) {
    using net::EventType;
    if (phase_ == Phase::Off) return;
    const net::Minigame* mg = client.minigame();
    switch (ev.type) {
    case EventType::MinigameUp:
        if (!mg) break;
        // DistributedMinigame.generate: setAvatarJoined before loading anything.
        if (!joinedSent_ && client.sendMinigameJoined()) joinedSent_ = true;
        if (!isCatch_) {
            // No 3DS version of this game yet: abort it (the AI pays 1..7 jellybeans).
            client.sendMinigameExit();
            snprintf(lines_[0], sizeof(lines_[0]), "This game isn't on the 3DS yet.");
            snprintf(lines_[1], sizeof(lines_[1]), "You still get a few jellybeans!");
            setPhase(Phase::Done);
            phaseMs_ = nowMs;
            break;
        }
        {
            double diff = 0;
            const u32 hood = mg->trolleyZone - mg->trolleyZone % 1000;
            static const u32 kSafeZones[6] = {2000, 1000, 5000, 4000, 3000, 9000}; // MinigameGlobals.SafeZones
            for (int i = 0; i < 6; i++)
                if (kSafeZones[i] == hood) diff = i / 5.0;
            if (mg->difficultyOverride != net::kNoDifficultyOverride) diff = mg->difficultyOverride / 65536.0;
            sched_.init(mg->doId, hood ? hood : home_, mg->numParticipants, diff);
            loadCatch(mg->numParticipants);
            nextDrop_ = 0;
            for (Live& l : live_) l = Live{};
            int idx = 0;
            for (int i = 0; i < mg->numParticipants; i++)
                if (mg->participants[i] == client.avatarId()) idx = i;
            // lineupPos(idx, n, 4.0): spread along x, facing the camera (H 180).
            tx_ = (idx - (mg->numParticipants - 1) * 0.5f) * 4.0f;
            ty_ = 0;
            th_ = 180.0f;
            speed_ = 0;
            stunned_ = 0;
            snprintf(lines_[0], sizeof(lines_[0]), "Catch the falling %ss!", catchgame::dropTypeName(sched_.fruit));
            snprintf(lines_[1], sizeof(lines_[1]), "Watch out for the anvils.");
            snprintf(lines_[2], sizeof(lines_[2]), "Circle Pad: run");
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        }
        break;
    case EventType::MinigameStart:
        if (isCatch_) {
            setPhase(Phase::Play);
            phaseMs_ = nowMs;
            TT_LOG("minigame: catch starts (%d drops, %d fruits)", sched_.numDrops, sched_.numFruits);
        }
        break;
    case EventType::CatchEveryoneDone:
        snprintf(lines_[0], sizeof(lines_[0]), "You caught %d of %d!", fruitsCaught_, sched_.numFruits);
        lines_[1][0] = 0;
        break;
    case EventType::MinigameExit:
    case EventType::MinigameAbort:
        // setGameExit: clean up, then setAvatarExited (normal exits only).
        if (ev.type == EventType::MinigameExit && !exitedSent_ && client.sendMinigameExited()) exitedSent_ = true;
        unloadCatch();
        setPhase(Phase::Purchase);
        phaseMs_ = nowMs;
        snprintf(lines_[1], sizeof(lines_[1]), "Counting jellybeans...");
        break;
    case EventType::PurchaseUp: {
        const net::Purchase* p = client.purchase();
        if (p) {
            for (int i = 0; i < net::kMinigamePlayers; i++) {
                if (p->playerIds[i] == client.avatarId()) {
                    snprintf(lines_[1], sizeof(lines_[1]), "You earned %u jellybeans!", unsigned(p->minigamePoints[i]));
                }
            }
        }
        setPhase(Phase::Purchase);
        phaseMs_ = nowMs;
        // No gag screen here: "Back to Playground" right away (trolley.md 2.5).
        if (!purchaseExitSent_ && client.sendPurchaseExit()) purchaseExitSent_ = true;
        break;
    }
    case EventType::PurchaseExit:
        client.sendPurchaseInventory(); // setInventory(done=1) with the post-payout money
        setPhase(Phase::Leaving);
        phaseMs_ = nowMs;
        break;
    default:
        break;
    }
}

void MinigameSession::update(net::Client& client, const circlePosition& pad, float dt, u32 nowMs) {
    if (phase_ == Phase::Off) return;
    const float inPhase = static_cast<s32>(nowMs - phaseMs_) / 1000.0f;
    switch (phase_) {
    case Phase::Rules:
        if (inPhase > kRulesSec && !readySent_ && client.sendMinigameReady()) {
            readySent_ = true;
            setPhase(Phase::WaitStart);
            phaseMs_ = nowMs;
            snprintf(lines_[2], sizeof(lines_[2]), "Waiting for the other toons...");
        }
        break;
    case Phase::Play: {
        const net::Minigame* mg = client.minigame();
        if (!mg) break;
        const double t = static_cast<s32>(nowMs - mg->gameStartMs) / 1000.0;
        // OrthoWalk: screen up = +y (the camera looks along +y), clamped to the stage.
        float ax = pad.dx / 156.0f, ay = pad.dy / 156.0f;
        if (autoPlay_) {
            float best = 1e9f;
            for (const Live& l : live_) {
                if (l.num < 0 || l.caught || !catchgame::dropTypeGood(sched_.drops[l.num].type) || l.z >= best) continue;
                best = l.z;
                const float dx = l.x - tx_, dy = l.y - ty_, d = sqrtf(dx * dx + dy * dy);
                ax = d > 0.3f ? dx / d : 0.0f;
                ay = d > 0.3f ? dy / d : 0.0f;
            }
        }
        const float mag = sqrtf(ax * ax + ay * ay);
        speed_ = 0;
        if (stunned_ > 0) {
            stunned_ -= dt;
        } else if (mag > 0.15f) {
            const float sp = static_cast<float>(sched_.toonSpeed) * (mag > 1 ? 1 : mag);
            tx_ += ax / mag * sp * dt;
            ty_ += ay / mag * sp * dt;
            th_ = atan2f(-ax, ay) * 180.0f / 3.14159265f;
            speed_ = sp;
        }
        const float hw = static_cast<float>(sched_.stageHalfWidth), hh = static_cast<float>(sched_.stageHalfHeight);
        tx_ = tx_ < -hw ? -hw : tx_ > hw ? hw : tx_;
        ty_ = ty_ < -hh ? -hh : ty_ > hh ? hh : ty_;
        // Spawn drops whose time has come (DistributedCatchGame.__dropTask).
        while (nextDrop_ < sched_.numDrops && sched_.drops[nextDrop_].t <= t) {
            const catchgame::Drop& d = sched_.drops[nextDrop_];
            for (Live& l : live_) {
                if (l.num >= 0) continue;
                double x, y;
                sched_.grid2world(d.gx, d.gy, &x, &y);
                l.num = nextDrop_;
                l.spawnT = d.t;
                l.x = static_cast<float>(x), l.y = static_cast<float>(y);
                l.h = static_cast<float>(d.spin * 360.0);
                l.caught = false;
                break;
            }
            nextDrop_++;
        }
        // Fall, catch, land.
        for (Live& l : live_) {
            if (l.num < 0) continue;
            const catchgame::Drop& d = sched_.drops[l.num];
            const double z = sched_.objectZ(d.type, t - l.spawnT);
            l.z = static_cast<float>(z);
            if (z < 0 || l.caught) {
                if (z < 0) l.num = -1;
                continue;
            }
            const bool good = catchgame::dropTypeGood(d.type);
            float r = 1.0f; // ObjRadius, larger for fruit on harder hoods
            if (good) r *= lerpf(1.0f, 1.3f, static_cast<float>(sched_.difficulty));
            const float dx = l.x - tx_, dy = l.y - ty_;
            if (z < kToonReach && dx * dx + dy * dy < (r + kToonCatchRadius) * (r + kToonCatchRadius) && stunned_ <= 0) {
                l.caught = true;
                client.sendCatchClaim(static_cast<u32>(l.num), d.type);
                if (good) {
                    fruitsCaught_++;
                } else {
                    anvilsHit_++;
                    stunned_ = kStunSec;
                }
                snprintf(lines_[0], sizeof(lines_[0]), "Caught %d of %d", fruitsCaught_, sched_.numFruits);
            }
        }
        // reportDone once the last drop has finished falling.
        if (!doneSent_ && nextDrop_ >= sched_.numDrops && sched_.numDrops > 0) {
            const catchgame::Drop& last = sched_.drops[sched_.numDrops - 1];
            if (t > last.t + sched_.fallDuration(last.type) && client.sendCatchDone()) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
                TT_LOG("minigame: catch done, %d of %d fruits, %d anvils", fruitsCaught_, sched_.numFruits, anvilsHit_);
            }
        }
        const int left = static_cast<int>(catchgame::kGameDuration - t);
        snprintf(lines_[2], sizeof(lines_[2]), "Time: %d", left > 0 ? left : 0);
        break;
    }
    case Phase::Leaving:
        if (inPhase > 0.5f) {
            returnHood_ = home_;
            setPhase(Phase::Off);
        }
        break;
    case Phase::Purchase:
        // The PM never came (or the exit was lost): go home anyway after a while.
        if (inPhase > 20.0f) {
            returnHood_ = home_;
            setPhase(Phase::Off);
        }
        break;
    default:
        if (phase_ != Phase::Play && inPhase > 90.0f) { // something got lost: never strand the player
            returnHood_ = home_;
            setPhase(Phase::Off);
        }
        break;
    }
    if (phaseMs_ == 0) phaseMs_ = nowMs;
}

u32 MinigameSession::popReturn() {
    const u32 h = returnHood_;
    returnHood_ = 0;
    return h;
}

void MinigameSession::toonPose(float* x, float* y, float* z, float* h) const {
    *x = tx_, *y = ty_, *z = 0.0f, *h = th_;
}

void MinigameSession::camera(C3D_FVec* eye, C3D_FVec* target) const {
    // CameraPosHpr: (0, -29.36, 28.17) for 1-2 players (further back for 3-4), pitched -35.
    const float back = 29.36f, up = 28.17f, pitch = 35.0f * 3.14159265f / 180.0f;
    *eye = FVec3_New(0.0f, -back, up);
    *target = FVec3_New(0.0f, -back + cosf(pitch) * 40.0f, up - sinf(pitch) * 40.0f);
}

void MinigameSession::draw(render::Renderer& renderer) {
    if (!inScene()) return;
    C3D_Mtx m;
    Mtx_Identity(&m);
    if (ground_.loaded()) renderer.drawModel(ground_, m);
    // Falling objects (their scale and offsets are baked; bake_minigame.py).
    for (const Live& l : live_) {
        if (l.num < 0 || l.caught) continue;
        render::Model& model = objects_[sched_.drops[l.num].type];
        if (!model.loaded()) continue;
        Mtx_Identity(&m);
        Mtx_Translate(&m, l.x, l.y, l.z, true);
        Mtx_RotateZ(&m, l.h * 3.14159265f / 180.0f, true);
        renderer.drawModel(model, m);
    }
}

} // namespace tt::game
