#include "game/maze_game.h"

#include <cmath>
#include <cstdio>
#include "core/log.h"

namespace tt::game {

static constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";

void MazeGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    (void)client;
    done_ = false;
    autoPlay_ = autoPlay;
    isRunning_ = false;
    startMs_ = nowMs;
    timeRemaining_ = maze::kGameDuration;

    toonX_ = 0.0f;
    toonY_ = 0.0f;
    toonZ_ = 0.0f;
    toonH_ = 180.0f;

    for (int i = 0; i < maze::kNumTreasures; ++i) {
        treasureTaken_[i] = false;
    }
    treasuresCollected_ = 0;
    score_ = 0;
    hitCooldown_ = 0.0f;

    maze::initSuits(suits_);

    char path[96];
    std::snprintf(path, sizeof(path), "%smaze_1player.ttm", kDir);
    assetBatch_.load(mazeModel_, path);
    std::snprintf(path, sizeof(path), "%smaze_treasure.ttm", kDir);
    assetBatch_.load(treasureModel_, path);
    // Four MazeSuit actors are active in a solo maze
    // (DistributedMazeGame.__loadSuits). Reuse the bounded Cog stand-in.
    assetBatch_.load(suitModel_, "romfs:/baked/suit_placeholder.ttm");

    std::snprintf(lines_[0], sizeof(lines_[0]), "Treasures: 0 / %d", maze::kNumTreasures);
    std::snprintf(lines_[1], sizeof(lines_[1]), "Time: 60 s");
    std::snprintf(lines_[2], sizeof(lines_[2]), "Score: 1 bean");
}

void MazeGame::shutdown() {
    assetBatch_.clear();
    mazeModel_.unload();
    treasureModel_.unload();
    suitModel_.unload();
    done_ = true;
}

