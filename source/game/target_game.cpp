#include "game/target_game.h"

#include <algorithm>
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
    // RubberBand.setup's slingshot and band; onstage picks the umbrella colour with random.randint(0, 3)
    // (unseeded upstream; the target seed picks it here).
    std::snprintf(path, sizeof(path), "%starget_sling.ttm", root);
    assetBatch_.load(slingModel_, path);
    std::snprintf(path, sizeof(path), "%starget_band.ttm", root);
    assetBatch_.load(bandModel_, path);
    std::snprintf(path, sizeof(path), "%starget_umbrella_open_%lu.ttm", root, static_cast<unsigned long>(seed % 4));
    assetBatch_.load(umbrellaOpen_, path);
    std::snprintf(path, sizeof(path), "%starget_umbrella_closed_%lu.ttm", root, static_cast<unsigned long>(seed % 4));
    assetBatch_.load(umbrellaClosed_, path);
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
    slingModel_.unload();
    bandModel_.unload();
    umbrellaOpen_.unload();
    umbrellaClosed_.unload();
    for (auto& h : hands_) h.unload();
    handKey_[0] = 0;
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
    } else if (flight_.mode != targetgame::FlightMode::Landed) {
        if (flight_.mode == targetgame::FlightMode::Fly && ((keysDown & (KEY_A | KEY_DDOWN)) || steerY < -0.5f)) {
            flight_.openUmbrella(); // the down arrow requests 'fall'
            TT_LOG("target game: umbrella opened at y=%.1f z=%.1f", flight_.y, flight_.z);
        }
        // enterJump / exitJump: the Toon's CollisionRadius sphere inside a trampoline's jump sphere.
        bool onTramp = false;
        for (int i = 0; i < field_.numJumps && !onTramp; ++i) {
            const float dx = flight_.x - field_.jumps[i].x, dy = flight_.y - field_.jumps[i].y, dz = flight_.z;
            const float r = field_.jumpSize + targetgame::kToonSphere;
            onTramp = dx * dx + dy * dy + dz * dz < r * r;
        }
        flight_.step(steerX, steerY, dt, onTramp);
        if (flight_.mode == targetgame::FlightMode::Landed && !scoreReported_) {
            // enterScore: the landing spot goes to the AI, which scores it.
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
// RubberBand: the band's base node sits at getToonPlace + (0, 10, 0); the slingshot model is at z -1
// under it and the posts at (+-5.5, 0, 13).
constexpr float kBandX = targetgame::kToonStartX, kBandY = 10.0f, kBandZ = targetgame::kToonStartZ;
constexpr float kPostX = 5.5f, kPostZ = 13.0f;

// Panda's HPR as a column-vector rotation: Rz(h) Rx(p) Ry(r), right-handed.
void rotateHpr(C3D_Mtx* m, float h, float p, float r) {
    constexpr float d = 0.0174532925f;
    Mtx_RotateZ(m, h * d, true);
    Mtx_RotateX(m, p * d, true);
    Mtx_RotateY(m, r * d, true);
}
} // namespace

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
    if (slingModel_.loaded()) {
        Mtx_Identity(&w);
        Mtx_Translate(&w, kBandX, kBandY, kBandZ - 1.0f, true);
        renderer.drawModel(slingModel_, w);
    }
    drawBand(renderer);
}


