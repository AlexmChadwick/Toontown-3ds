#include "game/minigame_session.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "net/dc_generated.h"

namespace tt::game {

namespace {

constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";
constexpr u16 kCannonGameId = 2;    // ToontownGlobals.CannonGameId
constexpr u16 kRingGameId = 5;      // ToontownGlobals.RingGameId
constexpr u16 kMazeGameId = 6;      // ToontownGlobals.MazeGameId
constexpr u16 kTugGameId = 7;       // ToontownGlobals.TugOfWarGameId
constexpr u16 kCatchGameId = 8;     // ToontownGlobals.CatchGameId
constexpr u16 kDivingGameId = 9;    // ToontownGlobals.DivingGameId
constexpr u16 kPairingGameId = 11;  // ToontownGlobals.PairingGameId
constexpr u16 kVineGameId = 12;     // ToontownGlobals.VineGameId

constexpr float kRulesSec = 4.0f;   // our rules screen
constexpr float kToonCatchRadius = 0.7f;
constexpr float kToonReach = 3.5f;
constexpr float kStunSec = 1.5f;

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

void MinigameSession::setPhase(Phase p) {
    phase_ = p;
    phaseMs_ = 0;
}

void MinigameSession::unloadCatch() {
    C3D_FrameSync();
    ground_.unload();
    for (auto& o : objects_) o.unload();
}

void MinigameSession::unloadCurrent() {
    if (isCatch_) unloadCatch();
    if (isCannon_) cannon_.shutdown();
    if (isTug_) tug_.shutdown();
    if (isRing_) ring_.shutdown();
    if (isMaze_) maze_.shutdown();
    if (isPairing_) pairing_.shutdown();
    if (isDiving_) diving_.shutdown();
    if (isVine_) vine_.shutdown();
}

void MinigameSession::shutdown() {
    unloadCurrent();
    phase_ = Phase::Off;
}

void MinigameSession::begin(net::Client& client, u32 zone, u16 gameId, u32 homeHood, u32 nowMs) {
    unloadCurrent();
    zone_ = zone;
    gameId_ = gameId;
    home_ = homeHood;

    isCatch_ = (gameId == kCatchGameId);
    isCannon_ = (gameId == kCannonGameId);
    isTug_ = (gameId == kTugGameId);
    isRing_ = (gameId == kRingGameId);
    isMaze_ = (gameId == kMazeGameId);
    isPairing_ = (gameId == kPairingGameId);
    isDiving_ = (gameId == kDivingGameId);
    isVine_ = (gameId == kVineGameId);

    joinedSent_ = readySent_ = doneSent_ = exitedSent_ = purchaseExitSent_ = false;
    fruitsCaught_ = anvilsHit_ = 0;
    returnHood_ = 0;

    // Trolley.handlePlayMinigame: setParent(SPHidden), then hood's quiet zone.
    client.sendSetParent(1);
    client.changeZone(zone);
    setPhase(Phase::Joining);
    phaseMs_ = nowMs;

    const char* gName = isCatch_ ? "Catching Game"
                      : isCannon_ ? "Cannon Game"
                      : isTug_ ? "Tug-of-War"
                      : isRing_ ? "Ring Game"
                      : isMaze_ ? "Maze Game"
                      : isPairing_ ? "Match Minnie"
                      : isDiving_ ? "Diving Game"
                      : isVine_ ? "Jungle Vines"
                      : "Trolley game";
    snprintf(title_, sizeof(title_), "%s", gName);
    snprintf(lines_[0], sizeof(lines_[0]), "Off to the game...");
    lines_[1][0] = lines_[2][0] = 0;

    TT_LOG("minigame: game %u (%s) in zone %lu (home %lu)", unsigned(gameId), gName,
           static_cast<unsigned long>(zone), static_cast<unsigned long>(homeHood));
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

void MinigameSession::handleEvent(const net::Event& ev, net::Client& client, u32 nowMs) {
    using net::EventType;
    if (phase_ == Phase::Off) return;
    const net::Minigame* mg = client.minigame();

    switch (ev.type) {
    case EventType::MinigameUp:
        if (!mg) break;
        if (!joinedSent_ && client.sendMinigameJoined()) joinedSent_ = true;

        if (isCatch_) {
            double diff = 0;
            const u32 hood = mg->trolleyZone - mg->trolleyZone % 1000;
            static const u32 kSafeZones[6] = {2000, 1000, 5000, 4000, 3000, 9000};
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
        } else if (isCannon_) {
            cannon_.init(client, nowMs, autoPlay_);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isTug_) {
            tug_.init(client, nowMs, autoPlay_);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isRing_) {
            ring_.init(client, nowMs, autoPlay_);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isMaze_) {
            maze_.init(client, nowMs, autoPlay_);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isPairing_) {
            pairing_.init(client, nowMs, autoPlay_);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isDiving_) {
            diving_.init(client, nowMs, autoPlay_);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isVine_) {
            vine_.init(client, nowMs, autoPlay_);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else {
            // Unsupported minigame: abort it (AI pays 1..7 jellybeans).
            client.sendMinigameExit();
            snprintf(lines_[0], sizeof(lines_[0]), "This game isn't on the 3DS yet.");
            snprintf(lines_[1], sizeof(lines_[1]), "You still get a few jellybeans!");
            setPhase(Phase::Done);
            phaseMs_ = nowMs;
        }
        break;

    case EventType::MinigameStart:
        setPhase(Phase::Play);
        phaseMs_ = nowMs;
        TT_LOG("minigame: game %u starts", unsigned(gameId_));
        break;

    case EventType::CatchEveryoneDone:
        snprintf(lines_[0], sizeof(lines_[0]), "You caught %d of %d!", fruitsCaught_, sched_.numFruits);
        lines_[1][0] = 0;
        break;

    case EventType::MinigameExit:
    case EventType::MinigameAbort:
        if (ev.type == EventType::MinigameExit && !exitedSent_ && client.sendMinigameExited()) exitedSent_ = true;
        unloadCurrent();
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
        if (!purchaseExitSent_ && client.sendPurchaseExit()) purchaseExitSent_ = true;
        break;
    }

    case EventType::PurchaseExit:
        client.sendPurchaseInventory();
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
    const u32 kDown = hidKeysDown();

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

        if (isCannon_) {
            cannon_.update(client, pad, kDown, dt, nowMs);
            if (cannon_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isTug_) {
            tug_.update(client, pad, kDown, dt, nowMs);
            if (tug_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isRing_) {
            ring_.update(client, pad, kDown, dt, nowMs);
            if (ring_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isMaze_) {
            maze_.update(client, pad, kDown, dt, nowMs);
            if (maze_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isPairing_) {
            pairing_.update(client, pad, kDown, dt, nowMs);
            if (pairing_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isDiving_) {
            diving_.update(client, pad, kDown, dt, nowMs);
            if (diving_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isVine_) {
            vine_.update(client, pad, kDown, dt, nowMs);
            if (vine_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isCatch_) {
            const double t = static_cast<s32>(nowMs - mg->gameStartMs) / 1000.0;
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
                float r = 1.0f;
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
        }
        break;
    }

    case Phase::Leaving:
        if (inPhase > 0.5f) {
            returnHood_ = home_;
            setPhase(Phase::Off);
        }
        break;

    case Phase::Purchase:
        if (inPhase > 20.0f) {
            returnHood_ = home_;
            setPhase(Phase::Off);
        }
        break;

    default:
        if (phase_ != Phase::Play && inPhase > 90.0f) {
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
    if (isCannon_) {
        cannon_.toonPose(x, y, z, h);
    } else if (isTug_) {
        tug_.toonPose(x, y, z, h);
    } else if (isRing_) {
        ring_.toonPose(x, y, z, h);
    } else if (isMaze_) {
        maze_.toonPose(x, y, z, h);
    } else if (isPairing_) {
        pairing_.toonPose(x, y, z, h);
    } else if (isDiving_) {
        diving_.toonPose(x, y, z, h);
    } else if (isVine_) {
        vine_.toonPose(x, y, z, h);
    } else {
        *x = tx_;
        *y = ty_;
        *z = 0.0f;
        *h = th_;
    }
}

bool MinigameSession::toonRunning() const {
    if (isCannon_) return cannon_.toonRunning();
    if (isTug_) return tug_.toonRunning();
    if (isRing_) return ring_.toonRunning();
    if (isMaze_) return maze_.toonRunning();
    if (isPairing_) return pairing_.toonRunning();
    if (isDiving_) return diving_.toonRunning();
    if (isVine_) return vine_.toonRunning();
    return speed_ > 0.5f;
}

void MinigameSession::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (isCannon_) {
        cannon_.camera(eye, target);
    } else if (isTug_) {
        tug_.camera(eye, target);
    } else if (isRing_) {
        ring_.camera(eye, target);
    } else if (isMaze_) {
        maze_.camera(eye, target);
    } else if (isPairing_) {
        pairing_.camera(eye, target);
    } else if (isDiving_) {
        diving_.camera(eye, target);
    } else if (isVine_) {
        vine_.camera(eye, target);
    } else {
        const float back = 29.36f, up = 28.17f, pitch = 35.0f * 3.14159265f / 180.0f;
        *eye = FVec3_New(0.0f, -back, up);
        *target = FVec3_New(0.0f, -back + cosf(pitch) * 40.0f, up - sinf(pitch) * 40.0f);
    }
}

const char* MinigameSession::line(int i) const {
    if (phase_ == Phase::Purchase || phase_ == Phase::Leaving || phase_ == Phase::Joining) {
        return (i >= 0 && i < 3) ? lines_[i] : "";
    }
    if (isCannon_) return cannon_.line(i);
    if (isTug_) return tug_.line(i);
    if (isRing_) return ring_.line(i);
    if (isMaze_) return maze_.line(i);
    if (isPairing_) return pairing_.line(i);
    if (isDiving_) return diving_.line(i);
    if (isVine_) return vine_.line(i);
    return (i >= 0 && i < 3) ? lines_[i] : "";
}

void MinigameSession::draw(render::Renderer& renderer) {
    if (!inScene()) return;

    if (isCannon_) {
        cannon_.draw(renderer);
        return;
    }
    if (isTug_) {
        tug_.draw(renderer);
        return;
    }
    if (isRing_) {
        ring_.draw(renderer);
        return;
    }
    if (isMaze_) {
        maze_.draw(renderer);
        return;
    }
    if (isPairing_) {
        pairing_.draw(renderer);
        return;
    }
    if (isDiving_) {
        diving_.draw(renderer);
        return;
    }
    if (isVine_) {
        vine_.draw(renderer);
        return;
    }

    if (isCatch_) {
        C3D_Mtx m;
        Mtx_Identity(&m);
        if (ground_.loaded()) renderer.drawModel(ground_, m);
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
}

} // namespace tt::game
