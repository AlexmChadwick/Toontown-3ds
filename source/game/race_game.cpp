#include "game/race_game.h"

#include <cstdio>
#include <cstring>

namespace tt::game {

bool RaceGame::init(const char* root) {
    sim_.init(1);
    selectedChoice_ = 1;
    choiceSubmitted_ = false;
    timer_ = 0.0f;

    char path[256];
    std::snprintf(path, sizeof(path), "%s/race_track.ttm", root);
    trackModel_.load(path);

    return true;
}

void RaceGame::shutdown() {
    trackModel_.unload();
}

void RaceGame::onSetChancePositions(const uint8_t* positions, int count) {
    for (int i = 0; i < count && i < 4; ++i) {
        sim_.setChance(i, positions[i], 19); // default reward
    }
}

void RaceGame::onSetServerChoices(const uint8_t* choices, const uint8_t* positions, const int16_t* rewards, int count) {
    (void)choices;
    (void)rewards;
    for (int i = 0; i < count && i < 4; ++i) {
        sim_.positions[i] = positions[i];
    }
    choiceSubmitted_ = false;
}

void RaceGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)pad;
    (void)dt;
    (void)nowMs;

    if (sim_.isDone) return;

    if (!choiceSubmitted_) {
        if (keysDown & (KEY_DLEFT | KEY_Y)) {
            selectedChoice_ = (selectedChoice_ > 0) ? selectedChoice_ - 1 : 4;
        } else if (keysDown & (KEY_DRIGHT | KEY_A)) {
            selectedChoice_ = (selectedChoice_ < 4) ? selectedChoice_ + 1 : 0;
        }

        if (keysDown & (KEY_A | KEY_START)) {
            choiceSubmitted_ = true;
            client.sendRaceChoice(selectedChoice_);

            // If offline, advance solo immediately
            int ch[4] = {selectedChoice_, 0, 0, 0};
            int rewards[4];
            sim_.processChoices(ch, rewards);
            choiceSubmitted_ = false;
        }
    }
}

void RaceGame::draw(render::Renderer& renderer) {
    if (trackModel_.loaded()) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        renderer.drawModel(trackModel_, world);
    }
}

void RaceGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    float x = (float)sim_.positions[0] * 5.0f;
    if (eye) {
        eye->x = x;
        eye->y = -25.0f;
        eye->z = 15.0f;
        eye->w = 1.0f;
    }
    if (target) {
        target->x = x;
        target->y = 0.0f;
        target->z = 2.0f;
        target->w = 1.0f;
    }
}

void RaceGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = (float)sim_.positions[0] * 5.0f;
    if (y) *y = 0.0f;
    if (z) *z = 0.0f;
    if (h) *h = 90.0f;
}

const char* RaceGame::line(int i) const {
    if (i == 0) {
        std::snprintf(lines_[0], sizeof(lines_[0]), "Position: %d / %d", sim_.positions[0], racegame::kNumberToWin);
        return lines_[0];
    }
    if (i == 1) {
        std::snprintf(lines_[1], sizeof(lines_[1]), "Select Move: %d spaces (Left/Right, A: Move)", selectedChoice_);
        return lines_[1];
    }
    if (i == 2) {
        std::snprintf(lines_[2], sizeof(lines_[2]), "Score: %d beans", sim_.scores[0]);
        return lines_[2];
    }
    return "";
}

} // namespace tt::game
