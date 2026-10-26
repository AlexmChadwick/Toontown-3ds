#include "game/cog_thief_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "game/panda_rng.h"

namespace tt::game {

namespace {
// DistributedCogThiefGame.loadCogs suitTypes.
constexpr const char* kCogThiefSuitTypes[] = {"ds", "ac", "bc", "ms"};
constexpr int kNumCogThiefSuitTypes = 4;
} // namespace

void CogThiefGame::chooseSuitTypes(uint32_t gameDoId) {
    // DistributedMinigame.__createRandomNumGen seeds RandomNumGen(doId); loadCogs is its first use.
    RandomNumGen rng(gameDoId);
    for (int i = 0; i < cogthief::kMaxCogs; ++i) {
        const char* type = kCogThiefSuitTypes[rng.choice(kNumCogThiefSuitTypes)];
        std::snprintf(suitTypes_[i], sizeof(suitTypes_[i]), "%s", type);
        if (i + 1 >= sim_.numCogs) break;
    }
}

int CogThiefGame::cogs(MinigameCog* out, int max) const {
    int n = 0;
    for (int i = 0; i < sim_.numCogs && n < max; ++i) {
        MinigameCog& c = out[n++];
        c.x = sim_.cogs[i].pos.x;
        c.y = sim_.cogs[i].pos.y;
        c.z = sim_.cogs[i].pos.z;
        c.h = cogHeading_[i];
        std::snprintf(c.code, sizeof(c.code), "%s", suitTypes_[i][0] ? suitTypes_[i] : "ds");
        c.walking = sim_.cogs[i].stunTimer <= 0.0f;
    }
    return n;
}


bool CogThiefGame::init(const char* root, int numCogs, float cogSpeed) {
    char path[256];
    std::snprintf(path, sizeof(path), "%scogthief_ground.ttm", root);
    assetBatch_.load(groundModel_, path);
    std::snprintf(path, sizeof(path), "%scogthief_barrel.ttm", root);
    assetBatch_.load(barrelModel_, path);
    std::snprintf(path, sizeof(path), "%scogthief_tank.ttm", root);
    assetBatch_.load(tankModel_, path);

    sim_.init(numCogs, cogSpeed);
    tx_ = 0.0f;
    ty_ = 12.0f;
    th_ = 180.0f;
    timer_ = 0.0f;
    score_ = 0;
    isDone_ = false;

    TT_LOG("cog thief: init cogs=%d speed=%.1f", numCogs, cogSpeed);
    return true;
}

void CogThiefGame::shutdown() {
    assetBatch_.clear();
    groundModel_.unload();
    barrelModel_.unload();
    tankModel_.unload();
    isDone_ = true;
}

void CogThiefGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)nowMs;
    if (isDone_) return;

    timer_ += dt;
    float ax = pad.dx / 156.0f;
    float ay = pad.dy / 156.0f;
    float mag = std::sqrt(ax * ax + ay * ay);
    if (mag > 0.15f) {
        float sp = cogthief::kToonSpeed * (mag > 1.0f ? 1.0f : mag);
        tx_ += (ax / mag) * sp * dt;
        ty_ += (ay / mag) * sp * dt;
        th_ = std::atan2(-ax, ay) * 180.0f / 3.14159265f;
    }

    if (tx_ < -cogthief::kStageHalfWidth) tx_ = -cogthief::kStageHalfWidth;
    if (tx_ > cogthief::kStageHalfWidth) tx_ = cogthief::kStageHalfWidth;
    if (ty_ < -cogthief::kStageHalfHeight) ty_ = -cogthief::kStageHalfHeight;
    if (ty_ > cogthief::kStageHalfHeight) ty_ = cogthief::kStageHalfHeight;

    if (keysDown & KEY_A) {
        if (sim_.throwPie(tx_, ty_, th_)) {
            client.sendCogThiefPie(static_cast<int32_t>(timer_ * 100.0f), tx_, ty_, 2.0f, th_);
            TT_LOG("cog thief: threw pie at (%.1f, %.1f)", tx_, ty_);
        }
    }

    sim_.step(dt);
    // CogThief turns to face where it walks (Panda heading: 0 faces +Y).
    for (int i = 0; i < sim_.numCogs; ++i) {
        const float dx = sim_.cogs[i].pos.x - lastCogPos_[i].x;
        const float dy = sim_.cogs[i].pos.y - lastCogPos_[i].y;
        if (dx * dx + dy * dy > 1e-6f) cogHeading_[i] = std::atan2(-dx, dy) * 57.2957795f;
        lastCogPos_[i] = sim_.cogs[i].pos;
    }

    if (timer_ >= cogthief::kGameDuration || sim_.allBarrelsStolen()) {
        isDone_ = true;
        score_ = cogthief::evaluateFinalScore(timer_, sim_.barrelsStolenCount());
        TT_LOG("cog thief: game over time=%.1f stolen=%d score=%d", timer_, sim_.barrelsStolenCount(), score_);
    }
}

void CogThiefGame::draw(render::Renderer& renderer) {
    C3D_Mtx model;
    Mtx_Identity(&model);
    if (groundModel_.loaded()) renderer.drawModel(groundModel_, model);

    if (tankModel_.loaded()) {
        Mtx_Identity(&model);
        Mtx_Translate(&model, 0.0f, 0.0f, 0.0f, false);
        renderer.drawModel(tankModel_, model);
    }

    if (barrelModel_.loaded()) {
        for (int i = 0; i < cogthief::kNumBarrels; i++) {
            if (sim_.barrels[i].stolen) continue;
            Mtx_Identity(&model);
            Mtx_Translate(&model, sim_.barrels[i].pos.x, sim_.barrels[i].pos.y, sim_.barrels[i].pos.z, false);
            renderer.drawModel(barrelModel_, model);
        }
    }
}

void CogThiefGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (!eye || !target) return;
    eye->x = 0.0f;
    eye->y = -42.0f;
    eye->z = 32.0f;
    target->x = 0.0f;
    target->y = 0.0f;
    target->z = 0.0f;
}

void CogThiefGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = tx_;
    if (y) *y = ty_;
    if (z) *z = 0.0f;
    if (h) *h = th_;
}

const char* CogThiefGame::line(int i) const {
    if (i < 0 || i >= 3) return "";
    switch (i) {
    case 0: {
        int saved = cogthief::kNumBarrels - sim_.barrelsStolenCount();
        std::snprintf(lines_[0], sizeof(lines_[0]), "Barrels: %d/%d", saved, cogthief::kNumBarrels);
        break;
    }
    case 1:
        std::snprintf(lines_[1], sizeof(lines_[1]), "Press A to Throw Pie!");
        break;
    case 2: {
        int left = static_cast<int>(cogthief::kGameDuration - timer_);
        std::snprintf(lines_[2], sizeof(lines_[2]), "Time: %d s", left > 0 ? left : 0);
        break;
    }
    }
    return lines_[i];
}

} // namespace tt::game
