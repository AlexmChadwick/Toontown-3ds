#include "game/target_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace tt::game {

bool TargetGame::init(const char* root, uint32_t seed, uint32_t safezone) {
    char path[256];
    std::snprintf(path, sizeof(path), "%starget_ground.ttm", root);
    assetBatch_.load(groundModel_, path);
    std::snprintf(path, sizeof(path), "%starget_sling.ttm", root);
    assetBatch_.load(slingModel_, path);
    std::snprintf(path, sizeof(path), "%starget_tramp.ttm", root);
    assetBatch_.load(trampModel_, path);

    numTargets_ = targetgame::setupTargets(seed, safezone, targets_, targetgame::kMaxTargets);
    flight_ = targetgame::FlightState{};
    flight_.mode = targetgame::FlightMode::Aim;
    score_ = 0;
    round_ = 1;
    isDone_ = false;
    scoreReported_ = false;

    TT_LOG("target game: init seed=%lu safezone=%lu targets=%d", static_cast<unsigned long>(seed), static_cast<unsigned long>(safezone), numTargets_);
    return true;
}

void TargetGame::nextRound() {
    // DistributedTargetGame.setRoundDone: reset the slingshot for the AI's next round.
    ++round_;
    flight_ = targetgame::FlightState{};
    scoreReported_ = false;
    isDone_ = false;
}

void TargetGame::shutdown() {
    assetBatch_.clear();
    groundModel_.unload();
    slingModel_.unload();
    trampModel_.unload();
    isDone_ = true;
}

void TargetGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)nowMs;
    float steerX = pad.dx / 156.0f;
    float steerY = pad.dy / 156.0f;
    if (std::abs(steerX) < 0.15f) steerX = 0.0f;
    if (std::abs(steerY) < 0.15f) steerY = 0.0f;

    if (flight_.mode == targetgame::FlightMode::Aim) {
        flight_.step(steerX, steerY, dt);
        if (keysDown & KEY_A) {
            flight_.launch();
            TT_LOG("target game: launched power=%.1f lateral=%.1f", flight_.power, flight_.aimLateral);
        }
    } else if (flight_.mode == targetgame::FlightMode::Fly) {
        if ((keysDown & KEY_A) || (keysDown & KEY_DDOWN) || steerY < -0.5f) {
            flight_.openUmbrella();
            TT_LOG("target game: umbrella opened at y=%.1f z=%.1f", flight_.y, flight_.z);
        }
        flight_.step(steerX, steerY, dt);
    } else if (flight_.mode == targetgame::FlightMode::Fall) {
        flight_.step(steerX, steerY, dt);
        if (flight_.mode == targetgame::FlightMode::Landed && !scoreReported_) {
            scoreReported_ = true;
            score_ = targetgame::evaluateScore(targets_, numTargets_, flight_.x, flight_.y);
            client.sendTargetScore(static_cast<int32_t>(flight_.x), static_cast<int32_t>(flight_.y));
            client.sendTargetPlayerDone();
            isDone_ = true;
            TT_LOG("target game: landed at (%.1f, %.1f) score=%d", flight_.x, flight_.y, score_);
        }
    }
}

void TargetGame::draw(render::Renderer& renderer) {
    C3D_Mtx model;
    Mtx_Identity(&model);
    if (groundModel_.loaded()) renderer.drawModel(groundModel_, model);

    if (slingModel_.loaded()) {
        Mtx_Identity(&model);
        Mtx_Translate(&model, 0.0f, 0.0f, 0.0f, false);
        renderer.drawModel(slingModel_, model);
    }

    if (trampModel_.loaded()) {
        Mtx_Identity(&model);
        Mtx_Translate(&model, 0.0f, 20.0f, 0.0f, false);
        renderer.drawModel(trampModel_, model);
    }
}

// The stock camera of each phase, without its 2.5 s blends: enterPrelaunch toon + (0, -25, 7)
// at p -15, enterFly riding the Toon at (0, CAMERA_Y + 12, 5) p -15, enterFall (0, -10, 15)
// p -68; camLens.setFov(80) throughout (MinigameSession::applyWorld).
void TargetGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (!eye || !target) return;
    float dy = -25.0f, dz = 7.0f, pitch = -15.0f;
    if (flight_.mode == targetgame::FlightMode::Fly) dy = -13.0f, dz = 5.0f;
    else if (flight_.mode != targetgame::FlightMode::Aim) dy = -10.0f, dz = 15.0f, pitch = -68.0f;
    const float p = pitch * 0.0174532925f;
    *eye = FVec4_New(flight_.x, flight_.y + dy, flight_.z + dz, 1.0f);
    *target = FVec4_New(eye->x, eye->y + std::cos(p) * 20.0f, eye->z + std::sin(p) * 20.0f, 1.0f);
}

void TargetGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = flight_.x;
    if (y) *y = flight_.y;
    if (z) *z = flight_.z;
    if (h) *h = 0.0f; // the stock Toon keeps heading 0 in every phase
}

const char* TargetGame::line(int i) const {
    if (i < 0 || i >= 3) return "";
    switch (i) {
    case 0:
        if (flight_.mode == targetgame::FlightMode::Aim) {
            std::snprintf(lines_[0], sizeof(lines_[0]), "Power: %d%%", static_cast<int>((flight_.power / 120.0f) * 100.0f));
        } else {
            std::snprintf(lines_[0], sizeof(lines_[0]), "Distance: %d ft", static_cast<int>(flight_.y));
        }
        break;
    case 1:
        if (flight_.mode == targetgame::FlightMode::Aim) {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Press A to Launch!");
        } else if (flight_.mode == targetgame::FlightMode::Fly) {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Press A to Open Umbrella!");
        } else if (flight_.mode == targetgame::FlightMode::Fall) {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Circle Pad to Glide");
        } else {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Score: %d", score_);
        }
        break;
    case 2:
        std::snprintf(lines_[2], sizeof(lines_[2]), "Round: %d/3", round_);
        break;
    }
    return lines_[i];
}

} // namespace tt::game
