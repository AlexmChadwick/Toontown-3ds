#include "game/minigame_limits.h"
#include "core/log.h"
#include "game/photo_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tt::game {

bool PhotoGame::init(const char* root, unsigned hood, uint32_t gameDoId) {
    (void)root;
    const auto* data = photogame::getHoodPhotoData(hood);
    sim_.init(1, hood, gameDoId);
    camYaw_ = data->startH;
    camPitch_ = data->startP;
    camX_ = data->camPosX + data->tripOffsetX;
    camY_ = data->camPosY + data->tripOffsetY;
    camZ_ = data->camPosZ + data->tripOffsetZ;
    timeRemaining_ = static_cast<float>(data->timeLimit);
    elapsed_ = 0.0f;
    TT_LOG("mgpass: photo start hood=%u film=%d", hood, sim_.maxFilm);

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
    elapsed_ += dt;
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
            const photogame::PhotoShotResult shot = sim_.evaluateShot(0, camX_, camY_, camZ_, camYaw_, camPitch_, elapsed_);
            if (shot.assignmentIndex >= 0 && shot.newHighScore) {
                client.sendPhotoScore(static_cast<uint8_t>(shot.subjectIndex), shot.poseName, shot.stars);
                TT_LOG("mgpass: photo score subject=%d pose=%s stars=%.0f", shot.subjectIndex, shot.poseName, shot.stars);
            }
            if (sim_.filmUsed[0] >= sim_.maxFilm) {
                client.sendPhotoFilmOut();
                TT_LOG("mgpass: photo film out");
            }
            if (sim_.filmUsed[0] == sim_.maxFilm) client.sendPhotoFilmOut();
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
        eye->x = camX_;
        eye->y = camY_;
        eye->z = camZ_;
        eye->w = 1.0f;
    }
    if (target) {
        float yawRad = camYaw_ * 0.0174532925f;
        float pitchRad = camPitch_ * 0.0174532925f;
        target->x = camX_ - std::sin(yawRad) * std::cos(pitchRad) * 20.0f;
        target->y = camY_ + std::cos(yawRad) * std::cos(pitchRad) * 20.0f;
        target->z = camZ_ + std::sin(pitchRad) * 20.0f;
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
        std::snprintf(lines_[2], sizeof(lines_[2]), "Frame the assigned subject and pose");
        return lines_[2];
    }
    return "";
}

} // namespace tt::game
