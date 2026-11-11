#include "game/target_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "game/target_scene.h"

namespace tt::game {

bool TargetGame::init(const char* root, uint32_t seed, uint32_t safezone) {
    // tools/pipeline/bake_target_scene.py: DistributedTargetGame.load / onstage / setupTargets.
    char path[256];
    const struct { render::Model* model; const char* name; } parts[] = {
        {&skyModel_, "target_sky"},           {&sandModel_, "target_sand"},
        {&mountModel_[0], "target_mount_left"}, {&mountModel_[1], "target_mount_right"},
        {&cactusModel_[0], "target_cactus_a"},  {&cactusModel_[1], "target_cactus_b"},
        {&trampModel_, "target_tramp"}};
    for (const auto& part : parts) {
        std::snprintf(path, sizeof(path), "%s%s.ttm", root, part.name);
        assetBatch_.load(*part.model, path);
    }
    unsigned hood = 2000;
    for (const auto& p : targetgame::scene::kPatterns)
        if (p.hood == safezone) hood = safezone;
    for (int t = 0; t < kRingTypes; ++t) {
        std::snprintf(path, sizeof(path), "%starget_ring_%u_%d.ttm", root, hood, t);
        assetBatch_.load(ringModel_[t], path);
    }

    numTargets_ = targetgame::setupField(seed, safezone, targets_, targetgame::kMaxTargets, &field_, targetTypes_);
    flight_ = targetgame::FlightState{};
    flight_.mode = targetgame::FlightMode::Aim;
    score_ = 0;
    round_ = 1;
    isDone_ = false;
    scoreReported_ = false;

    TT_LOG("target game: init seed=%lu safezone=%lu targets=%d", static_cast<unsigned long>(seed), static_cast<unsigned long>(safezone), numTargets_);
    return true;
}

void TargetGame::nextRound() {
    // DistributedTargetGame.setRoundDone: reset the slingshot for the AI's next round.
    ++round_;
    flight_ = targetgame::FlightState{};
    scoreReported_ = false;
    isDone_ = false;
}

void TargetGame::shutdown() {
    assetBatch_.clear();
    skyModel_.unload();
    sandModel_.unload();
    for (auto& m : mountModel_) m.unload();
    for (auto& m : cactusModel_) m.unload();
    trampModel_.unload();
    for (auto& m : ringModel_) m.unload();
    isDone_ = true;
}

void TargetGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)nowMs;
    float steerX = pad.dx / 156.0f;
    float steerY = pad.dy / 156.0f;
    if (std::abs(steerX) < 0.15f) steerX = 0.0f;
    if (std::abs(steerY) < 0.15f) steerY = 0.0f;

    if (flight_.mode == targetgame::FlightMode::Aim) {
        flight_.step(steerX, steerY, dt);
        if (keysDown & KEY_A) {
            flight_.launch();
            TT_LOG("target game: launched power=%.1f lateral=%.1f", flight_.power, flight_.aimLateral);
        }
    } else if (flight_.mode == targetgame::FlightMode::Fly) {
        if ((keysDown & KEY_A) || (keysDown & KEY_DDOWN) || steerY < -0.5f) {
            flight_.openUmbrella();
            TT_LOG("target game: umbrella opened at y=%.1f z=%.1f", flight_.y, flight_.z);
        }
        flight_.step(steerX, steerY, dt);
    } else if (flight_.mode == targetgame::FlightMode::Fall) {
        flight_.step(steerX, steerY, dt);
        if (flight_.mode == targetgame::FlightMode::Landed && !scoreReported_) {
            scoreReported_ = true;
            score_ = targetgame::evaluateScore(targets_, numTargets_, flight_.x, flight_.y);
            client.sendTargetScore(static_cast<int32_t>(flight_.x), static_cast<int32_t>(flight_.y));
            client.sendTargetPlayerDone();
            isDone_ = true;
            TT_LOG("target game: landed at (%.1f, %.1f) score=%d", flight_.x, flight_.y, score_);
        }
    }
}

namespace {
// A baked placement (row vectors, column 3 the translation) as a world matrix.
C3D_Mtx placed(const float* m) {
    C3D_Mtx w;
    Mtx_Identity(&w);
    for (int r = 0; r < 3; ++r) {
        w.r[r].x = m[r * 4 + 0];
        w.r[r].y = m[r * 4 + 1];
        w.r[r].z = m[r * 4 + 2];
        w.r[r].w = m[r * 4 + 3];
    }
    return w;
}
} // namespace

