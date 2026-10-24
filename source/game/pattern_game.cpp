#include "game/pattern_game.h"

#include <cstdio>
#include <cstring>

namespace tt::game {

bool PatternGame::init(const char* root) {
    sim_.init();
    timer_ = 0.0f;
    roundTime_ = 0.0f;
    waitingForPattern_ = true;
    patternSubmitted_ = false;

    char path[256];
    std::snprintf(path, sizeof(path), "%s/pattern_stage.ttm", root);
    stageModel_.load(path);

    return true;
}

void PatternGame::shutdown() {
    stageModel_.unload();
}

void PatternGame::onSetPattern(const uint8_t* pattern, int len) {
    sim_.setTargetPattern(pattern, len);
    roundTime_ = 0.0f;
    waitingForPattern_ = false;
    patternSubmitted_ = false;
}

void PatternGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)pad;
    (void)nowMs;

    if (waitingForPattern_) {
        // If not connected to server, synthesize an offline round after delay
        timer_ += dt;
        if (timer_ > 1.5f && sim_.round < patterngame::kNumRounds) {
            timer_ = 0.0f;
            uint8_t dummy[8];
            int len = 2 + sim_.round * 2;
            for (int i = 0; i < len; ++i) {
                dummy[i] = (uint8_t)(i % 4);
            }
            onSetPattern(dummy, len);
        }
        return;
    }

    roundTime_ += dt;

    if (!patternSubmitted_) {
        int pressed = -1;
        if (keysDown & KEY_DUP) pressed = 0;
        else if (keysDown & KEY_DRIGHT) pressed = 1;
        else if (keysDown & KEY_DDOWN) pressed = 2;
        else if (keysDown & KEY_DLEFT) pressed = 3;
        else if (keysDown & KEY_X) pressed = 0;
        else if (keysDown & KEY_A) pressed = 1;
        else if (keysDown & KEY_B) pressed = 2;
        else if (keysDown & KEY_Y) pressed = 3;

        if (pressed >= 0) {
            bool ok = sim_.inputButton((uint8_t)pressed);
            client.sendPatternButton((uint8_t)pressed, ok ? 0 : 1);

            if (sim_.inputLen >= sim_.targetLen) {
                patternSubmitted_ = true;
                client.sendPatternResult(sim_.inputPattern, (uint8_t)sim_.inputLen, (uint16_t)(roundTime_ * 1000.0f));
                sim_.finishRound(roundTime_, false, false);
                waitingForPattern_ = true;
                timer_ = 0.0f;
            }
        }

        if (roundTime_ >= patterngame::kInputTimeout) {
            patternSubmitted_ = true;
            client.sendPatternResult(sim_.inputPattern, (uint8_t)sim_.inputLen, (uint16_t)(roundTime_ * 1000.0f));
            sim_.finishRound(roundTime_, false, false);
            waitingForPattern_ = true;
            timer_ = 0.0f;
        }
    }
}

void PatternGame::draw(render::Renderer& renderer) {
    if (stageModel_.loaded()) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        renderer.drawModel(stageModel_, world);
    }
}

void PatternGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (eye) {
        eye->x = 0.0f;
        eye->y = -22.0f;
        eye->z = 6.0f;
        eye->w = 1.0f;
    }
    if (target) {
        target->x = 0.0f;
        target->y = 0.0f;
        target->z = 4.0f;
        target->w = 1.0f;
    }
}

void PatternGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    if (z) *z = 0.0f;
    if (h) *h = 180.0f;
}

const char* PatternGame::line(int i) const {
    if (i == 0) {
        std::snprintf(lines_[0], sizeof(lines_[0]), "Round %d / %d", sim_.round, patterngame::kNumRounds);
        return lines_[0];
    }
    if (i == 1) {
        std::snprintf(lines_[1], sizeof(lines_[1]), "Input: %d / %d (time: %.1fs)", sim_.inputLen, sim_.targetLen, roundTime_);
        return lines_[1];
    }
    if (i == 2) {
        std::snprintf(lines_[2], sizeof(lines_[2]), "Score: %d beans", sim_.score);
        return lines_[2];
    }
    return "";
}

} // namespace tt::game
