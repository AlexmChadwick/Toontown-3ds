#include "game/race_game.h"

#include <cstdio>
#include <cstring>

namespace tt::game {

bool RaceGame::init(const char* root) {
    sim_.init(1);
    selectedChoice_ = 1;
    choiceSubmitted_ = false;
    timer_ = 0.0f;
    displayedPosition_ = 0;
    moveStartPosition_ = 0;
    moveEndPosition_ = 0;
    moveTime_ = 0.0f;
    moveDuration_ = 0.0f;

    char path[256];
    std::snprintf(path, sizeof(path), "%s/race_track.ttm", root);
    assetBatch_.load(trackModel_, path);

    return true;
}

void RaceGame::shutdown() {
    assetBatch_.clear();
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
    const int oldPosition = sim_.positions[0];
    for (int i = 0; i < count && i < 4; ++i) {
        sim_.positions[i] = positions[i] > racegame::kNumberToWin ? racegame::kNumberToWin : positions[i];
        if (sim_.positions[i] == racegame::kNumberToWin) sim_.isDone = true;
    }
    moveStartPosition_ = oldPosition;
    moveEndPosition_ = sim_.positions[0];
    moveTime_ = 0.0f;
    const int spaces = std::abs(moveEndPosition_ - moveStartPosition_);
    moveDuration_ = spaces > 4 ? float(spaces) / 4.0f : float(spaces) / 1.2f;
    displayedPosition_ = moveEndPosition_;
    choiceSubmitted_ = false;
}

void RaceGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)pad;
    (void)nowMs;

    if (moveTime_ < moveDuration_) moveTime_ += dt;

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

            // DistributedRaceGame.setServerChoices owns movement and the next turn.

        }
    }
}

void RaceGame::draw(render::Renderer& renderer) {
    if (trackModel_.loaded()) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        // DistributedRaceGame.load scales the board to 0.8.  Omitting this
        // made the board sit outside the upstream camera framing.
        Mtx_Scale(&world, 0.8f, 0.8f, 0.8f);
        renderer.drawModel(trackModel_, world);
    }
}

void RaceGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    const float position = moveDuration_ > 0.0f ?
        moveStartPosition_ + (moveEndPosition_ - moveStartPosition_) * (moveTime_ / moveDuration_ > 1.0f ? 1.0f : moveTime_ / moveDuration_) :
        displayedPosition_;
    const float y = -2.9f * position;
    if (eye) {
        eye->x = -18.0f;
        eye->y = y - 17.0f;
        eye->z = 20.0f;
        eye->w = 1.0f;
    }
    if (target) {
        target->x = -2.0f;
        target->y = y - 4.0f;
        target->z = 0.0f;
        target->w = 1.0f;
    }
}

void RaceGame::toonPose(float* x, float* y, float* z, float* h) const {
    const float t = moveDuration_ > 0.0f ? (moveTime_ / moveDuration_ > 1.0f ? 1.0f : moveTime_ / moveDuration_) : 1.0f;
    const float position = moveStartPosition_ + (moveEndPosition_ - moveStartPosition_) * t;
    // Lane 0 of DistributedRaceGame.posHprArray, approximated along its
    // authored curved lane. The board's 0.8 scale is applied below too.
    if (x) *x = (-9.03f + 0.14f * position * position) * 0.8f;
    if (y) *y = (-2.9f * position) * 0.8f;
    if (z) *z = 0.025f;
    if (h) *h = 180.0f - 2.0f * position;
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
