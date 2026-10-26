#include "game/tag_game.h"

#include "world/toon_dna.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tt::game {

namespace {

// Stick deadzone, as the other minigames use.
constexpr float kPadDeadzone = 0.15f;
constexpr float kPadFull = 156.0f;
// Treasure pickup radius (squared 9.0, a 3 unit reach).
constexpr float kTreasureGrabSq = 9.0f;

float toonHeightOf(const uint8_t* dnaBytes) {
    world::ToonDna dna;
    if (dnaBytes && world::parseNetString(dnaBytes, net::kDnaSize, &dna)) return world::toonHeight(dna);
    return world::toonHeight(world::defaultToonDna());
}

} // namespace

bool TagGame::init(const char* root, uint32_t itAvId) {
    sim_.init(itAvId);
    me_ = taggame::Walker{};
    me_.x = 0.0f;
    me_.y = -15.0f;
    me_.h = 0.0f;
    tagSendCooldown_ = 0.0f;
    isDone_ = false;
    itPointerValid_ = false;
    std::snprintf(itName_, sizeof(itName_), "Toon");

    char path[256];
    std::snprintf(path, sizeof(path), "%s/tag_arena.ttm", root);
    assetBatch_.load(arenaModel_, path);
    std::snprintf(path, sizeof(path), "%s/tag_treasure.ttm", root);
    assetBatch_.load(treasureModel_, path);
    std::snprintf(path, sizeof(path), "%s/minigame_sky.ttm", root);
    assetBatch_.load(skyModel_, path);
    std::snprintf(path, sizeof(path), "%s/tag_pointer.ttm", root);
    assetBatch_.load(pointerModel_, path);

    return true;
}

void TagGame::shutdown() {
    assetBatch_.clear();
    arenaModel_.unload();
    treasureModel_.unload();
    skyModel_.unload();
    pointerModel_.unload();
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
    tagSendCooldown_ -= dt;

    // The other participants, from their broadcast poses (same arena frame as ours).
    taggame::Spot others[net::kMinigamePlayers];
    float otherHeights[net::kMinigamePlayers] = {};
    int numOthers = 0;
    if (const net::Minigame* mg = client.minigame()) {
        for (int i = 0; i < mg->numParticipants && i < net::kMinigamePlayers; ++i) {
            const uint32_t avId = mg->participants[i];
            if (avId == 0 || avId == client.avatarId()) continue;
            const net::RemoteToon* toon = client.findToon(avId);
            if (!toon) continue;
            others[numOthers].avId = avId;
            others[numOthers].x = toon->x;
            others[numOthers].y = toon->y;
            otherHeights[numOthers] = toonHeightOf(toon->hasDna ? toon->dna : nullptr);
            numOthers++;
        }
    }

    const uint32_t localAvId = client.avatarId();
    const bool localIt = sim_.itAvId == localAvId;

    // Where to walk: the autoplay follows the chase/flee rules, a player uses the Circle Pad.
    float throttle = 0.0f;
    float wantH = me_.h;
    if (autoPlay_) {
        float goalX = 0.0f;
        float goalY = 0.0f;
        if (taggame::chooseAutoGoal(sim_, localAvId, me_, others, numOthers, goalX, goalY)) {
            const float dx = goalX - me_.x;
            const float dy = goalY - me_.y;
            wantH = taggame::headingTo(me_.x, me_.y, goalX, goalY);
            throttle = taggame::autoThrottle(std::sqrt(dx * dx + dy * dy), taggame::wrapHeading(wantH - me_.h));
        }
    } else {
        const float dx = static_cast<float>(pad.dx) / kPadFull;
        const float dy = static_cast<float>(pad.dy) / kPadFull;
        if (std::fabs(dx) > kPadDeadzone || std::fabs(dy) > kPadDeadzone) {
            // Full stick deflection walks at full speed, as upstream's held keys do.
            throttle = 1.0f;
            wantH = taggame::headingTo(0.0f, 0.0f, dx, dy);
        }
    }
    taggame::stepWalker(me_, throttle, wantH, localIt, dt);

    // Upstream sendTagIfIt: the IT toon that touches another toon tags it; the AI owns the cooldown.
    if (localIt && tagSendCooldown_ <= 0.0f) {
        for (int i = 0; i < numOthers; ++i) {
            const float dx = others[i].x - me_.x;
            const float dy = others[i].y - me_.y;
            if (dx * dx + dy * dy < taggame::kTagContactDist * taggame::kTagContactDist) {
                client.sendTag(others[i].avId);
                tagSendCooldown_ = taggame::kTagSendIntervalSec;
                break;
            }
        }
    }

    // Treasures: grabbed by walking over them.
    for (int i = 0; i < sim_.numTreasures; ++i) {
        if (!sim_.treasures[i].active) continue;
        const float tx = sim_.treasures[i].x - me_.x;
        const float ty = sim_.treasures[i].y - me_.y;
        if (tx * tx + ty * ty < kTreasureGrabSq) sim_.grabTreasure(i);
    }

    // The IT sign sits at the IT toon's height.
    itPointerValid_ = false;
    if (sim_.itAvId != 0) {
        if (localIt) {
            itPointerValid_ = true;
            itPointerX_ = me_.x;
            itPointerY_ = me_.y;
            itPointerZ_ = toonHeightOf(client.avatarDna());
        } else {
            for (int i = 0; i < numOthers; ++i) {
                if (others[i].avId != sim_.itAvId) continue;
                itPointerValid_ = true;
                itPointerX_ = others[i].x;
                itPointerY_ = others[i].y;
                itPointerZ_ = otherHeights[i];
                break;
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

    if (pointerModel_.loaded() && itPointerValid_) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        Mtx_Translate(&world, itPointerX_, itPointerY_, itPointerZ_, true);
        renderer.drawModel(pointerModel_, world);
    }
}

void TagGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (eye) {
        eye->x = me_.x;
        eye->y = me_.y - 30.0f;
        eye->z = 25.0f;
        eye->w = 1.0f;
    }
    if (target) {
        target->x = me_.x;
        target->y = me_.y;
        target->z = 3.0f;
        target->w = 1.0f;
    }
}

void TagGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = me_.x;
    if (y) *y = me_.y;
    if (z) *z = 0.0f;
    if (h) *h = me_.h;
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
