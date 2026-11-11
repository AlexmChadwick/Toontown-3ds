#include "game/ice_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tt::game {

namespace {
// DistributedIceGame / DistributedIceWorld constants (IceGameGlobals.py).
constexpr float kTireBaseZ = 0.01f;         // TireRadius - (TireRadius - 0.01)
constexpr float kScoreCircleZ = 0.75f;      // TireRadius / 2
constexpr float kExpandFeetPerSec = 5.0f;   // ExpandFeetPerSec
constexpr float kCircleStartScale = 0.01f;  // scoreCircle.setScale(0.01)
constexpr float kCameraEyeY = -22.0f;       // cameraThreeQuarterView
constexpr float kCameraEyeZ = 45.0f;
constexpr float kCameraPitchDeg = 62.89f;   // hpr P -62.89: looking down
constexpr float kLookDistance = 50.0f;
} // namespace

bool IceGame::init(const char* root, int players, int slot, uint32_t doId, uint32_t hood) {
    sim_.init(players);
    rng_.initGenrand(doId); // DistributedMinigame.randomNumGen = RandomNumGen(self.doId)
    hood_ = hood;
    numTreasures_ = numPenalties_ = 0;
    setupMatch_ = -1;
    slot_ = (slot >= 0 && slot < 4) ? slot : 0;
    state_ = State::Aiming;
    aimAngle_ = 45.0f;
    aimForce_ = 50.0f;
    scoreTimer_ = 0.0f;

    char path[256];
    std::snprintf(path, sizeof(path), "%s/ice_arena.ttm", root);
    assetBatch_.load(arenaModel_, path);
    std::snprintf(path, sizeof(path), "%s/ice_backdrop.ttm", root);
    assetBatch_.load(backdropModel_, path);
    std::snprintf(path, sizeof(path), "%s/ice_tire.ttm", root);
    assetBatch_.load(tireModel_, path);
    std::snprintf(path, sizeof(path), "%s/ice_score_circle.ttm", root);
    assetBatch_.load(scoreCircleModel_, path);
    std::snprintf(path, sizeof(path), "%s/ice_barrel.ttm", root);
    assetBatch_.load(barrelModel_, path);
    std::snprintf(path, sizeof(path), "%s/ice_tnt.ttm", root);
    assetBatch_.load(tntModel_, path);

    return true;
}

void IceGame::shutdown() {
    assetBatch_.clear();
    arenaModel_.unload();
    backdropModel_.unload();
    tireModel_.unload();
    scoreCircleModel_.unload();
    barrelModel_.unload();
    tntModel_.unload();
}

void IceGame::setupStartOfMatch() {
    // IceGameGlobals.NumTreasures / NumPenalties / Obstacles by hood (MinWall -20, -15; MaxWall 20, 15).
    int nt = 2, np = 0;
    struct P { float x, y; };
    static constexpr P kDD[] = {{0, 0}}, kDG[] = {{-10, 0}, {10, 0}}, kMM[] = {{0, -7.5f}, {0, 7.5f}},
                       kBR[] = {{-10, 0}, {10, 0}, {0, -7.5f}, {0, 7.5f}},
                       kDL[] = {{-10, -7.5f}, {-10, 7.5f}, {10, -7.5f}, {10, 7.5f}};
    const P* obstacles = nullptr;
    int numObstacles = 0;
    switch (hood_) {
    case 1000: np = 1; obstacles = kDD; numObstacles = 1; break;
    case 5000: np = 1; obstacles = kDG; numObstacles = 2; break;
    case 4000: np = 1; obstacles = kMM; numObstacles = 2; break;
    case 3000: nt = 1; np = 2; obstacles = kBR; numObstacles = 4; break;
    case 9000: nt = 1; np = 2; obstacles = kDL; numObstacles = 4; break;
    default: break;
    }
    // RandomNumGen.randrange(start, stop) = start + __rand(stop - start), __rand(N) =
    // (Mersenne.getUint31() * N) >> 31.
    auto randrange = [this](int start, int stop) {
        const uint64_t u31 = rng_.gen32() >> 1;
        return start + int((u31 * uint64_t(stop - start)) >> 31);
    };
    constexpr float kMargin = icegame::kTireRadius + 1.0f; // treasureMargin
    auto clear = [&](float x, float y, const Item* list, int n) {
        for (int i = 0; i < n; ++i)
            if (std::hypot(x - list[i].x, y - list[i].y) < kMargin) return false;
        return true;
    };
    auto place = [&](Item* list, int* count, int want) {
        *count = 0;
        while (*count < want && *count < kMaxItems) {
            const float x = float(randrange(-15, 15)), y = float(randrange(-10, 10));
            bool good = true;
            for (int i = 0; i < numObstacles && good; ++i)
                good = std::hypot(x - obstacles[i].x, y - obstacles[i].y) >= kMargin;
            good = good && clear(x, y, treasures_, numTreasures_) && clear(x, y, list, *count);
            if (good) list[(*count)++] = {x, y, false};
        }
    };
    place(treasures_, &numTreasures_, nt);
    place(penalties_, &numPenalties_, np);
    setupMatch_ = sim_.curMatch;
}

