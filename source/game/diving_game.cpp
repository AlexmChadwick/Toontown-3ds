#include "game/diving_game.h"

#include <cmath>
#include <cstdio>
#include "core/log.h"

namespace tt::game {

static constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";

void DivingGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    (void)client;
    done_ = false;
    autoPlay_ = autoPlay;
    startMs_ = nowMs;
    timeRemaining_ = diving::kGameDuration;

    swimmer_ = {};
    swimmer_.x = 0.0f;
    swimmer_.z = 34.0f;

    recoveries_ = 0;
    score_ = 5;

    // 3 chests at seabed
    chests_[0] = {-10.0f, diving::kSeabedZ, true};
    chests_[1] = {0.0f, diving::kSeabedZ, true};
    chests_[2] = {10.0f, diving::kSeabedZ, true};

    // Fish at spawner depths
    for (int i = 0; i < diving::kNumSpawners; ++i) {
        fish_[i].z = diving::kSpawnerZ[i];
        fish_[i].dir = diving::kSpawnerDir[i];
        fish_[i].speed = 6.0f + (i * 1.2f);
        fish_[i].x = (fish_[i].dir > 0) ? diving::kMinX - 4.0f : diving::kMaxX + 4.0f;
        fish_[i].active = true;
    }

    crabX_ = 0.0f;
    crabDir_ = 1;

    char path[96];
    std::snprintf(path, sizeof(path), "%sdiving_game.ttm", kDir);
    environModel_.load(path);
    std::snprintf(path, sizeof(path), "%sdiving_boat.ttm", kDir);
    boatModel_.load(path);
    std::snprintf(path, sizeof(path), "%sdiving_chest.ttm", kDir);
    chestModel_.load(path);

    std::snprintf(lines_[0], sizeof(lines_[0]), "Treasures: 0");
    std::snprintf(lines_[1], sizeof(lines_[1]), "Depth: 0.0 m  (Time: 65 s)");
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: 5");
}

void DivingGame::shutdown() {
    environModel_.unload();
    boatModel_.unload();
    chestModel_.unload();
    done_ = true;
}

