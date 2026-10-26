#include "game/pattern_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tt::game {

namespace {
// DistributedPatternGame.minnieAnimNames (pattern values index it).
constexpr const char* kMinnieClipPaths[4] = {
    "sdmc:/3ds/toontown3ds/data/chars/minnie_800_up.tta",
    "sdmc:/3ds/toontown3ds/data/chars/minnie_800_left.tta",
    "sdmc:/3ds/toontown3ds/data/chars/minnie_800_down.tta",
    "sdmc:/3ds/toontown3ds/data/chars/minnie_800_right.tta",
};
// DistributedPatternGame.load: animPlayRate 1.4, times 1.06 each round.
constexpr float kAnimPlayRate = 1.4f;
constexpr float kAnimPlayRateMult = 1.06f;
// enterShowServerPattern: Wait(0.5) before the dance, Wait(0.2) after it.
constexpr float kShowLeadIn = 0.5f;
constexpr float kShowLeadOut = 0.2f;

// One ping-pong of a clip (__numPingPongFrames): frames 0..n-1 then back to 0.
int pingPongFrames(int numFrames) {
    return numFrames * 2 - 1;
}
int pingPongFrame(int index, int numFrames) {
    return index < numFrames ? index : 2 * numFrames - 2 - index;
}
} // namespace

bool PatternGame::init(const char* root) {
    sim_.init();
    timer_ = 0.0f;
    roundTime_ = 0.0f;
    readySent_ = false;
    waitingForPattern_ = true;
    patternSubmitted_ = false;

    char path[256];
    std::snprintf(path, sizeof(path), "%s/pattern_stage.ttm", root);
    assetBatch_.load(stageModel_, path);
    assetBatch_.load(minnieModel_, "sdmc:/3ds/toontown3ds/data/chars/minnie_800.ttm");
    for (int i = 0; i < 4; ++i) assetBatch_.load(minnieClips_[i], kMinnieClipPaths[i]);
    showTime_ = -1.0f;

    return true;
}

void PatternGame::shutdown() {
    assetBatch_.clear();
    stageModel_.unload();
    minnieModel_.unload();
    for (render::Anim& clip : minnieClips_) clip.unload();
}

float PatternGame::showStepDuration() const {
    // getDanceStepDuration: one ping-pong of 'up' at animPlayRate; every other clip's play rate
    // is scaled (minnieAnimSpeedMult) so its ping-pong takes the same time.
    const render::Anim& up = minnieClips_[0];
    const int frames = up.loaded() ? int(up.numFrames()) : 1;
    const float fps = up.loaded() ? up.fps() : 12.0f;
    const int round = sim_.round < 1 ? 1 : sim_.round;
    const float rate = kAnimPlayRate * std::pow(kAnimPlayRateMult, float(round - 1));
    return float(pingPongFrames(frames)) / (rate * fps);
}

bool PatternGame::showing() const {
    if (showTime_ < 0.0f) return false;
    return showTime_ < kShowLeadIn + float(sim_.targetLen) * showStepDuration() + kShowLeadOut;
}

void PatternGame::onSetPattern(const uint8_t* pattern, int len) {
    sim_.setTargetPattern(pattern, len);
    roundTime_ = 0.0f;
    waitingForPattern_ = false;
    patternSubmitted_ = false;
    showTime_ = 0.0f;
}

void PatternGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)pad;
    (void)nowMs;

    if (waitingForPattern_) {
        // DistributedPatternGameAI waits for reportPlayerReady before generating
        // each round. Never synthesize a pattern while connected to the AI.
        if (!readySent_) readySent_ = client.sendPatternReady();
        return;
    }

    // getUserInput starts only after Minnie has shown the pattern.
    if (showing()) {
        showTime_ += dt;
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

        if (autoPlay_ && sim_.inputLen < sim_.targetLen &&
            roundTime_ >= float(sim_.inputLen + 1) * .5f)
            pressed = sim_.targetPattern[sim_.inputLen];
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
        // DistributedPatternGame.onstage offsets matching_room by 18.39.
        Mtx_Translate(&world, 0.0f, 18.39f, 0.0f, true);
        renderer.drawModel(stageModel_, world);
    }
    if (minnieModel_.loaded()) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        // DistributedPatternGame.setToonRefs puts Minnie at (-1.6, 20, 0).
        Mtx_Translate(&world, -1.6f, 20.0f, 0.0f, true);
        Mtx_RotateZ(&world, 180.0f * 0.0174532925f, true);
        // Dancing: the current step's clip, one ping-pong per step; otherwise the first pose
        // of 'up' (enterShowServerPattern poses every clip at frame 0 between rounds).
        int clip = 0;
        float pingPong = 0.0f;
        const float danceTime = showTime_ - kShowLeadIn;
        const float step = showStepDuration();
        if (showing() && danceTime >= 0.0f && step > 0.0f) {
            const int index = int(danceTime / step);
            if (index < sim_.targetLen) {
                const int direction = sim_.targetPattern[index];
                clip = direction >= 0 && direction < 4 ? direction : 0;
                pingPong = (danceTime - float(index) * step) / step;
            }
        }
        const render::Anim& anim = minnieClips_[clip];
        if (anim.loaded()) {
            const int frames = int(anim.numFrames());
            const float position = pingPong * float(pingPongFrames(frames) - 1);
            const int first = int(position);
            const int second = first + 1 < pingPongFrames(frames) ? first + 1 : first;
            renderer.drawMorph(minnieModel_, anim, u32(pingPongFrame(first, frames)),
                               u32(pingPongFrame(second, frames)), position - float(first), world);
        } else {
            renderer.drawModel(minnieModel_, world);
        }
    }
}

void PatternGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (eye) {
        eye->x = 0.0f;
        eye->y = -8.0f;
        eye->z = 8.0f;
        eye->w = 1.0f;
    }
    if (target) {
        target->x = 0.0f;
        target->y = 18.0f;
        target->z = 3.0f;
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