void IceGame::onSetMatchAndRound(uint8_t match, uint8_t round) {
    if (sim_.curMatch != match) sim_.resetRound();
    sim_.curMatch = match;
    sim_.curRound = round;
    state_ = State::Aiming;
    if (round == 0 && setupMatch_ != match) setupStartOfMatch(); // enterInputChoice, curRound 0
}

void IceGame::onServer(net::EventType event, const net::Minigame& game) {
    if (event == net::EventType::IceTireInputs) {
        for (int i = 0; i < game.iceInputCount; ++i)
            sim_.setChoice(i, game.iceChoicesForce[i], game.iceChoicesDir[i]);
        state_ = State::Sliding;
    } else if (event == net::EventType::IceFinalPositions) {
        for (int i = 0; i < game.icePositionCount; ++i) {
            sim_.tires[i].x = game.iceFinalPositions[i][0];
            sim_.tires[i].y = game.iceFinalPositions[i][1];
            sim_.tires[i].moving = false;
        }
    } else if (event == net::EventType::IceScores) {
        for (int i = 0; i < game.iceScoreCount; ++i) sim_.scores[i] = game.iceScores[i];
    } else if (event == net::EventType::IceTreasureGrabbed) {
        if (game.iceGrabbedItem < uint32_t(numTreasures_)) treasures_[game.iceGrabbedItem].grabbed = true;
    } else if (event == net::EventType::IcePenaltyGrabbed) {
        if (game.iceGrabbedItem < uint32_t(numPenalties_)) penalties_[game.iceGrabbedItem].grabbed = true;
    } else if (event == net::EventType::IceNewState) {
        if (!std::strcmp(game.iceState, "inputChoice")) {
            state_ = State::Aiming;
            if (sim_.curRound == 0 && setupMatch_ != sim_.curMatch) setupStartOfMatch();
        }
        else if (!std::strcmp(game.iceState, "scoring")) {
            state_ = State::Scoring;
            scoreTimer_ = 0;
        } else if (!std::strcmp(game.iceState, "finalResults")) sim_.isDone = true;
    }
}

void IceGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)nowMs;

    if (state_ == State::Aiming) {
        // Aim angle & force using Circle Pad
        float dx = (float)pad.dx / 156.0f;
        float dy = (float)pad.dy / 156.0f;
        if (std::fabs(dx) > 0.15f) {
            aimAngle_ -= dx * 60.0f * dt;
        }
        if (std::fabs(dy) > 0.15f) {
            aimForce_ += dy * 40.0f * dt;
            if (aimForce_ < 10.0f) aimForce_ = 10.0f;
            if (aimForce_ > 100.0f) aimForce_ = 100.0f;
        }

        if (keysDown & KEY_A) {
            client.sendIceChoice(aimForce_, aimAngle_);
            state_ = State::WaitChoices; // wait for the AI's setTireInputs
        }
    } else if (state_ == State::Sliding) {
        sim_.stepPhysics(dt);
        // toonHitSomething: our tire's sphere (TireRadius) meets an IceTreasure sphere (radius 1).
        const icegame::Tire& mine = sim_.tires[slot_];
        for (int pass = 0; pass < 2; ++pass) {
            Item* list = pass ? penalties_ : treasures_;
            const int n = pass ? numPenalties_ : numTreasures_;
            for (int i = 0; i < n; ++i) {
                if (list[i].grabbed || std::hypot(mine.x - list[i].x, mine.y - list[i].y) >= icegame::kTireRadius + 1.0f)
                    continue;
                list[i].grabbed = true;
                if (pass) client.sendIceClaimPenalty(uint8_t(i));
                else client.sendIceClaimTreasure(uint8_t(i));
            }
        }
        if (sim_.allTiresStopped()) {
            float positions[12] = {};
            for (int i = 0; i < sim_.numPlayers; ++i) {
                positions[i*3] = sim_.tires[i].x;
                positions[i*3+1] = sim_.tires[i].y;
                positions[i*3+2] = icegame::kTireRadius;
            }
            client.sendIceEndingPositions(positions, sim_.numPlayers);
            state_ = State::WaitEnding;
        }
    } else if (state_ == State::Scoring) {
        scoreTimer_ += dt;
        // IceGameGlobals.ShowScoresDuration; the AI owns match/round increments.
        if (scoreTimer_ >= 4.0f && client.sendIceScoringMovieDone()) state_ = State::WaitRound;
    }
}

