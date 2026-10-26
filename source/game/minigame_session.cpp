#include "game/minigame_session.h"
#include "world/toon_avatar.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "net/dc_generated.h"

namespace tt::game {

namespace {

constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";
constexpr u16 kRaceGameId = 1;      // ToontownGlobals.RaceGameId
constexpr u16 kCannonGameId = 2;    // ToontownGlobals.CannonGameId
constexpr u16 kTagGameId = 3;       // ToontownGlobals.TagGameId
constexpr u16 kPatternGameId = 4;   // ToontownGlobals.PatternGameId
constexpr u16 kRingGameId = 5;      // ToontownGlobals.RingGameId
constexpr u16 kMazeGameId = 6;      // ToontownGlobals.MazeGameId
constexpr u16 kTugGameId = 7;       // ToontownGlobals.TugOfWarGameId
constexpr u16 kCatchGameId = 8;     // ToontownGlobals.CatchGameId
constexpr u16 kDivingGameId = 9;    // ToontownGlobals.DivingGameId
constexpr u16 kTargetGameId = 10;   // ToontownGlobals.TargetGameId
constexpr u16 kPairingGameId = 11;  // ToontownGlobals.PairingGameId
constexpr u16 kVineGameId = 12;     // ToontownGlobals.VineGameId
constexpr u16 kIceGameId = 13;      // ToontownGlobals.IceGameId
constexpr u16 kCogThiefGameId = 14; // ToontownGlobals.CogThiefGameId
constexpr u16 kTwoDGameId = 15;     // ToontownGlobals.TwoDGameId
constexpr u16 kPhotoGameId = 16;    // ToontownGlobals.PhotoGameId

constexpr float kRulesSec = 4.0f;   // our rules screen
constexpr float kToonCatchRadius = 0.7f;
constexpr float kToonReach = 3.5f;


float lerpf(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

void MinigameSession::setPhase(Phase p) {
    phase_ = p;
    phaseMs_ = 0;
}

void MinigameSession::unloadCatch() {
    assetBatch_.clear();
    C3D_FrameSync();
    ground_.unload();
    for (auto& o : objects_) o.unload();
}

void MinigameSession::unloadCurrent() {
    C3D_FrameSync();
    if (isCatch_) unloadCatch();
    if (isCannon_) cannon_.shutdown();
    if (isTug_) tug_.shutdown();
    if (isRing_) ring_.shutdown();
    if (isMaze_) maze_.shutdown();
    if (isPairing_) pairing_.shutdown();
    if (isDiving_) diving_.shutdown();
    if (isVine_) vine_.shutdown();
    if (isTarget_) target_.shutdown();
    if (isCogThief_) cogThief_.shutdown();
    if (isTwoD_) twoD_.shutdown();
    if (isTag_) tag_.shutdown();
    if (isPattern_) pattern_.shutdown();
    if (isIce_) ice_.shutdown();
    if (isRace_) race_.shutdown();
    if (isPhoto_) photo_.shutdown();
    purchasePanel_.close();
}

void MinigameSession::shutdown() {
    unloadCurrent();
    purchasePanel_.shutdown();
    phase_ = Phase::Off;
}

void MinigameSession::begin(net::Client& client, u32 zone, u16 gameId, u32 homeHood, u32 nowMs) {
    unloadCurrent();
    zone_ = zone;
    gameId_ = gameId;
    home_ = homeHood;
    startedGame_ = endedGame_ = completedPurchase_ = false;

    isCatch_ = (gameId == kCatchGameId);
    isCannon_ = (gameId == kCannonGameId);
    isTug_ = (gameId == kTugGameId);
    isRing_ = (gameId == kRingGameId);
    isMaze_ = (gameId == kMazeGameId);
    isPairing_ = (gameId == kPairingGameId);
    isDiving_ = (gameId == kDivingGameId);
    isVine_ = (gameId == kVineGameId);
    isTarget_ = (gameId == kTargetGameId);
    isCogThief_ = (gameId == kCogThiefGameId);
    isTwoD_ = (gameId == kTwoDGameId);
    isTag_ = (gameId == kTagGameId);
    isPattern_ = (gameId == kPatternGameId);
    isIce_ = (gameId == kIceGameId);
    isRace_ = (gameId == kRaceGameId);
    isPhoto_ = (gameId == kPhotoGameId);

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
                      : isTarget_ ? "Toon Slingshot"
                      : isCogThief_ ? "Cog Thief"
                      : isTwoD_ ? "Toon Escape"
                      : isTag_ ? "Tag Game"
                      : isPattern_ ? "Match Minnie"
                      : isIce_ ? "Ice Slide"
                      : isRace_ ? "Race Game"
                      : isPhoto_ ? "Photo Fun"
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
    assetBatch_.load(ground_, path);
    for (int i = 0; i < catchgame::kNumDropTypes; i++) {
        snprintf(path, sizeof(path), "%scatch_%s.ttm", kDir, catchgame::dropTypeName(i));
        assetBatch_.load(objects_[i], path);
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
            stunned_ = eating_ = 0;
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
        } else if (isTarget_) {
            target_.init(kDir, mg->targetSeed, mg->trolleyZone - mg->trolleyZone % 1000);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isCogThief_) {
            cogThief_.init(kDir);
            cogThief_.chooseSuitTypes(mg->doId);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isTwoD_) {
            twoD_.init(kDir, home_);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isTag_) {
            tag_.init(kDir, mg->tagItAvId ? mg->tagItAvId : client.avatarId());
            tag_.setAutoPlay(autoPlay_);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isPattern_) {
            pattern_.init(kDir);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isIce_) {
            ice_.init(kDir, mg->numParticipants);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isRace_) {
            race_.init(kDir);
            // DistributedRaceGame: each toon runs in the lane of its avIdList index.
            for (int i = 0; i < mg->numParticipants && i < racegame::kNumLanes; ++i)
                if (mg->participants[i] == client.avatarId()) race_.setLane(i);
            setPhase(Phase::Rules);
            phaseMs_ = nowMs;
        } else if (isPhoto_) {
            // ---- games2 ----: PhotoGameBase seeds assignments with its distributed object id.
            photo_.init(kDir, home_, mg->doId);
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
        startedGame_ = true;
        TT_LOG("mgpass: start id %u", unsigned(gameId_));
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
        endedGame_ = true;
        TT_LOG("mgpass: result id %u: %.70s / %.70s", unsigned(gameId_), line(0), line(1));
        TT_LOG("mgpass: end id %u aborted %u", unsigned(gameId_), unsigned(ev.type == EventType::MinigameAbort));
        if (ev.type == EventType::MinigameExit && !exitedSent_ && client.sendMinigameExited()) exitedSent_ = true;
        // The game's scene stays resident through the purchase (upstream keeps the last frame
        // until the purchase step is over); it is unloaded when we leave (Phase::Leaving).
        setPhase(Phase::Purchase);
        phaseMs_ = nowMs;
        snprintf(lines_[1], sizeof(lines_[1]), "Counting jellybeans...");
        break;

    case EventType::PurchaseUp: {
        const net::Purchase* p = client.purchase();
        uint32_t earnedBeans = 0;
        if (p) {
            for (int i = 0; i < net::kMinigamePlayers; i++) {
                if (p->playerIds[i] == client.avatarId()) {
                    earnedBeans = p->minigamePoints[i];
                    snprintf(lines_[1], sizeof(lines_[1]), "You earned %u jellybeans!", unsigned(earnedBeans));
                }
            }
        }
        TT_LOG("mgpass: score id %u beans %lu (server)", unsigned(gameId_), static_cast<unsigned long>(earnedBeans));
        setPhase(Phase::Purchase);
        phaseMs_ = nowMs;
        purchasePanel_.init();
        const uint32_t deadlineMs = p && p->deadlineMs ? p->deadlineMs : nowMs + net::kPurchaseCountdownMs;
        purchasePanel_.open(client, earnedBeans, deadlineMs, nowMs);
        break;
    }

    case EventType::PurchaseExit:
        if (!client.sendPurchaseInventory(purchasePanel_.cart().inv, purchasePanel_.cart().money)) break;
        completedPurchase_ = true;
        TT_LOG("mgpass: purchase exit id %u acknowledged, inventory submitted", unsigned(gameId_));
        purchasePanel_.close();
        setPhase(Phase::Leaving);
        phaseMs_ = nowMs;
        break;

    case EventType::TargetRoundDone:
        if (isTarget_) {
            target_.nextRound();
            doneSent_ = false;
            setPhase(Phase::Play);
            phaseMs_ = nowMs;
        }
        break;

    case EventType::TagSetIt:
        if (isTag_ && mg) tag_.setIt(mg->tagItAvId);
        break;

    case EventType::PatternSetPlayerPatterns:
        if (isPattern_) pattern_.onResults();
        break;

    case EventType::RaceServerChoices:
        if (isRace_ && mg) {
            uint8_t choices[4], positions[4];
            int16_t rewards[4];
            const int count = mg->numParticipants < 4 ? mg->numParticipants : 4;
            for (int i = 0; i < count; ++i) {
                choices[i] = uint8_t(mg->raceServerChoices[i]);
                positions[i] = mg->raceServerPositions[i];
                rewards[i] = mg->raceServerRewards[i];
            }
            race_.onSetServerChoices(choices, positions, rewards, count);
        }
        break;

    case EventType::PatternSetPattern:
        if (isPattern_ && mg) pattern_.onSetPattern(mg->patternSeq, mg->patternSeqLen);
        break;

    case EventType::IceNewState:
    case EventType::IceTireInputs:
    case EventType::IceFinalPositions:
    case EventType::IceScores:
        if (isIce_ && mg) ice_.onServer(ev.type, *mg);
        break;

    case EventType::IceMatchAndRound:
        if (isIce_ && mg) ice_.onSetMatchAndRound(static_cast<uint8_t>(mg->iceMatch), static_cast<uint8_t>(mg->iceRound));
        break;

    case EventType::RaceChancePositions:
        if (isRace_ && mg) race_.onSetChancePositions(mg->raceChancePositions, mg->numParticipants);
        break;

    // ---- games2 ----
    case EventType::TwoDSetSections:
        if (isTwoD_) twoD_.handleSetSections(client);
        break;
    case EventType::TwoDTreasureGrabbed:
        if (isTwoD_) twoD_.handleTreasureGrabbed(ev.doId, uint8_t(ev.arg >> 8), uint8_t(ev.arg));
        break;
    case EventType::TwoDEnemyShot:
        if (isTwoD_) twoD_.handleEnemyShot(uint8_t(ev.arg >> 24), uint8_t(ev.arg >> 16), uint16_t(ev.arg));
        break;
    case EventType::TwoDVictoryScore:
        if (isTwoD_) twoD_.handleVictoryScore(uint8_t(ev.arg));
        break;
    case EventType::TwoDEveryoneDone:
        if (isTwoD_) twoD_.handleEveryoneDone();
        break;
    case EventType::PhotoNewAIScore:
        if (isPhoto_) photo_.onAIScore(ev.doId, uint8_t(ev.arg >> 24), float(ev.arg & 0xffffff) / 100.0f);
        break;
    // ---- games2 ----

    default:
        break;
    }
}

void MinigameSession::update(net::Client& client, const circlePosition& pad, float dt, u32 nowMs) {
    if (phase_ == Phase::Off) return;
    const float inPhase = static_cast<s32>(nowMs - phaseMs_) / 1000.0f;
    if (client.state() == net::State::Backoff || client.state() == net::State::Failed ||
        client.state() == net::State::Idle) {
        TT_LOG("mgpass: disconnect id %u (incomplete)", unsigned(gameId_));
        shutdown();
        return;
    }
    u32 kDown = hidKeysDown();
    // Test-only physical button presses: use each game's regular input/sender path.
    if (autoPlay_ && (isTarget_ || isCogThief_ || isTwoD_ || isTag_ || isIce_ || isRace_ || isPhoto_) &&
        phase_ == Phase::Play && int(inPhase * 2) != int((inPhase - dt) * 2))
        kDown |= KEY_A;

    switch (phase_) {
    case Phase::Rules:
        if (inPhase > kRulesSec && !loadingAssets() && !readySent_ && client.sendMinigameReady()) {
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

        if (isTarget_) {
            target_.update(client, pad, kDown, dt, nowMs);
            if (target_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isCogThief_) {
            cogThief_.update(client, pad, kDown, dt, nowMs);
            if (cogThief_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isTwoD_) {
            // ---- games2 ----
            twoD_.setAutoPlay(autoPlay_);
            // ---- games2 ----
            twoD_.update(client, pad, kDown, dt, nowMs);
            if (twoD_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isTag_) {
            tag_.update(client, pad, kDown, dt, nowMs);
            if (tag_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isPattern_) {
            pattern_.setAutoPlay(autoPlay_);
            pattern_.update(client, pad, kDown, dt, nowMs);
            if (pattern_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isIce_) {
            ice_.update(client, pad, kDown, dt, nowMs);
            if (ice_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isRace_) {
            race_.update(client, pad, kDown, dt, nowMs);
            if (race_.isDone() && !doneSent_) {
                doneSent_ = true;
                setPhase(Phase::Done);
                phaseMs_ = nowMs;
            }
            break;
        }

        if (isPhoto_) {
            photo_.update(client, pad, kDown, hidKeysHeld(), dt, nowMs, autoPlay_);
            if (photo_.isDone() && !doneSent_) {
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
            if(eating_>0) eating_-=dt;
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
                        eating_=16/24.0f;
                    } else {
                        anvilsHit_++;
                        stunned_ = 44/42.0f; // CatchGameToonSD inclusive frame-12 fall interval
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
            unloadCurrent();
            setPhase(Phase::Off);
            TT_LOG("mgpass: returned id %u", unsigned(gameId_));
        }
        break;

    case Phase::Purchase:
        // A requestExit is not setPurchaseExit. Stay here until the server's
        // reply, then submit inventory(done=1) before returning to the playground.
        if (autoPlay_ && inPhase > 3 && purchasePanel_.active()) purchasePanel_.finish(false);
        purchasePanel_.update(client, nowMs);
        if (purchasePanel_.active()) {
            touchPosition touch;
            hidTouchRead(&touch);
            purchasePanel_.handleInput(kDown, touch);
        }
        break;

    default:
        // No timed escape into the world with a live game or purchase object.
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
    } else if (isTarget_) {
        target_.toonPose(x, y, z, h);
    } else if (isCogThief_) {
        cogThief_.toonPose(x, y, z, h);
    } else if (isTwoD_) {
        twoD_.toonPose(x, y, z, h);
    } else if (isTag_) {
        tag_.toonPose(x, y, z, h);
    } else if (isPattern_) {
        pattern_.toonPose(x, y, z, h);
    } else if (isIce_) {
        ice_.toonPose(x, y, z, h);
    } else if (isRace_) {
        race_.toonPose(x, y, z, h);
    } else if (isPhoto_) {
        photo_.toonPose(x, y, z, h);
    } else {
        *x = tx_;
        *y = ty_;
        *z = 0.0f;
        *h = th_;
    }
}

int MinigameSession::placedPlayers(const net::Client& client, MinigamePlayer* out, int max) const {
    const net::Minigame* mg = client.minigame();
    if (!mg || !inGame()) return 0;
    int n = 0;
    if (isRace_) {
        // DistributedRaceGame.positionInPlace: everyone stands on their own lane (avIdList index).
        for (int lane = 0; lane < mg->numParticipants && n < max; ++lane) {
            if (mg->participants[lane] == client.avatarId()) continue;
            MinigamePlayer& p = out[n++];
            p.doId = mg->participants[lane];
            race_.lanePose(lane, &p.x, &p.y, &p.z, &p.h);
        }
    } else if (isPattern_) {
        // DistributedPatternGame.getBackRowPos: the others stand in the back row, ordered by
        // remoteAvIdList, facing the camera.
        constexpr float kBackRowHome[3] = {3.0f, 11.0f, 0.0f};
        constexpr float kBackRowXSpacing = 1.8f;
        const int spots = mg->numParticipants - 1;
        int index = 0;
        for (int i = 0; i < mg->numParticipants && n < max; ++i) {
            if (mg->participants[i] == client.avatarId()) continue;
            MinigamePlayer& p = out[n++];
            p.doId = mg->participants[i];
            p.x = kBackRowHome[0] + kBackRowXSpacing * float(index) - kBackRowXSpacing * float(spots - 1) / 2.0f;
            p.y = kBackRowHome[1];
            p.z = kBackRowHome[2];
            p.h = 180.0f; // makeToonLookatCamera: the camera is in front of the stage
            ++index;
        }
    }
    return n;
}

int MinigameSession::cogs(MinigameCog* out, int max) const {
    if (phase_ != Phase::Play && phase_ != Phase::Rules) return 0;
    if (isMaze_) return maze_.cogs(out, max);
    if (isCogThief_) return cogThief_.cogs(out, max);
    if (isTug_) return tug_.cogs(out, max);
    return 0;
}

bool MinigameSession::toonRunning() const {
    if (isCannon_) return cannon_.toonRunning();
    if (isTug_) return tug_.toonRunning();
    if (isRing_) return ring_.toonRunning();
    if (isMaze_) return maze_.toonRunning();
    if (isPairing_) return pairing_.toonRunning();
    if (isDiving_) return diving_.toonRunning();
    if (isVine_) return vine_.toonRunning();
    if (isRace_) return race_.toonRunning();
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
    } else if (isTarget_) {
        target_.camera(eye, target);
    } else if (isCogThief_) {
        cogThief_.camera(eye, target);
    } else if (isTwoD_) {
        twoD_.camera(eye, target);
    } else if (isTag_) {
        tag_.camera(eye, target);
    } else if (isPattern_) {
        pattern_.camera(eye, target);
    } else if (isIce_) {
        ice_.camera(eye, target);
    } else if (isRace_) {
        race_.camera(eye, target);
    } else if (isPhoto_) {
        photo_.camera(eye, target);
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
    if (isTarget_) return target_.line(i);
    if (isCogThief_) return cogThief_.line(i);
    if (isTwoD_) return twoD_.line(i);
    if (isTag_) return tag_.line(i);
    if (isPattern_) return pattern_.line(i);
    if (isIce_) return ice_.line(i);
    if (isRace_) return race_.line(i);
    if (isPhoto_) return photo_.line(i);
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
    if (isTarget_) {
        target_.draw(renderer);
        return;
    }
    if (isCogThief_) {
        cogThief_.draw(renderer);
        return;
    }
    if (isTwoD_) {
        twoD_.draw(renderer);
        return;
    }
    if (isTag_) {
        tag_.draw(renderer);
        return;
    }
    if (isPattern_) {
        pattern_.draw(renderer);
        return;
    }
    if (isIce_) {
        ice_.draw(renderer);
        return;
    }
    if (isRace_) {
        race_.draw(renderer);
        return;
    }
    if (isPhoto_) {
        photo_.draw(renderer);
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

const char* MinigameSession::toonNetworkState() const {
    if(isCatch_) return stunned_>0?"off":eating_>0?"CatchEating":"Catching";
    if(isRing_ || isDiving_ || (isCannon_ && cannon_.toonRunning())) return "swim";
    return "off";
}

void MinigameSession::applyAnimation(world::ToonAvatar& avatar) const {
    using namespace world;
    avatar.setHeadOnly(false);avatar.setBodyPose();
    if(isCannon_) { cannon_.applyAnimation(avatar);return; }
    if(isTug_) { tug_.applyAnimation(avatar);return; }
    if(isRing_ || isDiving_) { avatar.playAction(kAnimSwim,ClipMode::Loop);return; }
    if(isCatch_) {
        if(stunned_>0) {
            avatar.playAction(kAnimSlipForward,ClipMode::Once,42/24.0f,12/24.0f,55/24.0f);
            avatar.seek(44/42.0f-stunned_);return;
        }
        avatar.playAction(eating_>0?(toonRunning()?kAnimCatchEatnrun:kAnimCatchEatneutral):
            (toonRunning()?kAnimCatchRun:kAnimCatchNeutral),ClipMode::Loop);
        return;
    }
    avatar.play(toonRunning()?kAnimRun:kAnimNeutral,1);
}

} // namespace tt::game
