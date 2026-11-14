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
    // DistributedTagTreasure.modelPath: the playground ice cream cone.
    assetBatch_.load(treasureModel_, "romfs:/baked/icecream.ttm");
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
    camHeight_ = std::fmax(toonHeightOf(client.avatarDna()), 3.0f);
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
        // Held arrow keys: full speed, as upstream's walk.
        const float move = dy > kPadDeadzone ? 1.0f : dy < -kPadDeadzone ? -1.0f : 0.0f;
        const float turn = dx > kPadDeadzone ? -1.0f : dx < -kPadDeadzone ? 1.0f : 0.0f;
        taggame::stepTank(me_, move, turn, localIt, dt);
    }
    if (autoPlay_) taggame::stepWalker(me_, throttle, wantH, localIt, dt);

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

    // Treasures: the AI's DistributedTagTreasures (TagTreasurePlannerAI regenerates them); entering
    // one asks for it once (DistributedTreasure.requestGrab), and the score is the AI's.
    sim_.numTreasures = 0;
    uint32_t touching = 0;
    for (int i = 0; i < net::kMaxTreasures; ++i) {
        const net::Treasure& t = client.treasureSlot(i);
        if (!t.active || t.classId != dc::DistributedTagTreasure::kClassId) continue;
        if (!sim_.addTreasure(t.x, t.y)) break;
        const float tx = t.x - me_.x;
        const float ty = t.y - me_.y;
        if (tx * tx + ty * ty < kTreasureGrabSq) touching = t.doId;
    }
    if (touching && touching != lastTouched_) client.sendRequestGrab(touching);
    lastTouched_ = touching;
    if (const net::Minigame* mg = client.minigame())
        for (int i = 0; i < mg->numParticipants && i < net::kMinigamePlayers; ++i)
            if (mg->participants[i] == localAvId) sim_.localScore = mg->tagTreasureScores[i]; // setTreasureScore

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
                // TagTreasurePlannerAI spawns at z 0.1; DistributedTreasure scales 0.9 and the
                // model's card is a BillboardEffect (faces -Y to the eye).
                const C3D_FVec eye = renderer.eye();
                Mtx_Translate(&world, sim_.treasures[i].x, sim_.treasures[i].y, 0.1f, true);
                Mtx_RotateZ(&world, std::atan2(eye.x - sim_.treasures[i].x, -(eye.y - sim_.treasures[i].y)), true);
                Mtx_Scale(&world, 0.9f, 0.9f, 0.9f);
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

// enterPlay: setIdealCameraPos(0, -24, 8) behind the Toon, with camera position 0's look-at
// (0, 1.5, camHeight), so the arena's horizon and sky are in view.
void TagGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    constexpr float kDegToRad = 0.0174532925f;
    const float s = std::sin(me_.h * kDegToRad), c = std::cos(me_.h * kDegToRad);
    // Toon-space (x, y) to world: forward is (-sin h, cos h).
    auto at = [&](float y, float z, C3D_FVec* out) {
        out->x = me_.x - s * y;
        out->y = me_.y + c * y;
        out->z = z;
        out->w = 1.0f;
    };
    if (eye) at(-24.0f, 8.0f, eye);
    if (target) at(1.5f, camHeight_, target);
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