void MazeGame::update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs) {
    (void)nowMs;
    (void)kDown;
    if (done_) return;

    timeRemaining_ -= dt;
    if (timeRemaining_ <= 0.0f) {
        timeRemaining_ = 0.0f;
        done_ = true;
    }

    if (hitCooldown_ > 0.0f) {
        hitCooldown_ -= dt;
        if (hitCooldown_ < 0.0f) hitCooldown_ = 0.0f;
    }

    // Check server events (e.g. other player grabbed treasure or all taken)
    if (client.minigame() && client.minigame()->mazeAllTaken) {
        done_ = true;
    }

    // Circle Pad movement
    float moveX = 0.0f;
    float moveY = 0.0f;
    constexpr int kDeadzone = 20;

    if (std::abs(pad.dx) > kDeadzone || std::abs(pad.dy) > kDeadzone) {
        moveX = pad.dx / 150.0f;
        moveY = pad.dy / 150.0f;
        const float len = std::sqrt(moveX * moveX + moveY * moveY);
        if (len > 1.0f) {
            moveX /= len;
            moveY /= len;
        }
    }

    if (autoPlay_) {
        // Find nearest uncollected treasure and steer toward it
        float nearestDistSq = 1e9f;
        float targetX = 0.0f, targetY = 0.0f;
        for (int i = 0; i < maze::kNumTreasures; ++i) {
            if (!treasureTaken_[i]) {
                const float dx = maze::kTreasures[i].x - toonX_;
                const float dy = maze::kTreasures[i].y - toonY_;
                const float d2 = dx * dx + dy * dy;
                if (d2 < nearestDistSq) {
                    nearestDistSq = d2;
                    targetX = maze::kTreasures[i].x;
                    targetY = maze::kTreasures[i].y;
                }
            }
        }
        if (nearestDistSq < 1e8f) {
            float dx = targetX - toonX_;
            float dy = targetY - toonY_;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d > 0.1f) {
                moveX = dx / d;
                moveY = dy / d;
            }
        }
    }

    const float speed = 8.0f; // DistributedMazeGame.TOON_SPEED
    if (std::abs(moveX) > 0.05f || std::abs(moveY) > 0.05f) {
        isRunning_ = true;
        toonH_ = std::atan2(-moveX, -moveY) * 180.0f / 3.14159265f;
        const float newX = toonX_ + moveX * speed * dt;
        const float newY = toonY_ + moveY * speed * dt;
        maze::doOrthoCollisions(toonX_, toonY_, newX, newY, toonX_, toonY_);
    } else {
        isRunning_ = false;
    }

    // Check treasure pickups
    constexpr float kGrabDistSq = maze::kTreasureGrabRadius * maze::kTreasureGrabRadius;
    for (int i = 0; i < maze::kNumTreasures; ++i) {
        if (!treasureTaken_[i]) {
            const float dx = toonX_ - maze::kTreasures[i].x;
            const float dy = toonY_ - maze::kTreasures[i].y;
            if (dx * dx + dy * dy < kGrabDistSq) {
                treasureTaken_[i] = true;
                treasuresCollected_++;
                client.sendMazeClaimTreasure(i);
                score_ = maze::calculateScore(treasuresCollected_, treasuresCollected_ == maze::kNumTreasures);
                if (treasuresCollected_ >= maze::kNumTreasures) {
                    done_ = true;
                }
            }
        }
    }

    // Update and check suits
    for (int i = 0; i < maze::kNumSuits; ++i) {
        maze::updateSuit(suits_[i], dt);
        if (hitCooldown_ <= 0.0f) {
            const float dx = toonX_ - suits_[i].x;
            const float dy = toonY_ - suits_[i].y;
            if (dx * dx + dy * dy < maze::kSuitHitRadius * maze::kSuitHitRadius) {
                hitCooldown_ = 2.5f;
                client.sendMazeHitBySuit(client.avatarId(), client.networkTime(nowMs));
                // Respawn toon at center
                toonX_ = 0.0f;
                toonY_ = 0.0f;
            }
        }
    }

    // Update bottom screen lines
    std::snprintf(lines_[0], sizeof(lines_[0]), "Treasures: %d / %d", treasuresCollected_, maze::kNumTreasures);
    std::snprintf(lines_[1], sizeof(lines_[1]), "Time: %d s", int(std::ceil(timeRemaining_)));
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: %d", score_ > 0 ? score_ : 1);
}

void MazeGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    if (mazeModel_.loaded()) {
        Mtx_Identity(&m);
        renderer.drawModel(mazeModel_, m);
    }
    if (treasureModel_.loaded()) {
        for (int i = 0; i < maze::kNumTreasures; ++i) {
            if (!treasureTaken_[i]) {
                const float dx = maze::kTreasures[i].x - toonX_;
                const float dy = maze::kTreasures[i].y - toonY_;
                if (std::abs(dx) < 25.0f && std::abs(dy) < 25.0f) {
                    Mtx_Identity(&m);
                    Mtx_Translate(&m, maze::kTreasures[i].x, maze::kTreasures[i].y, 0.5f, true);
                    renderer.drawModel(treasureModel_, m);
                }
            }
        }
    }
    if (suitModel_.loaded()) {
        for (int i = 0; i < maze::kNumSuits; ++i) {
            Mtx_Identity(&m);
            Mtx_Translate(&m, suits_[i].x, suits_[i].y, 0.0f, true);
            Mtx_RotateZ(&m, suits_[i].heading * 0.0174532925f, true);
            renderer.drawModel(suitModel_, m);
        }
    }
}

void MazeGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = toonX_;
    if (y) *y = toonY_;
    if (z) *z = toonZ_;
    if (h) *h = toonH_;
}

void MazeGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (target) {
        target->x = toonX_;
        target->y = toonY_;
        target->z = toonZ_ + 1.5f;
        target->w = 1.0f;
    }
    if (eye) {
        // Top-down angled follow camera: Y offset -19, Z offset 45
        eye->x = toonX_;
        eye->y = toonY_ - 19.0f;
        eye->z = toonZ_ + 45.0f;
        eye->w = 1.0f;
    }
}

} // namespace tt::game
