#include "game/minigame_limits.h"
#include "core/log.h"
#include "game/photo_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tt::game {

bool PhotoGame::init(const char* root, unsigned hood) {
    (void)root;
    sim_.init(1, 32);
    camYaw_ = -90.0f;
    camPitch_ = 0.0f;
    timeRemaining_ = 120.0f;
    for (const auto& limits : kMinigameLimits) {
        if (limits.hood == hood) {
            sim_.maxFilm = limits.film;
            timeRemaining_ = float(limits.photoSeconds);
        }
    }
    TT_LOG("mgpass: limitation id 16: animated photo assignments are not implemented; no fabricated scores");

    return true;
}

void PhotoGame::shutdown() {
    assetBatch_.clear();
    sceneModel_.unload();
    subjectModel_.unload();
}

void PhotoGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)nowMs;

    if (sim_.isDone) return;

    timeRemaining_ -= dt;
    if (timeRemaining_ <= 0.0f) {
        sim_.calculateScores();
        sim_.isDone = true;
        return;
    }

    // Camera pan/tilt
    float dx = (float)pad.dx / 156.0f;
    float dy = (float)pad.dy / 156.0f;
    if (std::fabs(dx) > 0.15f) {
        camYaw_ += dx * 50.0f * dt;
    }
    if (std::fabs(dy) > 0.15f) {
        camPitch_ += dy * 35.0f * dt;
        if (camPitch_ < -25.0f) camPitch_ = -25.0f;
        if (camPitch_ > 25.0f) camPitch_ = 25.0f;
    }

    // Snap photo
    if (keysDown & KEY_A) {
        if (sim_.filmUsed[0] < sim_.maxFilm) {
            // A snapshot with no implemented subject/pose cannot earn invented stars.
            // DistributedPhotoGame.__testCollisions still consumes film for empty shots.
            ++sim_.filmUsed[0];
            if (sim_.filmUsed[0] == sim_.maxFilm) client.sendPhotoFilmOut();
        }
    }
}

void PhotoGame::draw(render::Renderer& renderer) {
    (void)renderer; // the owning scene draws its real playground, not camera props as subjects
}

void PhotoGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (eye) {
        eye->x = 0.0f;
        eye->y = 0.0f;
        eye->z = 5.0f;
        eye->w = 1.0f;
    }
    if (target) {
        float yawRad = camYaw_ * 0.0174532925f;
        float pitchRad = camPitch_ * 0.0174532925f;
        target->x = std::cos(yawRad) * std::cos(pitchRad) * 20.0f;
        target->y = std::sin(yawRad) * std::cos(pitchRad) * 20.0f;
        target->z = 5.0f + std::sin(pitchRad) * 20.0f;
        target->w = 1.0f;
    }
}

void PhotoGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    if (z) *z = 0.0f;
    if (h) *h = camYaw_;
}

const char* PhotoGame::line(int i) const {
    if (i == 0) {
        std::snprintf(lines_[0], sizeof(lines_[0]), "Film: %d / %d  Time: %.0fs", sim_.maxFilm - sim_.filmUsed[0], sim_.maxFilm, timeRemaining_);
        return lines_[0];
    }
    if (i == 1) {
        std::snprintf(lines_[1], sizeof(lines_[1]), "Aim: Yaw %.0f Pitch %.0f (A: Snap Photo)", camYaw_, camPitch_);
        return lines_[1];
    }
    if (i == 2) {
        std::snprintf(lines_[2], sizeof(lines_[2]), "Subject paths unavailable; score from AI");
        return lines_[2];
    }
    return "";
}

} // namespace tt::game
