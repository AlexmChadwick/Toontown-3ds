// Host test for BattleCamera: MovieCamera shot choice, determinism, framing, and coordinate math.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "world/battle_camera.h"
#include "world/battle.h"

namespace tt::world {

// Stub implementations of Battle methods called by battle_camera.cpp
bool Battle::actorPos(uint32_t doId, float* x, float* y, float* z, float* h, bool* isToon) const {
    if (doId == 100) { // Toon 1
        if (x) *x = 0.0f;
        if (y) *y = -6.0f;
        if (z) *z = 0.0f;
        if (h) *h = 0.0f;
        if (isToon) *isToon = true;
        return true;
    }
    if (doId == 200) { // Cog 1
        if (x) *x = 0.0f;
        if (y) *y = 6.0f;
        if (z) *z = 0.0f;
        if (h) *h = 180.0f;
        if (isToon) *isToon = false;
        return true;
    }
    // Default / battle center
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    if (z) *z = 0.0f;
    if (h) *h = 0.0f;
    if (isToon) *isToon = false;
    return true;
}

float Battle::actorHeight(uint32_t doId) const {
    if (doId == 100) return 4.0f;
    if (doId == 200) return 6.0f;
    return 4.5f;
}

} // namespace tt::world

namespace {

constexpr float kEps = 0.0001f;

bool approx(float a, float b, float eps = kEps) {
    return std::fabs(a - b) <= eps;
}

float distance3d(float x1, float y1, float z1, float x2, float y2, float z2) {
    const float dx = x2 - x1, dy = y2 - y1, dz = z2 - z1;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void testBattleCamRng() {
    printf("Testing BattleCamRng determinism and reproducibility...\n");

    tt::world::BattleCamRng rng1(0x12345678);
    tt::world::BattleCamRng rng2(0x12345678);

    // 1. Same seed must produce identical numbers
    for (int i = 0; i < 50; ++i) {
        uint32_t val1 = rng1.next();
        uint32_t val2 = rng2.next();
        assert(val1 == val2);

        float f1 = rng1.rand01();
        float f2 = rng2.rand01();
        assert(f1 >= 0.0f && f1 < 1.0f);
        assert(f1 == f2);

        int c1 = rng1.choice(5);
        int c2 = rng2.choice(5);
        assert(c1 >= 0 && c1 < 5);
        assert(c1 == c2);
    }

    // 2. Different seeds produce distinct sequences
    tt::world::BattleCamRng rng3(0x87654321);
    bool anyDifferent = false;
    for (int i = 0; i < 10; ++i) {
        if (rng1.next() != rng3.next()) {
            anyDifferent = true;
            break;
        }
    }
    assert(anyDifferent);

    printf("  BattleCamRng tests passed.\n");
}

void testModeTransitions() {
    printf("Testing BattleCamera mode transitions...\n");

    tt::world::BattleCamera cam;
    assert(cam.numShots() == 0);

    // 1. WaitForInput
    cam.startWaitForInput();
    assert(cam.currentMode() == tt::world::BattleCamShotKind::WaitForInput);
    assert(cam.numShots() == 0); // WaitForInput uses default overhead shot, no scripted shots

    // 2. FaceOff
    cam.startFaceOff(200, 1000, 3500);
    assert(cam.currentMode() == tt::world::BattleCamShotKind::FaceOffTaunt);

    // 3. Reward
    cam.startReward(100, 5000, 8000);
    assert(cam.currentMode() == tt::world::BattleCamShotKind::Reward);

    printf("  Mode transition tests passed.\n");
}

void testShotChoiceGenerators() {
    printf("Testing MovieCamera shot choice generators...\n");

    tt::world::BattleCamera cam;

    // 1. Heal shot (normal and uber)
    cam.reset();
    cam.chooseHealShot(100, 1000, 3200, false);
    assert(cam.numShots() >= 1);
    for (int i = 0; i < cam.numShots(); ++i) {
        assert(cam.shot(i).kind == tt::world::BattleCamShotKind::HeldBattle ||
               cam.shot(i).kind == tt::world::BattleCamShotKind::HeldAvatar ||
               cam.shot(i).kind == tt::world::BattleCamShotKind::FocusPoint ||
               cam.shot(i).kind == tt::world::BattleCamShotKind::Shake);
    }

    cam.reset();
    cam.chooseHealShot(100, 1000, 6000, true);
    assert(cam.numShots() >= 2);

    // 2. Trap shot
    cam.reset();
    cam.chooseTrapShot(100, 2000, 3500);
    assert(cam.numShots() >= 1);
    assert(cam.shot(0).kind == tt::world::BattleCamShotKind::HeldBattle ||
           cam.shot(0).kind == tt::world::BattleCamShotKind::FocusPoint);

    // 3. Lure shot
    cam.reset();
    cam.chooseLureShot(100, 3000, 4500, false);
    assert(cam.numShots() >= 1);

    cam.reset();
    cam.chooseLureShot(100, 3000, 6000, true); // has train trap
    assert(cam.numShots() >= 2);

    // 4. Sound shot
    cam.reset();
    cam.chooseSoundShot(100, 200, 4000, 3500, 1, 1, false);
    assert(cam.numShots() >= 1);

    // 5. Throw shot
    cam.reset();
    cam.chooseThrowShot(100, 200, 5000, 3500, 1, 1);
    assert(cam.numShots() >= 1);

    // 6. Squirt shot
    cam.reset();
    cam.chooseSquirtShot(100, 200, 6000, 3300, 1, 1);
    assert(cam.numShots() >= 1);

    // 7. Drop shot
    cam.reset();
    cam.chooseDropShot(100, 200, 7000, 4500, 1, 1);
    assert(cam.numShots() >= 1);

    // 8. Suit attack shot
    cam.reset();
    cam.chooseSuitShot(0, 200, 100, 8000, 2500, false);
    assert(cam.numShots() >= 1);

    // 9. SOS shot
    cam.reset();
    cam.chooseSOSShot(100, 9000, 4000);
    assert(cam.numShots() >= 1);

    printf("  Shot choice generator tests passed.\n");
}

void testMovieDeterminism() {
    printf("Testing full movie build determinism and repeatability across rounds...\n");

    tt::world::Battle battle;
    tt::net::BattleMovie movie;
    std::memset(&movie, 0, sizeof(movie));
    movie.numToons = 2;
    movie.toons[0] = 100;
    movie.toons[1] = 101;
    movie.numSuits = 1;
    movie.suits[0] = 200;

    // Toon 0 attacks with Throw (track 4, level 2)
    movie.toonAttacks[0].toonIdx = 0;
    movie.toonAttacks[0].track = 4;
    movie.toonAttacks[0].level = 2;
    movie.toonAttacks[0].target = 200;
    movie.toonAttacks[0].hp[0] = 15;
    movie.toonAttacks[0].numHp = 1;

    // Toon 1 attacks with Sound (track 3, level 1)
    movie.toonAttacks[1].toonIdx = 1;
    movie.toonAttacks[1].track = 3;
    movie.toonAttacks[1].level = 1;
    movie.toonAttacks[1].target = 200;
    movie.toonAttacks[1].hp[0] = 6;
    movie.toonAttacks[1].numHp = 1;

    // Other attack slots empty
    movie.toonAttacks[2].track = -1;
    movie.toonAttacks[3].track = -1;

    // Cog attacks Toon 0
    movie.suitAttacks[0].suitIdx = 0;
    movie.suitAttacks[0].attack = 2;
    movie.suitAttacks[0].target = 0;
    movie.suitAttacks[0].hp[0] = 5;
    movie.suitAttacks[0].numHp = 1;
    for (int i = 1; i < 4; ++i) movie.suitAttacks[i].attack = -1;

    tt::world::BattleCamera camA;
    tt::world::BattleCamera camB;

    // Same battleId and round -> must be identical
    camA.buildMovie(12345, 1, battle, movie, 1000);
    camB.buildMovie(12345, 1, battle, movie, 1000);

    assert(camA.numShots() > 0);
    assert(camA.numShots() == camB.numShots());
    for (int i = 0; i < camA.numShots(); ++i) {
        const auto& sA = camA.shot(i);
        const auto& sB = camB.shot(i);
        assert(sA.kind == sB.kind);
        assert(sA.startMs == sB.startMs);
        assert(sA.durationMs == sB.durationMs);
        assert(sA.targetActorId == sB.targetActorId);
        assert(approx(sA.x, sB.x));
        assert(approx(sA.y, sB.y));
        assert(approx(sA.z, sB.z));
        assert(approx(sA.h, sB.h));
        assert(approx(sA.p, sB.p));
        assert(approx(sA.r, sB.r));
        assert(approx(sA.targetX, sB.targetX));
        assert(approx(sA.targetY, sB.targetY));
        assert(approx(sA.targetZ, sB.targetZ));
    }

    // Different round -> distinct sequence, but still valid
    tt::world::BattleCamera camC;
    camC.buildMovie(12345, 2, battle, movie, 1000);
    assert(camC.numShots() > 0);
    // At least some parameters (e.g. random camera position or angle) should differ
    bool anyDifference = false;
    if (camA.numShots() != camC.numShots()) {
        anyDifference = true;
    } else {
        for (int i = 0; i < camA.numShots(); ++i) {
            if (!approx(camA.shot(i).x, camC.shot(i).x) ||
                !approx(camA.shot(i).z, camC.shot(i).z)) {
                anyDifference = true;
                break;
            }
        }
    }
    assert(anyDifference);

    printf("  Movie determinism tests passed.\n");
}

void testSamplingAndFraming() {
    printf("Testing camera sampling, 400x240 framing scale, and coordinate validation...\n");

    tt::world::Battle battle;
    tt::world::BattleCamera cam;

    // 1. WaitForInput sampling
    cam.startWaitForInput();
    float eyeX = 0, eyeY = 0, eyeZ = 0, tgtX = 0, tgtY = 0, tgtZ = 0;
    cam.sample(1000, battle, &eyeX, &eyeY, &eyeZ, &tgtX, &tgtY, &tgtZ);
    // Overhead camera: eye should be behind/above, target near battle center
    assert(eyeZ > 5.0f);
    assert(approx(tgtX, 0.0f));
    assert(std::fabs(tgtY) < 3.0f);
    assert(tgtZ < eyeZ);
    float dist = distance3d(eyeX, eyeY, eyeZ, tgtX, tgtY, tgtZ);
    assert(dist > 5.0f && dist < 50.0f);

    // 2. FaceOff Taunt sampling
    cam.startFaceOff(200, 1000, 3500);
    cam.sample(1500, battle, &eyeX, &eyeY, &eyeZ, &tgtX, &tgtY, &tgtZ);
    // Should frame Cog 200 (at y=6.0f)
    assert(std::isfinite(eyeX) && std::isfinite(eyeY) && std::isfinite(eyeZ));
    assert(std::isfinite(tgtX) && std::isfinite(tgtY) && std::isfinite(tgtZ));
    dist = distance3d(eyeX, eyeY, eyeZ, tgtX, tgtY, tgtZ);
    assert(dist > 1.0f && dist < 30.0f);

    // 3. FaceOff Walk sampling
    cam.sample(1000 + 3500 + 500, battle, &eyeX, &eyeY, &eyeZ, &tgtX, &tgtY, &tgtZ);
    assert(std::isfinite(eyeX) && std::isfinite(eyeY) && std::isfinite(eyeZ));
    assert(std::isfinite(tgtX) && std::isfinite(tgtY) && std::isfinite(tgtZ));

    // 4. Reward sampling
    cam.startReward(100, 5000, 8000);
    cam.sample(7000, battle, &eyeX, &eyeY, &eyeZ, &tgtX, &tgtY, &tgtZ);
    assert(std::isfinite(eyeX) && std::isfinite(eyeY) && std::isfinite(eyeZ));
    assert(std::isfinite(tgtX) && std::isfinite(tgtY) && std::isfinite(tgtZ));
    dist = distance3d(eyeX, eyeY, eyeZ, tgtX, tgtY, tgtZ);
    assert(dist > 1.0f && dist < 30.0f);

    printf("  Camera sampling and framing tests passed.\n");
}

} // namespace

int main() {
    printf("=== Battle Camera Host Test ===\n");
    testBattleCamRng();
    testModeTransitions();
    testShotChoiceGenerators();
    testMovieDeterminism();
    testSamplingAndFraming();
    printf("All battle camera host tests passed successfully!\n");
    return 0;
}