void DivingGame::update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs) {
    (void)kDown;
    (void)nowMs;
    if (done_) return;

    timeRemaining_ -= dt;
    if (timeRemaining_ <= 0.0f) {
        timeRemaining_ = 0.0f;
        done_ = true;
    }

    // Input
    float inputX = 0.0f;
    float inputZ = 0.0f;
    constexpr int kDeadzone = 20;
    if (std::abs(pad.dx) > kDeadzone || std::abs(pad.dy) > kDeadzone) {
        inputX = pad.dx / 150.0f;
        inputZ = pad.dy / 150.0f;
    }

    if (autoPlay_) {
        // If no treasure, dive down to nearest chest
        if (!swimmer_.hasTreasure) {
            inputZ = -1.0f;
            int targetChest = 1;
            for (int i = 0; i < 3; ++i) {
                if (chests_[i].available) { targetChest = i; break; }
            }
            float dx = chests_[targetChest].x - swimmer_.x;
            inputX = (dx > 1.0f) ? 0.8f : (dx < -1.0f) ? -0.8f : 0.0f;
        } else {
            // Swim up to boat
            inputZ = 1.0f;
            inputX = (swimmer_.x > 1.0f) ? -0.8f : (swimmer_.x < -1.0f) ? 0.8f : 0.0f;
        }
    }

    // Update swimmer physics
    diving::updateSwimmer(swimmer_, inputX, inputZ, dt);

    // Update fish movement
    for (int i = 0; i < diving::kNumSpawners; ++i) {
        fish_[i].x += fish_[i].dir * fish_[i].speed * dt;
        if (fish_[i].dir > 0 && fish_[i].x > diving::kMaxX + 5.0f) {
            fish_[i].x = diving::kMinX - 5.0f;
        } else if (fish_[i].dir < 0 && fish_[i].x < diving::kMinX - 5.0f) {
            fish_[i].x = diving::kMaxX + 5.0f;
        }

        // Collision with fish
        if (swimmer_.freezeTimer <= 0.0f) {
            const float dx = swimmer_.x - fish_[i].x;
            const float dz = swimmer_.z - fish_[i].z;
            if (dx * dx + dz * dz < 1.8f * 1.8f) {
                diving::applyObstacleHit(swimmer_);
                client.sendDivingFishCollision(client.ownStats().money, i, i, "normal");
            }
        }
    }

    // Update crab movement along seabed
    crabX_ += crabDir_ * 4.0f * dt;
    if (crabX_ > 18.0f) { crabX_ = 18.0f; crabDir_ = -1; }
    else if (crabX_ < -18.0f) { crabX_ = -18.0f; crabDir_ = 1; }

    // Collision with crab
    if (swimmer_.freezeTimer <= 0.0f && swimmer_.z <= diving::kSeabedZ + 2.0f) {
        const float dx = swimmer_.x - crabX_;
        if (std::abs(dx) < 2.0f) {
            diving::applyObstacleHit(swimmer_);
            client.sendDivingCrabCollision(client.ownStats().money, "normal");
        }
    }

    // Check chest pickup
    for (int i = 0; i < 3; ++i) {
        if (diving::checkChestPickup(swimmer_, chests_[i])) {
            client.sendDivingPickupTreasure(i);
            break;
        }
    }

    // Check boat recovery
    if (diving::checkBoatRecovery(swimmer_)) {
        recoveries_++;
        score_ = diving::calculateScore(recoveries_);
        client.sendDivingTreasureRecovered();
        // Respawn chests
        for (int i = 0; i < 3; ++i) {
            chests_[i].available = true;
        }
    }

    // Server score sync if minigame active
    if (client.minigame() && client.minigame()->divingScoreAv != 0) {
        score_ = diving::calculateScore(recoveries_);
    }

    // Depth in meters (surface at 36 = 0m, bottom at -38 = 74m)
    const float depthM = std::max(0.0f, (36.0f - swimmer_.z));

    std::snprintf(lines_[0], sizeof(lines_[0]), "Treasures: %d", recoveries_);
    std::snprintf(lines_[1], sizeof(lines_[1]), "Depth: %.1f m  (Time: %d s)", depthM, int(std::ceil(timeRemaining_)));
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: %d", score_);
}

void DivingGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    if (environModel_.loaded()) {
        Mtx_Identity(&m);
        Mtx_Translate(&m, 0.0f, 0.5f, -41.0f, true);
        Mtx_Scale(&m, 2.8f, 2.8f, 2.73f);
        renderer.drawModel(environModel_, m);
    }
    if (boatModel_.loaded()) {
        Mtx_Identity(&m);
        Mtx_Translate(&m, 0.0f, 3.0f, 33.25f, true);
        Mtx_Scale(&m, 2.8f, 2.8f, 2.8f);
        renderer.drawModel(boatModel_, m);
    }
    if (chestModel_.loaded()) {
        for (int i = 0; i < 3; ++i) {
            if (chests_[i].available) {
                Mtx_Identity(&m);
                Mtx_Translate(&m, chests_[i].x, 0.0f, chests_[i].z, true);
                Mtx_Scale(&m, 1.5f, 1.5f, 1.5f);
                renderer.drawModel(chestModel_, m);
            }
        }
    }
}

void DivingGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = swimmer_.x;
    if (y) *y = -1.0f;
    if (z) *z = swimmer_.z;
    if (h) {
        // Pitch/heading based on velocity
        float angle = std::atan2(-swimmer_.xVel, -swimmer_.zVel) * 180.0f / 3.14159265f;
        *h = angle;
    }
}

void DivingGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    // Camera follows swimmer vertically in 2D plane
    float camZ = swimmer_.z + 2.0f;
    if (camZ < -24.0f) camZ = -24.0f;
    if (camZ > 34.0f) camZ = 34.0f;

    if (target) {
        target->x = swimmer_.x * 0.3f;
        target->y = 0.0f;
        target->z = camZ;
        target->w = 1.0f;
    }
    if (eye) {
        eye->x = 0.0f;
        eye->y = -22.0f;
        eye->z = camZ;
        eye->w = 1.0f;
    }
}

} // namespace tt::game