// RubberBand.redraw: a strip from post 1 through three points around the held Toon (offset
// (0, -1.75, 0), 1.5 up) to post 2, thinner and paler the further it stretches. Each of its four
// quads is the unit band card sheared onto its two vertical edges.
void TargetGame::drawBand(render::Renderer& renderer) {
    if (!bandModel_.loaded()) return;
    float hx = flight_.x - kBandX, hy = flight_.y - kBandY - 1.75f, hz = flight_.z - kBandZ + 1.5f;
    if (hy > 0.0f) { hx = 0.0f; hy = 0.0f; hz = kPostZ; }
    const float hy2 = std::min(hy + 0.25f, 0.0f);
    float thick = 1.0f + hy * (1.0f / 25.0f);
    if (thick < 0.2f) thick = 0.2f;
    const float prop = thick; // bandThickness / bandHeight
    // colorRelax (1, 0.3, 0.2) to colorStrecth (1, 0.6, 0.4); vertex shade 0.5 bottom, 1 top.
    const float shade = 0.75f;
    renderer.setBaseTint(shade, (prop * 0.3f + (1 - prop) * 0.6f) * shade, (prop * 0.2f + (1 - prop) * 0.4f) * shade, 1.0f);
    const float pts[5][3] = {{-kPostX, 0.0f, kPostZ + 0.45f}, {hx - 0.5f, hy2, hz}, {hx, hy, hz},
                             {hx + 0.5f, hy2, hz}, {kPostX, 0.0f, kPostZ + 0.45f}};
    for (int i = 0; i < 4; ++i) {
        const float* a = pts[i];
        const float* b = pts[i + 1];
        C3D_Mtx m;
        Mtx_Identity(&m);
        // Columns: x along the segment, y unused, z the band's thickness; origin at the first edge.
        m.r[0].x = b[0] - a[0]; m.r[1].x = b[1] - a[1]; m.r[2].x = b[2] - a[2];
        m.r[0].z = 0.0f;        m.r[1].z = 0.0f;        m.r[2].z = thick;
        m.r[0].w = kBandX + a[0]; m.r[1].w = kBandY + a[1]; m.r[2].w = kBandZ + a[2];
        renderer.drawModel(bandModel_, m);
    }
    renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
}

void TargetGame::drawHeld(render::Renderer& renderer, const world::ToonAvatar& toon) {
    // enterBouncing / score hide both umbrellas.
    const bool open = flight_.mode == targetgame::FlightMode::Fall;
    if (flight_.mode == targetgame::FlightMode::Bouncing || flight_.mode == targetgame::FlightMode::Landed) return;
    render::Model& model = open ? umbrellaOpen_ : umbrellaClosed_;
    world::ToonAvatar::TorsoFrame torso;
    if (!model.loaded() || !toon.torsoFrame(&torso) || !torso.torsoKey) return;
    static constexpr world::ToonAnim kClips[3] = {world::kAnimSit, world::kAnimSwim, world::kAnimJumpIdle};
    if (std::strncmp(handKey_, torso.torsoKey, sizeof(handKey_))) {
        for (auto& h : hands_) h.unload();
        std::snprintf(handKey_, sizeof(handKey_), "%s", torso.torsoKey);
    }
    int c = 0;
    while (c < 3 && kClips[c] != torso.anim) ++c;
    if (c == 3) return;
    if (!hands_[c].loaded()) {
        char path[96];
        std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/toon/%s_%s.jnt", handKey_,
                      world::kToonAnimNames[kClips[c]]);
        if (!hands_[c].load(path)) return;
    }
    const int right = hands_[c].joint("def_joint_right_hold");
    C3D_Mtx hand, world;
    if (right < 0 || !hands_[c].sample(right, torso.time, &hand)) return;
    Mtx_Multiply(&world, &torso.world, &hand);
    if (open) {
        // enterFall: umbrella hpr (0, 180, -25), scale 1.5; open_umbrella at (0.1, 0.2, -0.1), hpr (-90, 0, 15).
        rotateHpr(&world, 0.0f, 180.0f, -25.0f);
        Mtx_Scale(&world, 1.5f, 1.5f, 1.5f);
        Mtx_Translate(&world, 0.1f, 0.2f, -0.1f, true);
        rotateHpr(&world, -90.0f, 0.0f, 15.0f);
    } else {
        // closed_umbrella: CompassEffect PRot keeps its hpr (0, 100, 35) relative to render.
        C3D_Mtx fixed;
        Mtx_Identity(&fixed);
        Mtx_Translate(&fixed, world.r[0].w, world.r[1].w, world.r[2].w, true);
        rotateHpr(&fixed, 0.0f, 100.0f, 35.0f);
        world = fixed;
    }
    renderer.drawModel(model, world);
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
    const bool aim = flight_.mode == targetgame::FlightMode::Aim; // enterPrelaunch: from the Toon's place
    *eye = aim ? FVec4_New(targetgame::kToonStartX, dy, targetgame::kToonStartZ + dz, 1.0f)
               : FVec4_New(flight_.x, flight_.y + dy, flight_.z + dz, 1.0f);
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
        } else if (flight_.mode == targetgame::FlightMode::Bouncing) {
            std::snprintf(lines_[1], sizeof(lines_[1]), "Bounce!");
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