void IceGame::draw(render::Renderer& renderer) {
    C3D_Mtx world;
    if (arenaModel_.loaded()) {
        Mtx_Identity(&world);
        renderer.drawModel(arenaModel_, world);
    }
    if (backdropModel_.loaded()) {
        Mtx_Identity(&world);
        renderer.drawModel(backdropModel_, world);
    }

    // IceTreasure: the barrel / TNT copy at the spot, z 0, until grabbed (showGrab).
    for (int pass = 0; pass < 2; ++pass) {
        render::Model& m = pass ? tntModel_ : barrelModel_;
        const Item* list = pass ? penalties_ : treasures_;
        const int n = pass ? numPenalties_ : numTreasures_;
        if (!m.loaded()) continue;
        for (int i = 0; i < n; ++i) {
            if (list[i].grabbed) continue;
            Mtx_Identity(&world);
            Mtx_Translate(&world, list[i].x, list[i].y, 0.0f, true);
            renderer.drawModel(m, world);
        }
    }

    // Every player's tire: the model's base sits on the rink (DistributedIceWorld.createTire
    // puts the holder at TireRadius and the model at -TireRadius + 0.01).
    if (tireModel_.loaded()) {
        for (int i = 0; i < sim_.numPlayers && i < 4; ++i) {
            Mtx_Identity(&world);
            Mtx_Translate(&world, sim_.tires[i].x, sim_.tires[i].y, kTireBaseZ, true);
            renderer.drawModel(tireModel_, world);
        }
    }

    // The score circle grows from 0.01 at ExpandFeetPerSec to the farthest scoring tire, at
    // z = TireRadius / 2, half alpha (DistributedIceGame.setScores / scoreCircle).
    if (state_ == State::Scoring && scoreCircleModel_.loaded()) {
        float far = 0.0f;
        for (int i = 0; i < sim_.numPlayers && i < 4; ++i) {
            const float d = sim_.tires[i].distanceToCenter();
            if (d > far) far = d;
        }
        float radius = kCircleStartScale + kExpandFeetPerSec * scoreTimer_;
        if (radius > far) radius = far;
        Mtx_Identity(&world);
        Mtx_Translate(&world, 0.0f, 0.0f, kScoreCircleZ, true);
        Mtx_Scale(&world, radius, radius, 1.0f);
        renderer.setBaseTint(1.0f, 1.0f, 1.0f, 0.5f);
        renderer.drawModel(scoreCircleModel_, world);
        renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void IceGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    // DistributedIceGame.cameraThreeQuarterView = (0, -22, 45, 0, -62.89, 0): a 50-unit look
    // along the pitched view direction gives the target.
    const float pitch = kCameraPitchDeg * 3.14159265f / 180.0f;
    if (eye) {
        eye->x = 0.0f;
        eye->y = kCameraEyeY;
        eye->z = kCameraEyeZ;
        eye->w = 1.0f;
    }
    if (target) {
        target->x = 0.0f;
        target->y = kCameraEyeY + std::cos(pitch) * kLookDistance;
        target->z = kCameraEyeZ - std::sin(pitch) * kLookDistance;
        target->w = 1.0f;
    }
}

void IceGame::tirePose(int index, float* x, float* y) const {
    if (index < 0 || index >= 4) index = 0;
    if (x) *x = sim_.tires[index].x;
    if (y) *y = sim_.tires[index].y;
}

void IceGame::toonPose(float* x, float* y, float* z, float* h) const {
    // DistributedIceGame.forceLocalToonToTire: the local toon rides its own tire.
    if (x) *x = sim_.tires[slot_].x;
    if (y) *y = sim_.tires[slot_].y;
    if (z) *z = 0.8f;
    if (h) *h = aimAngle_;
}

const char* IceGame::line(int i) const {
    if (i == 0) {
        std::snprintf(lines_[0], sizeof(lines_[0]), "Match %d / %d", sim_.curMatch + 1, icegame::kNumMatches);
        return lines_[0];
    }
    if (i == 1) {
        if (state_ == State::Aiming) {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Angle: %.0f deg  Force: %.0f%% (A: Launch)", aimAngle_, aimForce_);
        } else if (state_ == State::Sliding) {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Sliding... Dist: %.1f", sim_.tires[0].distanceToCenter());
        } else {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Scored! Dist: %.1f", sim_.tires[0].distanceToCenter());
        }
        return lines_[1];
    }
    if (i == 2) {
        std::snprintf(lines_[2], sizeof(lines_[2]), "Score: %d beans", sim_.scores[0]);
        return lines_[2];
    }
    return "";
}

} // namespace tt::game
