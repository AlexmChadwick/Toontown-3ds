#include "game/ice_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tt::game {

bool IceGame::init(const char* root, int players) {
    sim_.init(players);
    state_ = State::Aiming;
    aimAngle_ = 45.0f;
    aimForce_ = 50.0f;
    scoreTimer_ = 0.0f;

    char path[256];
    std::snprintf(path, sizeof(path), "%s/ice_arena.ttm", root);
    assetBatch_.load(arenaModel_, path);
    std::snprintf(path, sizeof(path), "%s/ice_tire.ttm", root);
    assetBatch_.load(tireModel_, path);

    return true;
}

void IceGame::shutdown() {
    assetBatch_.clear();
    arenaModel_.unload();
    tireModel_.unload();
}

void IceGame::onSetMatchAndRound(uint8_t match, uint8_t round) {
    if (sim_.curMatch != match) sim_.resetRound();
    sim_.curMatch = match;
    sim_.curRound = round;
    state_ = State::Aiming;
}

void IceGame::onServer(net::EventType event, const net::Minigame& game) {
    if (event == net::EventType::IceTireInputs) {
        for (int i = 0; i < game.iceInputCount; ++i)
            sim_.setChoice(i, game.iceChoicesForce[i], game.iceChoicesDir[i]);
        state_ = State::Sliding;
    } else if (event == net::EventType::IceFinalPositions) {
        for (int i = 0; i < game.icePositionCount; ++i) {
            sim_.tires[i].x = game.iceFinalPositions[i][0];
            sim_.tires[i].y = game.iceFinalPositions[i][1];
            sim_.tires[i].moving = false;
        }
    } else if (event == net::EventType::IceScores) {
        for (int i = 0; i < game.iceScoreCount; ++i) sim_.scores[i] = game.iceScores[i];
    } else if (event == net::EventType::IceNewState) {
        if (!std::strcmp(game.iceState, "inputChoice")) state_ = State::Aiming;
        else if (!std::strcmp(game.iceState, "scoring")) {
            state_ = State::Scoring;
            scoreTimer_ = 0;
        } else if (!std::strcmp(game.iceState, "finalResults")) sim_.isDone = true;
    }
}

void IceGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)nowMs;

    if (state_ == State::Aiming) {
        // Aim angle & force using Circle Pad
        float dx = (float)pad.dx / 156.0f;
        float dy = (float)pad.dy / 156.0f;
        if (std::fabs(dx) > 0.15f) {
            aimAngle_ -= dx * 60.0f * dt;
        }
        if (std::fabs(dy) > 0.15f) {
            aimForce_ += dy * 40.0f * dt;
            if (aimForce_ < 10.0f) aimForce_ = 10.0f;
            if (aimForce_ > 100.0f) aimForce_ = 100.0f;
        }

        if (keysDown & KEY_A) {
            client.sendIceChoice(aimForce_, aimAngle_);
            state_ = State::WaitChoices; // wait for the AI's setTireInputs
        }
    } else if (state_ == State::Sliding) {
        sim_.stepPhysics(dt);
        if (sim_.allTiresStopped()) {
            float positions[12] = {};
            for (int i = 0; i < sim_.numPlayers; ++i) {
                positions[i*3] = sim_.tires[i].x;
                positions[i*3+1] = sim_.tires[i].y;
                positions[i*3+2] = icegame::kTireRadius;
            }
            client.sendIceEndingPositions(positions, sim_.numPlayers);
            state_ = State::WaitEnding;
        }
    } else if (state_ == State::Scoring) {
        scoreTimer_ += dt;
        // IceGameGlobals.ShowScoresDuration; the AI owns match/round increments.
        if (scoreTimer_ >= 4.0f && client.sendIceScoringMovieDone()) state_ = State::WaitRound;
    }
}

void IceGame::draw(render::Renderer& renderer) {
    if (arenaModel_.loaded()) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        renderer.drawModel(arenaModel_, world);
    }

    if (tireModel_.loaded()) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        Mtx_Translate(&world, sim_.tires[0].x, sim_.tires[0].y, 0.5f, true);
        renderer.drawModel(tireModel_, world);
    }
}

void IceGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (eye) {
        eye->x = 0.0f;
        eye->y = -35.0f;
        eye->z = 40.0f;
        eye->w = 1.0f;
    }
    if (target) {
        target->x = 0.0f;
        target->y = 0.0f;
        target->z = 0.0f;
        target->w = 1.0f;
    }
}

void IceGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = sim_.tires[0].x;
    if (y) *y = sim_.tires[0].y;
    if (z) *z = 0.8f;
    if (h) *h = aimAngle_;
}

const char* IceGame::line(int i) const {
    if (i == 0) {
        std::snprintf(lines_[0], sizeof(lines_[0]), "Match %d / %d", sim_.curMatch + 1, icegame::kNumMatches);
        return lines_[0];
    }
    if (i == 1) {
        if (state_ == State::Aiming) {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Angle: %.0f deg  Force: %.0f%% (A: Launch)", aimAngle_, aimForce_);
        } else if (state_ == State::Sliding) {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Sliding... Dist: %.1f", sim_.tires[0].distanceToCenter());
        } else {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Scored! Dist: %.1f", sim_.tires[0].distanceToCenter());
        }
        return lines_[1];
    }
    if (i == 2) {
        std::snprintf(lines_[2], sizeof(lines_[2]), "Score: %d beans", sim_.scores[0]);
        return lines_[2];
    }
    return "";
}

} // namespace tt::game
