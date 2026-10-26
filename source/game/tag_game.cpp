#include "game/tag_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tt::game {

bool TagGame::init(const char* root, uint32_t itAvId) {
    sim_.init(itAvId);
    toonX_ = 0.0f;
    toonY_ = -15.0f;
    toonH_ = 0.0f;
    isDone_ = false;
    std::snprintf(itName_, sizeof(itName_), "Toon");

    char path[256];
    std::snprintf(path, sizeof(path), "%s/tag_arena.ttm", root);
    assetBatch_.load(arenaModel_, path);
    std::snprintf(path, sizeof(path), "%s/tag_treasure.ttm", root);
    assetBatch_.load(treasureModel_, path);
    std::snprintf(path, sizeof(path), "%s/minigame_sky.ttm", root);
    assetBatch_.load(skyModel_, path);

    return true;
}

void TagGame::shutdown() {
    assetBatch_.clear();
    arenaModel_.unload();
    treasureModel_.unload();
    skyModel_.unload();
}

void TagGame::onTag(uint32_t tagger, uint32_t tagged) {
    sim_.attemptTag(tagger, tagged);
}

void TagGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)keysDown;
    (void)nowMs;

    const char* name = client.avatarName(sim_.itAvId);
    if (name && name[0]) std::snprintf(itName_, sizeof(itName_), "%s", name);

    if (sim_.timeRemaining > 0.0f) {
        sim_.step(dt);
        if (sim_.timeRemaining <= 0.0f) {
            isDone_ = true;
        }
    }

    // Move local toon
    float dx = (float)pad.dx / 156.0f;
    float dy = (float)pad.dy / 156.0f;
    if (autoPlay_ && sim_.numTreasures > 0) {
        // The test autoplay has no Circle Pad input. Follow an available
        // treasure through the normal local movement path.
        for (int i = 0; i < sim_.numTreasures; ++i) {
            if (!sim_.treasures[i].active) continue;
            dx = sim_.treasures[i].x - toonX_;
            dy = sim_.treasures[i].y - toonY_;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.01f) {
                dx /= length;
                dy /= length;
            }
            break;
        }
    }
    if (std::fabs(dx) > 0.15f || std::fabs(dy) > 0.15f) {
        float speed = 16.0f * dt;
        toonX_ += dx * speed;
        toonY_ += dy * speed;
        toonH_ = std::atan2(-dx, dy) * 57.29578f;

        // Clamp to arena
        float dist = std::sqrt(toonX_ * toonX_ + toonY_ * toonY_);
        if (dist > taggame::kArenaRadius) {
            toonX_ = (toonX_ / dist) * taggame::kArenaRadius;
            toonY_ = (toonY_ / dist) * taggame::kArenaRadius;
        }

        // Check treasures
        for (int i = 0; i < sim_.numTreasures; ++i) {
            if (sim_.treasures[i].active) {
                float tx = sim_.treasures[i].x - toonX_;
                float ty = sim_.treasures[i].y - toonY_;
                if (tx * tx + ty * ty < 9.0f) {
                    sim_.grabTreasure(i);
                }
            }
        }
    }
}

void TagGame::draw(render::Renderer& renderer) {
    if (skyModel_.loaded()) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        renderer.drawModel(skyModel_, world);
    }
    if (arenaModel_.loaded()) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        renderer.drawModel(arenaModel_, world);
    }

    if (treasureModel_.loaded()) {
        for (int i = 0; i < sim_.numTreasures; ++i) {
            if (sim_.treasures[i].active) {
                C3D_Mtx world;
                Mtx_Identity(&world);
                Mtx_Translate(&world, sim_.treasures[i].x, sim_.treasures[i].y, 0.5f, true);
                renderer.drawModel(treasureModel_, world);
            }
        }
    }
}

void TagGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (eye) {
        eye->x = toonX_;
        eye->y = toonY_ - 30.0f;
        eye->z = 25.0f;
        eye->w = 1.0f;
    }
    if (target) {
        target->x = toonX_;
        target->y = toonY_;
        target->z = 3.0f;
        target->w = 1.0f;
    }
}

void TagGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = toonX_;
    if (y) *y = toonY_;
    if (z) *z = 0.0f;
    if (h) *h = toonH_;
}

const char* TagGame::line(int i) const {
    if (i == 0) {
        std::snprintf(lines_[0], sizeof(lines_[0]), "Time: %.0fs", sim_.timeRemaining);
        return lines_[0];
    }
    if (i == 1) {
        std::snprintf(lines_[1], sizeof(lines_[1]), "Score: %d", sim_.localScore);
        return lines_[1];
    }
    if (i == 2) {
        std::snprintf(lines_[2], sizeof(lines_[2]), "IT: %s (cd: %.1fs)", itName_, sim_.tagBackTimer);
        return lines_[2];
    }
    return "";
}

} // namespace tt::game
