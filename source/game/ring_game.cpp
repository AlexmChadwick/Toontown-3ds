#include "core/gpu_sync.h"
#include "game/ring_game.h"

#include <cmath>
#include <cstdio>
#include "core/log.h"

namespace tt::game {

namespace {
constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";
constexpr float kSpacing = ringgame::kRingGroupSpacing;                               // 67.5
constexpr float kFirstOffset = ringgame::kFarPlaneDist;                               // 135
// DistributedRingGame.defineConstants: ENVIRON_LENGTH 150, ENVIRON_START_OFFSET 20. The baked
// swimming_game already sits at its upstream placement (0, 75, -14.5).
constexpr float kEnvironLength = 150.0f;
constexpr float kEnvironStartOffset = 20.0f;
constexpr int kEnvironRowCount = 2;
constexpr int kEnvironSideCount = 2; // blocks at +-150 and +-300 (x = 0 is the model)
} // namespace

void RingGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    char path[128];
    snprintf(path, sizeof(path), "%sring_game.ttm", kDir);
    assetBatch_.load(environ_, path);
    snprintf(path, sizeof(path), "%sring_ring.ttm", kDir);
    assetBatch_.load(ringModel_, path);

    const net::Minigame* mg = client.minigame();
    slot_ = 0;
    numPlayers_ = mg ? mg->numParticipants : 1;
    if (numPlayers_ < 1) numPlayers_ = 1;
    if (mg) {
        for (int i = 0; i < numPlayers_; ++i) {
            if (mg->participants[i] == client.avatarId()) slot_ = i;
        }
    }

    uint32_t seed = mg ? mg->doId : 1001;
    RandomNumGen rng(seed); // DistributedMinigame.randomNumGen: __generateRings is its only user
    const uint32_t hood = mg ? (mg->trolleyZone - mg->trolleyZone % 1000) : 2000;
    for (int i = 0; i < ringgame::kMaxRings; ++i)
        colorIndices_[i] = (mg && mg->ringColorIndices[i] >= 0) ? mg->ringColorIndices[i] % 7 : i; // setColorIndices
    const int colorIdx = colorIndices_[slot_ < ringgame::kMaxRings ? slot_ : 0];
    ringgame::generateRings(hood, numPlayers_ < ringgame::kMaxRings ? numPlayers_ : ringgame::kMaxRings, rng, groups_);
    gameStartMs_ = 0;

    for (int i = 0; i < ringgame::kNumRingGroups; ++i) {
        groupPassed_[i] = false;
        groupHit_[i] = false;
    }

    done_ = false;
    autoPlay_ = autoPlay;
    startMs_ = nowMs;
    ringsCollected_ = 0;
    perfect_ = true;
    nextGroup_ = 0;

    // __placeToon: lined up TOON_INITIAL_SPACING apart.
    toonX_ = slot_ * ringgame::kToonInitialSpacing - ringgame::kToonInitialSpacing * (numPlayers_ - 1) / 2.0f;
    toonZ_ = 0.0f;
    toonH_ = 0.0f;

    snprintf(lines_[0], sizeof(lines_[0]), "Swim through the %s rings!", ringgame::kRingColors[colorIdx % 7].name);
    snprintf(lines_[1], sizeof(lines_[1]), "Circle Pad: steer in the water");
    snprintf(lines_[2], sizeof(lines_[2]), "Rings: 0 / 16");

    TT_LOG("ring_game: init slot %d/%d, color %d (%s)", slot_, numPlayers_, colorIdx, ringgame::kRingColors[colorIdx % 7].name);
}

void RingGame::shutdown() {
    assetBatch_.clear();
    ::tt::gpu::frameSync();
    environ_.unload();
    ringModel_.unload();
}

