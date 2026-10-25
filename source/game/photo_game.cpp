#include "game/photo_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tt::game {

bool PhotoGame::init(const char* root) {
    sim_.init(1, 32);
    camYaw_ = -90.0f;
    camPitch_ = 0.0f;
    timeRemaining_ = 60.0f;

    char path[256];
    std::snprintf(path, sizeof(path), "%s/photo_scene.ttm", root);
    assetBatch_.load(sceneModel_, path);
    std::snprintf(path, sizeof(path), "%s/photo_subject.ttm", root);
    assetBatch_.load(subjectModel_, path);

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
            // Check subject in center of frame
            // Let subject 0 be at yaw -90, subject 1 at yaw -45, etc.
            int bestSubject = 0;
            float bestDiff = 999.0f;
            for (int s = 0; s < 5; ++s) {
                float targetYaw = -90.0f + (float)s * 30.0f;
                float diff = std::fabs(camYaw_ - targetYaw);
                if (diff < bestDiff) {
                    bestDiff = diff;
                    bestSubject = s;
                }
            }

            float stars = 1.0f;
            if (bestDiff < 5.0f) stars = 5.0f;
            else if (bestDiff < 10.0f) stars = 4.0f;
            else if (bestDiff < 18.0f) stars = 3.0f;
            else if (bestDiff < 28.0f) stars = 2.0f;

            sim_.submitPhoto(0, bestSubject, 0, stars);
            client.sendPhotoScore(bestSubject, 0, stars);
        }
    }
}

void PhotoGame::draw(render::Renderer& renderer) {
    if (sceneModel_.loaded()) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        renderer.drawModel(sceneModel_, world);
    }

    if (subjectModel_.loaded()) {
        for (int s = 0; s < 5; ++s) {
            float rad = (-90.0f + (float)s * 30.0f) * 0.0174532925f;
            float dist = 25.0f;
            C3D_Mtx world;
            Mtx_Identity(&world);
            Mtx_Translate(&world, std::cos(rad) * dist, std::sin(rad) * dist, 0.0f, true);
            renderer.drawModel(subjectModel_, world);
        }
    }
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
        std::snprintf(lines_[2], sizeof(lines_[2]), "Team Score: %d beans", sim_.scores[0]);
        return lines_[2];
    }
    return "";
}

} // namespace tt::game
