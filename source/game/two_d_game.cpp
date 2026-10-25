#include "game/minigame_limits.h"
#include "game/two_d_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace tt::game {

bool TwoDGame::init(const char* root, unsigned hood) {
    (void)root;
    duration_ = 150;
    for (const auto& limits : kMinigameLimits)
        if (limits.hood == hood) duration_ = float(limits.twoDSeconds);
    // DistributedTwoDGameAI.setupSections supplies the actual section layout.
    // The old four "test platforms" and x=140 finish were invented content.
    // Until section geometry is implemented, report only the stock timer expiry.
    player_ = twodgame::PlayerPhysics{};
    player_.x = -39;
    player_.z = 13.59f; // ToonBlitzGlobals.ToonStartingPosition
    numPlatforms_ = 0;
    rawScore_ = 0;
    score_ = 0;
    timer_ = 0.0f;
    completed_ = false;
    isDone_ = false;

    TT_LOG("mgpass: limitation id 15: upstream section layout not implemented; no fabricated course or victory");
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
    (void)pad;
    (void)keysDown;
    if (timer_ >= duration_) {
        isDone_ = true;
        client.sendTwoDReportDone(); // DistributedTwoDGame.timerExpired, no toonVictory
        TT_LOG("twod game: timer expired without a completed course");
    }
}

void TwoDGame::draw(render::Renderer& renderer) {
    (void)renderer; // never present ice_game_2d or a Sellbot sign as a Toon Escape course
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
        std::snprintf(lines_[0], sizeof(lines_[0]), "Course sections not implemented");
        break;
    case 1:
        std::snprintf(lines_[1], sizeof(lines_[1]), "Waiting for the game timer...");
        break;
    case 2: {
        int left = static_cast<int>(duration_ - timer_);
        std::snprintf(lines_[2], sizeof(lines_[2]), "Time: %d s", left > 0 ? left : 0);
        break;
    }
    }
    return lines_[i];
}

} // namespace tt::game