void RingGame::update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs) {
    (void)kDown;
    const net::Minigame* mg = client.minigame();
    if (!gameStartMs_) {
        if (!mg || !mg->gameStartMs) return; // enterSwim starts with setGameStart
        gameStartMs_ = mg->gameStartMs;
    }
    const float t = ringT(nowMs);
    float nextX = 0.0f, nextZ = 0.0f;
    if (nextGroup_ < ringgame::kNumRingGroups) groups_[nextGroup_].ringPos(slot_, t, &nextX, &nextZ);

    // Toon movement
    float ax = pad.dx / 156.0f;
    float az = pad.dy / 156.0f;

    // AutoPlay: steer towards upcoming ring
    if (autoPlay_ && nextGroup_ < ringgame::kNumRingGroups) {
        const float dx = nextX - toonX_;
        const float dz = nextZ - toonZ_;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (dist > 0.2f) {
            ax = dx / dist;
            az = dz / dist;
        } else {
            ax = 0.0f;
            az = 0.0f;
        }
    }

    if (std::abs(ax) > 0.1f) {
        toonX_ += ax * ringgame::kToonSpeed * dt;
        if (toonX_ < -ringgame::kMaxToonXZ) toonX_ = -ringgame::kMaxToonXZ;
        if (toonX_ > ringgame::kMaxToonXZ) toonX_ = ringgame::kMaxToonXZ;
    }
    toonH_ = 0.0f; // __placeToon: hpr (0, 0, 0); steering only moves the Toon in x / z

    if (std::abs(az) > 0.1f) {
        toonZ_ += az * ringgame::kToonSpeed * dt;
        if (toonZ_ < -ringgame::kMaxToonXZ) toonZ_ = -ringgame::kMaxToonXZ;
        if (toonZ_ > ringgame::kMaxToonXZ) toonZ_ = ringgame::kMaxToonXZ;
    }

    // Check ring groups
    for (int i = 0; i < ringgame::kNumRingGroups; ++i) {
        if (groupPassed_[i]) continue;
        const float ringY = kFirstOffset + i * kSpacing - ringgame::kRingSwimVel * t;
        if (ringY <= 0.0f) {
            groupPassed_[i] = true;
            nextGroup_ = i + 1;
            float rx, rz;
            groups_[i].ringPos(slot_, t, &rx, &rz); // ourRing = getRing(avIdList.index(localAvId))
            const bool hit = ringgame::checkCollision(toonX_, toonZ_, rx, rz);
            passedMs_[i] = nowMs;
            groupHit_[i] = hit;
            if (hit) {
                ringsCollected_++;
                client.sendRingToonGotRing(1);
            } else {
                perfect_ = false;
                client.sendRingToonGotRing(0);
            }
            snprintf(lines_[2], sizeof(lines_[2]), "Rings: %d / 16", ringsCollected_);
            TT_LOG("ring_game: passed group %d, hit=%d, total=%d", i, hit ? 1 : 0, ringsCollected_);
        }
    }

    if (nextGroup_ >= ringgame::kNumRingGroups && !done_) {
        done_ = true;
        const int score = ringgame::calcScore(ringsCollected_, perfect_, numPlayers_);
        snprintf(lines_[0], sizeof(lines_[0]), "Course complete! %s", perfect_ ? "PERFECT!" : "");
        snprintf(lines_[1], sizeof(lines_[1]), "You earned %d jellybeans!", score);
    }
}

void RingGame::toonPose(float* x, float* y, float* z, float* h) const {
    *x = toonX_;
    *y = 0.0f;
    *z = toonZ_;
    *h = toonH_;
}

// DistributedRingGame.onstage: the camera rides the Toon at (0, CAMERA_Y -15, 0), level, with
// camLens.setFov(80) (MinigameSession::applyWorld).
void RingGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    *eye = FVec3_New(toonX_, -15.0f, toonZ_);
    *target = FVec3_New(toonX_, 40.0f, toonZ_);
}

void RingGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    Mtx_Identity(&m);

    // Underwater environment: DistributedRingGame.load instances the environ model ('model' at
    // x 0 and 'blocks' at x +-150, +-300) in two rows along Y, and __updateEnvironTask scrolls
    // the whole node by distance = (t * TOON_SWIM_VEL) mod ENVIRON_LENGTH + ENVIRON_START_OFFSET.
    if (environ_.loaded()) {
        const float t = static_cast<int>(tt::net::plat::nowMs() - startMs_) / 1000.0f;
        const float distance = std::fmod(t * ringgame::kRingSwimVel, kEnvironLength) + kEnvironStartOffset;
        for (int row = 0; row < kEnvironRowCount; ++row) {
            const float y = kEnvironLength * row - distance;
            for (int col = -kEnvironSideCount; col <= kEnvironSideCount; ++col) {
                Mtx_Identity(&m);
                Mtx_Translate(&m, kEnvironLength * col, y, 0.0f, true);
                renderer.drawModel(environ_, m);
            }
        }
    }

    // Rings: each group's rings ride its RingTrackGroup, ring i in colorIndices[i]; our ring
    // leaves 0.1 s after the group passes and fades over 0.5 s (__makeRingFadeAway).
    if (ringModel_.loaded() && gameStartMs_) {
        const uint32_t now = tt::net::plat::nowMs();
        const float t = ringT(now);
        for (int i = 0; i < ringgame::kNumRingGroups; ++i) {
            const float ringY = kFirstOffset + i * kSpacing - ringgame::kRingSwimVel * t;
            if (ringY < -10.0f || ringY > 150.0f) continue;
            const ringgame::RingTrackGroup& g = groups_[i];
            for (int r = 0; r < g.numRings; ++r) {
                if (r == slot_ && groupPassed_[i] && int32_t(now - passedMs_[i]) > 600) continue;
                float x, z;
                g.ringPos(r, t, &x, &z);
                const ringgame::RingColor& color = ringgame::kRingColors[colorIndices_[r]];
                renderer.setBaseTint(color.r, color.g, color.b, color.a);
                Mtx_Identity(&m);
                Mtx_Translate(&m, x, ringY, z, true);
                renderer.drawModel(ringModel_, m);
            }
        }
        renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

} // namespace tt::game
