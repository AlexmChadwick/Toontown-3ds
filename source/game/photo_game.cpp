#include "game/minigame_limits.h"
#include "core/log.h"
#include "game/photo_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tt::game {

namespace {

// DistributedPhotoGame.__localPhotoMoveTask reads LEFT/RIGHT/UP/DOWN as held keys; the pad is
// analog, so it scales the same velocities. Deadzone matches the earlier pad handling.
constexpr float kPadDeadzone = 0.15f;
constexpr float kPadFull = 156.0f;
// Snap rate limit for autoplay only; a human presses A at most once per shot.
constexpr float kAutoShotCooldownSec = 1.0f;
constexpr float kViewDistance = 20.0f;

float padAxis(int v) {
    const float f = static_cast<float>(v) / kPadFull;
    return std::fabs(f) > kPadDeadzone ? f : 0.0f;
}

} // namespace

bool PhotoGame::init(const char* root, unsigned hood, uint32_t gameDoId) {
    (void)root;
    const auto* data = photogame::getHoodPhotoData(hood);
    sim_.init(1, hood, gameDoId);
    swivel_.h = data->startH;
    swivel_.p = data->startP;
    if (data->hasRoot) {
        // constructXX sets photoRoot; __createTripod puts the tripod at photoRoot + TRIPOD_OFFSET,
        // and the camera sits on the swivel pivot above it.
        eyeX_ = data->rootX + data->tripOffsetX;
        eyeY_ = data->rootY + data->tripOffsetY;
        eyeZ_ = data->rootZ + data->tripOffsetZ + photogame::kTripodPivotZ;
    } else {
        // No upstream root for this hood: fall back to the intro camera position.
        eyeX_ = data->camPosX + data->tripOffsetX;
        eyeY_ = data->camPosY + data->tripOffsetY;
        eyeZ_ = data->camPosZ + data->tripOffsetZ;
        TT_LOG("mgpass: photo hood=%u has no photoRoot; using the intro camera", hood);
    }
    timeRemaining_ = static_cast<float>(data->timeLimit);
    elapsed_ = 0.0f;
    shotCooldown_ = 0.0f;
    TT_LOG("mgpass: photo start hood=%u film=%d tripod=%.2f,%.2f,%.2f", hood, sim_.maxFilm, eyeX_, eyeY_, eyeZ_);

    return true;
}

void PhotoGame::shutdown() {
    assetBatch_.clear();
    sceneModel_.unload();
    subjectModel_.unload();
}

void PhotoGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, u32 keysHeld, float dt,
                       u32 nowMs, bool autoAim) {
    (void)nowMs;

    if (sim_.isDone) return;

    timeRemaining_ -= dt;
    elapsed_ += dt;
    if (timeRemaining_ <= 0.0f) {
        sim_.calculateScores();
        sim_.isDone = true;
        return;
    }
    shotCooldown_ -= dt;

    bool fire = false;
    if (autoAim) {
        // planAutoShot steers the swivel itself and reports when it is on a matching subject.
        const bool aligned = photogame::planAutoShot(sim_, eyeX_, eyeY_, eyeZ_, swivel_, elapsed_, dt);
        fire = aligned && shotCooldown_ <= 0.0f;
    } else {
        // Circle Pad: right turns right (negative heading), up looks up, as upstream's RIGHT/UP.
        float turn = -padAxis(pad.dx);
        float tilt = padAxis(pad.dy);
        if (keysHeld & KEY_DLEFT) turn += 1.0f;
        if (keysHeld & KEY_DRIGHT) turn -= 1.0f;
        if (keysHeld & KEY_DUP) tilt += 1.0f;
        if (keysHeld & KEY_DDOWN) tilt -= 1.0f;
        photogame::stepSwivel(swivel_, turn, tilt, dt);
        fire = (keysDown & KEY_A) != 0;
    }

    if (fire && sim_.filmUsed[0] < sim_.maxFilm) {
        const photogame::PhotoShotResult shot =
            sim_.evaluateShot(0, eyeX_, eyeY_, eyeZ_, swivel_.h, swivel_.p, elapsed_);
        shotCooldown_ = kAutoShotCooldownSec;
        if (shot.assignmentIndex >= 0 && shot.newHighScore) {
            client.sendPhotoScore(static_cast<uint8_t>(shot.subjectIndex), shot.poseName, shot.stars);
            TT_LOG("mgpass: photo score subject=%d pose=%s stars=%.0f", shot.subjectIndex, shot.poseName, shot.stars);
        }
        // Upstream sends filmOut once, when the last frame is used (__testCollisions).
        if (sim_.filmUsed[0] == sim_.maxFilm) {
            client.sendPhotoFilmOut();
            TT_LOG("mgpass: photo film out");
        }
    }
}

void PhotoGame::onAIScore(uint32_t avId, uint8_t assignment, float stars) {
    if (sim_.submitAIScore(avId, 0, assignment, stars)) {
        TT_LOG("mgpass: photo AI score assignment=%u stars=%.0f", assignment, stars);
    }
}

void PhotoGame::draw(render::Renderer& renderer) {
    (void)renderer; // the owning scene draws its real playground, not camera props as subjects
}

void PhotoGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (eye) {
        eye->x = eyeX_;
        eye->y = eyeY_;
        eye->z = eyeZ_;
        eye->w = 1.0f;
    }
    if (target) {
        // Same forward vector as Simulation::evaluateShot (Panda heading and pitch).
        const float yawRad = swivel_.h * 0.0174532925f;
        const float pitchRad = swivel_.p * 0.0174532925f;
        target->x = eyeX_ - std::sin(yawRad) * std::cos(pitchRad) * kViewDistance;
        target->y = eyeY_ + std::cos(yawRad) * std::cos(pitchRad) * kViewDistance;
        target->z = eyeZ_ + std::sin(pitchRad) * kViewDistance;
        target->w = 1.0f;
    }
}

void PhotoGame::toonPose(float* x, float* y, float* z, float* h) const {
    // Upstream __loadToonInTripod reparents the local toon to the swivel: same origin and heading.
    if (x) *x = eyeX_;
    if (y) *y = eyeY_;
    if (z) *z = eyeZ_;
    if (h) *h = swivel_.h;
}

const char* PhotoGame::line(int i) const {
    if (i == 0) {
        std::snprintf(lines_[0], sizeof(lines_[0]), "Film: %d / %d  Time: %.0fs", sim_.maxFilm - sim_.filmUsed[0], sim_.maxFilm, timeRemaining_);
        return lines_[0];
    }
    if (i == 1) {
        std::snprintf(lines_[1], sizeof(lines_[1]), "Heading %.0f  Tilt %.0f  (A: snap)", swivel_.h, swivel_.p);
        return lines_[1];
    }
    if (i == 2) {
        std::snprintf(lines_[2], sizeof(lines_[2]), "Frame the assigned subject and pose");
        return lines_[2];
    }
    return "";
}

} // namespace tt::game
