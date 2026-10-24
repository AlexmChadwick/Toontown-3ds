#include "game/ring_game.h"

#include <cmath>
#include <cstdio>
#include "core/log.h"

namespace tt::game {

namespace {
constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";
constexpr float kSpacing = ringgame::kRingGroupArrivalPeriod * ringgame::kRingSwimVel; // 60.0f
constexpr float kFirstOffset = ringgame::kFirstGroupArrival * ringgame::kRingSwimVel;   // 80.0f
} // namespace

void RingGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    char path[128];
    snprintf(path, sizeof(path), "%sring_game.ttm", kDir);
    environ_.load(path);
    snprintf(path, sizeof(path), "%sring_ring.ttm", kDir);
    ringModel_.load(path);

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
    RandomNumGen rng(seed);
    const uint32_t hood = mg ? (mg->trolleyZone - mg->trolleyZone % 1000) : 2000;
    const int colorIdx = (mg && slot_ < 4 && mg->ringColorIndices[slot_] >= 0) ? mg->ringColorIndices[slot_] : 0;

    ringgame::generateRings(hood, rng, colorIdx, rings_);

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

    toonX_ = 0.0f;
    toonZ_ = 0.0f;
    toonH_ = 0.0f;

    snprintf(lines_[0], sizeof(lines_[0]), "Swim through the %s rings!", ringgame::kRingColors[colorIdx % 7].name);
    snprintf(lines_[1], sizeof(lines_[1]), "Circle Pad: steer in the water");
    snprintf(lines_[2], sizeof(lines_[2]), "Rings: 0 / 16");

    TT_LOG("ring_game: init slot %d/%d, color %d (%s)", slot_, numPlayers_, colorIdx, ringgame::kRingColors[colorIdx % 7].name);
}

void RingGame::shutdown() {
    C3D_FrameSync();
    environ_.unload();
    ringModel_.unload();
}

void RingGame::update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs) {
    (void)kDown;
    const float t = static_cast<int>(nowMs - startMs_) / 1000.0f;

    // Toon movement
    float ax = pad.dx / 156.0f;
    float az = pad.dy / 156.0f;

    // AutoPlay: steer towards upcoming ring
    if (autoPlay_ && nextGroup_ < ringgame::kNumRingGroups) {
        const float dx = rings_[nextGroup_].x - toonX_;
        const float dz = rings_[nextGroup_].z - toonZ_;
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
        toonH_ = -ax * 20.0f;
    } else {
        toonH_ = 0.0f;
    }

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
            const bool hit = ringgame::checkCollision(toonX_, toonZ_, rings_[i].x, rings_[i].z);
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

void RingGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    *eye = FVec3_New(toonX_ * 0.3f, -15.0f, toonZ_ * 0.3f + 1.0f);
    *target = FVec3_New(toonX_ * 0.5f, 40.0f, toonZ_ * 0.5f);
}

void RingGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    Mtx_Identity(&m);

    // Underwater environment
    if (environ_.loaded()) {
        renderer.drawModel(environ_, m);
    }

    // Rings
    if (ringModel_.loaded()) {
        const float t = static_cast<int>(tt::net::plat::nowMs() - startMs_) / 1000.0f;
        for (int i = 0; i < ringgame::kNumRingGroups; ++i) {
            const float ringY = kFirstOffset + i * kSpacing - ringgame::kRingSwimVel * t;
            // Only draw rings in front of camera and within visible range
            if (ringY < -10.0f || ringY > 150.0f) continue;
            Mtx_Identity(&m);
            Mtx_Translate(&m, rings_[i].x, ringY, rings_[i].z, true);
            renderer.drawModel(ringModel_, m);
        }
    }
}

} // namespace tt::game
