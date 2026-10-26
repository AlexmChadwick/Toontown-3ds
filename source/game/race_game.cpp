#include "game/race_game.h"

#include <cmath>

#include <cstdio>
#include <cstring>

namespace tt::game {

namespace {
constexpr float kCameraMoveSeconds = 0.75f; // moveCamera: posQuatInterval(0.75, ...)
// DistributedRaceGame.cameraTopView (onstage): x, y, z, h, p.
constexpr float kTopView[5] = {-22.78f, -41.65f, 31.53f, -51.55f, -42.68f};
} // namespace

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
    lane_ = 0;
    numRacers_ = 1;
    {
        // onstage: the top view until the first move; look 10 units along its heading/pitch.
        const float h = kTopView[3] * 0.0174532925f;
        const float p = kTopView[4] * 0.0174532925f;
        const float look[3] = {-std::sin(h) * std::cos(p), std::cos(h) * std::cos(p), std::sin(p)};
        for (int i = 0; i < 3; ++i) {
            camTo_[i] = kTopView[i];
            camTo_[3 + i] = kTopView[i] + look[i] * 10.0f;
        }
        for (int i = 0; i < 6; ++i) camFrom_[i] = camTo_[i];
        camTime_ = kCameraMoveSeconds;
    }

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
    const int oldPosition = sim_.positions[lane_];
    for (int i = 0; i < count && i < 4; ++i) {
        sim_.positions[i] = positions[i] > racegame::kNumberToWin ? racegame::kNumberToWin : positions[i];
        if (sim_.positions[i] == racegame::kNumberToWin) sim_.isDone = true;
    }
    moveStartPosition_ = oldPosition;
    moveEndPosition_ = sim_.positions[lane_];
    moveTime_ = 0.0f;
    const int spaces = std::abs(moveEndPosition_ - moveStartPosition_);
    moveDuration_ = spaces > 4 ? float(spaces) / 4.0f : float(spaces) / 1.2f;
    displayedPosition_ = moveEndPosition_;
    numRacers_ = count < 1 ? 1 : count;
    moveCamera();
    choiceSubmitted_ = false;
}

void RaceGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)pad;
    (void)nowMs;

    if (moveTime_ < moveDuration_) moveTime_ += dt;
    if (camTime_ < kCameraMoveSeconds) camTime_ += dt;

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
    const float t = camTime_ / kCameraMoveSeconds > 1.0f ? 1.0f : camTime_ / kCameraMoveSeconds;
    float v[6];
    for (int i = 0; i < 6; ++i) v[i] = camFrom_[i] + (camTo_[i] - camFrom_[i]) * t;
    if (eye) *eye = FVec4_New(v[0], v[1], v[2], 1.0f);
    if (target) *target = FVec4_New(v[3], v[4], v[5], 1.0f);
}

void RaceGame::moveCamera() {
    // DistributedRaceGame.moveCamera, positions in posHprArray units (render space, as upstream).
    using racegame::kLanePosHpr;
    using racegame::kNumberToWin;
    int best = sim_.positions[0];
    int bestLane = 0;
    for (int lane = 0; lane < numRacers_ && lane < racegame::kNumLanes; ++lane) {
        if (sim_.positions[lane] > best) {
            best = sim_.positions[lane];
            bestLane = lane;
        }
    }
    best = best < kNumberToWin ? best : kNumberToWin;
    const int own = sim_.positions[lane_];
    const float* pos1 = kLanePosHpr[lane_][own < kNumberToWin - 4 ? own : kNumberToWin - 4];
    const int bestLookAt = best + 1;
    const int ownLookAt = own + 4;
    const float* pos2;
    if (ownLookAt >= bestLookAt) {
        pos2 = kLanePosHpr[lane_][ownLookAt < kNumberToWin ? ownLookAt : kNumberToWin];
    } else {
        pos2 = kLanePosHpr[bestLane][bestLookAt < kNumberToWin ? bestLookAt : kNumberToWin];
    }
    constexpr float kDistanceMultiplier = 0.8f;
    const float raceFraction = float(best) / float(kNumberToWin);
    const float height = 10.0f * raceFraction + (1.0f - raceFraction) * 22.0f;
    const float* lookAt = kLanePosHpr[lane_][own < kNumberToWin - 6 ? own : kNumberToWin - 6];
    for (int i = 0; i < 6; ++i) camFrom_[i] = camFrom_[i] + (camTo_[i] - camFrom_[i]) *
                                     (camTime_ / kCameraMoveSeconds > 1.0f ? 1.0f : camTime_ / kCameraMoveSeconds);
    camTo_[0] = pos2[0] + kDistanceMultiplier * (pos2[0] - pos1[0]);
    camTo_[1] = pos2[1] + kDistanceMultiplier * (pos2[1] - pos1[1]);
    camTo_[2] = pos2[2] + height;
    camTo_[3] = lookAt[0];
    camTo_[4] = lookAt[1];
    camTo_[5] = lookAt[2];
    camTime_ = 0.0f;
}

void RaceGame::toonPose(float* x, float* y, float* z, float* h) const {
    const float t = moveDuration_ > 0.0f ? (moveTime_ / moveDuration_ > 1.0f ? 1.0f : moveTime_ / moveDuration_) : 1.0f;
    float position = moveStartPosition_ + (moveEndPosition_ - moveStartPosition_) * t;
    if (position < 0.0f) position = 0.0f;
    if (position > float(racegame::kNumPlaces - 1)) position = float(racegame::kNumPlaces - 1);
    // DistributedRaceGame.positionInPlace / walkInPlace: the lane's posHprArray entries, relative
    // to the race board (scaled 0.8 in load); between places the toon walks the straight segment.
    const int lane = lane_ >= 0 && lane_ < racegame::kNumLanes ? lane_ : 0;
    const int from = int(position);
    const int to = from + 1 < racegame::kNumPlaces ? from + 1 : from;
    const float f = position - float(from);
    const float* a = racegame::kLanePosHpr[lane][from];
    const float* b = racegame::kLanePosHpr[lane][to];
    constexpr float kBoardScale = 0.8f; // DistributedRaceGame.load: raceBoard.setScale(0.8)
    if (x) *x = (a[0] + (b[0] - a[0]) * f) * kBoardScale;
    if (y) *y = (a[1] + (b[1] - a[1]) * f) * kBoardScale;
    if (z) *z = (a[2] + (b[2] - a[2]) * f) * kBoardScale;
    if (h) *h = a[3] + (b[3] - a[3]) * f;
}

void RaceGame::lanePose(int lane, float* x, float* y, float* z, float* h) const {
    if (lane < 0 || lane >= racegame::kNumLanes) lane = 0;
    int place = sim_.positions[lane];
    if (place < 0) place = 0;
    if (place > racegame::kNumPlaces - 1) place = racegame::kNumPlaces - 1;
    const float* p = racegame::kLanePosHpr[lane][place];
    constexpr float kBoardScale = 0.8f; // DistributedRaceGame.load: raceBoard.setScale(0.8)
    *x = p[0] * kBoardScale;
    *y = p[1] * kBoardScale;
    *z = p[2] * kBoardScale;
    *h = p[3];
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
