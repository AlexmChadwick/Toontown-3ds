#include "game/vine_game.h"

#include <cmath>
#include <cstdio>
#include "core/log.h"

namespace tt::game {

static constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";

void VineGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    (void)client;
    done_ = false;
    autoPlay_ = autoPlay;
    startMs_ = nowMs;
    timeRemaining_ = vine::kGameDuration;

    toon_ = {};
    toon_.state = vine::State::OnVine;
    toon_.currentVine = 0;
    toon_.vineT = 0.5f;
    toon_.swingTime = 0.0f;
    vine::getVineToonPos(0, toon_.vineT, 0.0f, toon_.posX, toon_.posZ);

    for (int i = 0; i < vine::kNumVines - 1; ++i) {
        bananaTaken_[i] = false;
    }
    bananas_ = 0;
    score_ = 1;

    char path[96];
    std::snprintf(path, sizeof(path), "%svine_game.ttm", kDir);
    jungleModel_.load(path);
    std::snprintf(path, sizeof(path), "%svine_shelf.ttm", kDir);
    shelfModel_.load(path);

    std::snprintf(lines_[0], sizeof(lines_[0]), "Vine: 1 / 20");
    std::snprintf(lines_[1], sizeof(lines_[1]), "Bananas: 0  (Time: 70 s)");
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: 1");
}

void VineGame::shutdown() {
    jungleModel_.unload();
    shelfModel_.unload();
    done_ = true;
}

void VineGame::update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs) {
    (void)nowMs;
    if (done_) return;

    timeRemaining_ -= dt;
    if (timeRemaining_ <= 0.0f) {
        timeRemaining_ = 0.0f;
        done_ = true;
    }

    if (toon_.state == vine::State::OnVine) {
        // Climbing up/down on vine
        constexpr int kDeadzone = 20;
        if (std::abs(pad.dy) > kDeadzone) {
            toon_.vineT -= (pad.dy / 150.0f) * 0.8f * dt;
            if (toon_.vineT < 0.2f) toon_.vineT = 0.2f;
            else if (toon_.vineT > 0.85f) toon_.vineT = 0.85f;
        }

        // Pumping swing momentum with Left/Right
        float pump = 1.0f;
        if (std::abs(pad.dx) > kDeadzone) {
            pump += (std::abs(pad.dx) / 150.0f) * 0.5f;
        }
        toon_.swingTime += dt * pump;

        const float angle = vine::getVineAngle(toon_.swingTime);
        vine::getVineToonPos(toon_.currentVine, toon_.vineT, angle, toon_.posX, toon_.posZ);

        // AutoPlay jump at forward swing peak
        bool doJump = (kDown & KEY_A) != 0;
        if (autoPlay_) {
            const float omega = (2.0f * 3.14159265f) / 3.0f;
            const float angVel = 0.785398f * omega * std::cos(omega * toon_.swingTime);
            if (angle > 0.35f && angVel > 0.0f) {
                doJump = true;
            }
        }

        if (doJump) {
            const float omega = (2.0f * 3.14159265f) / 3.0f;
            const float angVel = 0.785398f * omega * std::cos(omega * toon_.swingTime);
            vine::jumpFromVine(toon_, angle, angVel);
        }
    } else if (toon_.state == vine::State::Jumping) {
        vine::updateJump(toon_, dt);

        // Check banana pickup between vines
        const int between = toon_.currentVine;
        if (between >= 0 && between < vine::kNumVines - 1 && !bananaTaken_[between]) {
            const float bananaX = between * vine::kVineSpacing + (vine::kVineSpacing * 0.5f);
            if (std::abs(toon_.posX - bananaX) < 3.0f && std::abs(toon_.posZ - 16.0f) < 5.0f) {
                bananaTaken_[between] = true;
                bananas_++;
                client.sendVineClaimTreasure(between);
                score_ = vine::calculateScore(bananas_, timeRemaining_, false);
            }
        }

        // Check catch next vine
        if (vine::checkVineCatch(toon_)) {
            client.sendVineNewVine(client.ownStats().money, int8_t(toon_.currentVine), toon_.vineT, 1);
            if (toon_.state == vine::State::Finished) {
                client.sendVineReachedEndVine(int8_t(toon_.currentVine));
                score_ = vine::calculateScore(bananas_, timeRemaining_, true);
                done_ = true;
            }
        }
    }

    std::snprintf(lines_[0], sizeof(lines_[0]), "Vine: %d / 20", toon_.currentVine + 1);
    std::snprintf(lines_[1], sizeof(lines_[1]), "Bananas: %d  (Time: %d s)", bananas_, int(std::ceil(timeRemaining_)));
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: %d", score_);
}

void VineGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    if (jungleModel_.loaded()) {
        Mtx_Identity(&m);
        Mtx_Translate(&m, toon_.posX, 0.0f, 0.0f, true);
        renderer.drawModel(jungleModel_, m);
    }
    if (shelfModel_.loaded()) {
        for (int i = 0; i < vine::kNumVines; ++i) {
            const float vx = i * vine::kVineSpacing;
            if (std::abs(vx - toon_.posX) < 50.0f) {
                Mtx_Identity(&m);
                Mtx_Translate(&m, vx, 0.0f, vine::kVineTopZ, true);
                renderer.drawModel(shelfModel_, m);
            }
        }
    }
}

void VineGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = toon_.posX;
    if (y) *y = 0.0f;
    if (z) *z = toon_.posZ;
    if (h) *h = 90.0f; // facing right along the jungle track
}

void VineGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (target) {
        target->x = toon_.posX + 4.0f;
        target->y = 0.0f;
        target->z = toon_.posZ + 2.0f;
        target->w = 1.0f;
    }
    if (eye) {
        eye->x = toon_.posX;
        eye->y = -30.0f;
        eye->z = toon_.posZ + 4.0f;
        eye->w = 1.0f;
    }
}

} // namespace tt::game