void TargetGame::draw(render::Renderer& renderer) {
    // onstage: six rows of seven desert blocks (sand left out of the outer columns), a mountain
    // in the second column out on each side; the renderer culls each copy.
    for (const auto& b : targetgame::scene::kBlocks) {
        const C3D_Mtx w = placed(b.m);
        if (skyModel_.loaded()) renderer.drawModel(skyModel_, w);
        if (b.kind && sandModel_.loaded()) renderer.drawModel(sandModel_, w);
    }
    for (const auto& m : targetgame::scene::kMountains) {
        render::Model& model = mountModel_[m.kind];
        if (model.loaded()) renderer.drawModel(model, placed(m.m));
    }
    C3D_Mtx w;
    // setupTargets: each target's circle and rings, trampolines over their jump circles, cacti.
    for (int i = 0; i < numTargets_; ++i) {
        if (i > 0 && targets_[i].x == targets_[i - 1].x && targets_[i].y == targets_[i - 1].y) continue;
        render::Model& ring = ringModel_[targetTypes_[i] < 4 ? targetTypes_[i] : 3];
        if (!ring.loaded()) continue;
        Mtx_Identity(&w);
        Mtx_Translate(&w, targets_[i].x, targets_[i].y, 0.0f, true);
        renderer.drawModel(ring, w);
    }
    const float latScale = field_.jumpSize / 6.8f; // normJumpSize
    for (int i = 0; i < field_.numJumps; ++i) {
        Mtx_Identity(&w);
        Mtx_Translate(&w, field_.jumps[i].x, field_.jumps[i].y, 0.0f, true);
        if (ringModel_[4].loaded()) renderer.drawModel(ringModel_[4], w);
        Mtx_Translate(&w, 0.0f, 0.0f, 0.05f, true);
        Mtx_Scale(&w, latScale, latScale, 1.0f);
        if (trampModel_.loaded()) renderer.drawModel(trampModel_, w);
    }
    for (const auto& c : field_.cacti) {
        render::Model& model = cactusModel_[c.kind];
        if (!model.loaded()) continue;
        Mtx_Identity(&w);
        Mtx_Translate(&w, c.x, c.y, 0.0f, true);
        Mtx_RotateZ(&w, c.h * 0.0174532925f, true);
        Mtx_Scale(&w, c.scale, c.scale, c.scale);
        renderer.drawModel(model, w);
    }
}

// The stock camera of each phase, without its 2.5 s blends: enterPrelaunch toon + (0, -25, 7)
// at p -15, enterFly riding the Toon at (0, CAMERA_Y + 12, 5) p -15, enterFall (0, -10, 15)
// p -68; camLens.setFov(80) throughout (MinigameSession::applyWorld).
void TargetGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (!eye || !target) return;
    float dy = -25.0f, dz = 7.0f, pitch = -15.0f;
    if (flight_.mode == targetgame::FlightMode::Fly) dy = -13.0f, dz = 5.0f;
    else if (flight_.mode != targetgame::FlightMode::Aim) dy = -10.0f, dz = 15.0f, pitch = -68.0f;
    const float p = pitch * 0.0174532925f;
    *eye = FVec4_New(flight_.x, flight_.y + dy, flight_.z + dz, 1.0f);
    *target = FVec4_New(eye->x, eye->y + std::cos(p) * 20.0f, eye->z + std::sin(p) * 20.0f, 1.0f);
}

void TargetGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = flight_.x;
    if (y) *y = flight_.y;
    if (z) *z = flight_.z;
    if (h) *h = 0.0f; // the stock Toon keeps heading 0 in every phase
}

const char* TargetGame::line(int i) const {
    if (i < 0 || i >= 3) return "";
    switch (i) {
    case 0:
        if (flight_.mode == targetgame::FlightMode::Aim) {
            std::snprintf(lines_[0], sizeof(lines_[0]), "Power: %d%%", static_cast<int>((flight_.power / 120.0f) * 100.0f));
        } else {
            std::snprintf(lines_[0], sizeof(lines_[0]), "Distance: %d ft", static_cast<int>(flight_.y));
        }
        break;
    case 1:
        if (flight_.mode == targetgame::FlightMode::Aim) {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Press A to Launch!");
        } else if (flight_.mode == targetgame::FlightMode::Fly) {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Press A to Open Umbrella!");
        } else if (flight_.mode == targetgame::FlightMode::Fall) {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Circle Pad to Glide");
        } else {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Score: %d", score_);
        }
        break;
    case 2:
        std::snprintf(lines_[2], sizeof(lines_[2]), "Round: %d/3", round_);
        break;
    }
    return lines_[i];
}

} // namespace tt::game
