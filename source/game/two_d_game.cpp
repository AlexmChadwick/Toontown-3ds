#include "game/two_d_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace tt::game {

bool TwoDGame::init(const char* root) {
    char path[256];
    std::snprintf(path, sizeof(path), "%s/minigames/toonblitz_game.ttm", root);
    assetBatch_.load(gameModel_, path);
    std::snprintf(path, sizeof(path), "%s/minigames/toonblitz_block.ttm", root);
    assetBatch_.load(blockModel_, path);

    player_ = twodgame::PlayerPhysics{};
    player_.x = 0.0f;
    player_.z = 0.0f;

    // A few test platforms
    numPlatforms_ = 4;
    platforms_[0] = {20.0f, 0.0f, 40.0f, 6.0f};
    platforms_[1] = {50.0f, 0.0f, 75.0f, 12.0f};
    platforms_[2] = {85.0f, 0.0f, 110.0f, 18.0f};
    platforms_[3] = {120.0f, 0.0f, 150.0f, 24.0f};

    rawScore_ = 0;
    score_ = 0;
    timer_ = 0.0f;
    completed_ = false;
    isDone_ = false;

    TT_LOG("twod game: init platforms=%d", numPlatforms_);
    return true;
}

void TwoDGame::shutdown() {
    assetBatch_.clear();
    gameModel_.unload();
    blockModel_.unload();
    isDone_ = true;
}

void TwoDGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)nowMs;
    if (isDone_) return;

    timer_ += dt;
    float moveX = pad.dx / 156.0f;
    if (keysDown & KEY_A) {
        player_.jump();
    }
    player_.step(moveX, platforms_, numPlatforms_, dt);

    // End of course (reach x >= 140)
    if (player_.x >= 140.0f && !completed_) {
        completed_ = true;
        isDone_ = true;
        float timeLeft = twodgame::kGameDurationTTC - timer_;
        score_ = twodgame::calculateJellybeans(rawScore_, timeLeft, true);
        client.sendTwoDReportDone();
        TT_LOG("twod game: completed course time=%.1f score=%d", timer_, score_);
    } else if (timer_ >= twodgame::kGameDurationTTC) {
        isDone_ = true;
        score_ = twodgame::calculateJellybeans(rawScore_, 0.0f, false);
        TT_LOG("twod game: time out score=%d", score_);
    }
}

void TwoDGame::draw(render::Renderer& renderer) {
    C3D_Mtx model;
    Mtx_Identity(&model);
    if (gameModel_.loaded()) renderer.drawModel(gameModel_, model);

    if (blockModel_.loaded()) {
        for (int i = 0; i < numPlatforms_; i++) {
            Mtx_Identity(&model);
            Mtx_Translate(&model, (platforms_[i].x1 + platforms_[i].x2) * 0.5f, 0.0f, platforms_[i].z2, false);
            renderer.drawModel(blockModel_, model);
        }
    }
}

void TwoDGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (!eye || !target) return;
    eye->x = player_.x;
    eye->y = -45.0f;
    eye->z = player_.z + 6.0f;
    target->x = player_.x;
    target->y = 0.0f;
    target->z = player_.z + 4.0f;
}

void TwoDGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = player_.x;
    if (y) *y = 0.0f;
    if (z) *z = player_.z;
    if (h) *h = player_.facingRight ? 90.0f : -90.0f;
}

const char* TwoDGame::line(int i) const {
    if (i < 0 || i >= 3) return "";
    switch (i) {
    case 0:
        std::snprintf(lines_[0], sizeof(lines_[0]), "Progress: %d%%", static_cast<int>((player_.x / 140.0f) * 100.0f));
        break;
    case 1:
        std::snprintf(lines_[1], sizeof(lines_[1]), "Circle Pad: Run  A: Jump");
        break;
    case 2: {
        int left = static_cast<int>(twodgame::kGameDurationTTC - timer_);
        std::snprintf(lines_[2], sizeof(lines_[2]), "Time: %d s", left > 0 ? left : 0);
        break;
    }
    }
    return lines_[i];
}

} // namespace tt::game
