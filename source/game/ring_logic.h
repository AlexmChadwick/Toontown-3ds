#pragma once

#include <cstdint>
#include "game/panda_rng.h"

namespace tt::game::ringgame {

constexpr int kNumRingGroups = 16;
constexpr int kMaxRings = 4;          // one per player
constexpr float kMaxToonXZ = 10.0f;   // RingGameGlobals.MAX_TOONXZ
constexpr float kToonSpeed = 10.0f; // units / sec in XZ plane
constexpr float kRingRadius = 3.0f;
constexpr float kRingRadiusSqrd = (3.0f * 1.025f) * (3.0f * 1.025f); // 9.455625f
constexpr float kToonInitialSpacing = 4.0f; // TOON_INITIAL_SPACING
// DistributedRingGame.defineConstants: FAR_PLANE_DIST 150 with CAMERA_Y -15 and TOON_Y 0.
constexpr float kFarPlaneDist = 135.0f;
constexpr float kRingGroupArrivalPeriod = 3.0f; // seconds between ring groups
constexpr float kRingGroupSpacing = kFarPlaneDist / 2.0f;                  // RING_GROUP_SPACING 67.5
constexpr float kRingSwimVel = kRingGroupSpacing / kRingGroupArrivalPeriod; // TOON_SWIM_VEL 22.5
constexpr float kFirstGroupArrival = kFarPlaneDist / kRingSwimVel;         // T_FIRST_RING_GROUP_ARRIVES 6.0

// Ring color definitions
struct RingColor {
    const char* name;
    float r, g, b, a;
};

constexpr RingColor kRingColors[7] = {
    {"Red", 1.0f, 0.4f, 0.2f, 1.0f},
    {"Green", 0.0f, 0.9f, 0.2f, 1.0f},
    {"Orange", 1.0f, 0.5f, 0.25f, 1.0f},
    {"Purple", 1.0f, 0.0f, 1.0f, 1.0f},
    {"White", 1.0f, 1.0f, 1.0f, 1.0f},
    {"Black", 0.0f, 0.0f, 0.0f, 1.0f},
    {"Yellow", 1.0f, 1.0f, 0.2f, 1.0f},
};

// __generateRings: randomNumGen.choice(difficultyPatterns[hood]) (source/game/ring_tables.h).
void getDifficultyPattern(uint32_t hood, RandomNumGen& rng, int* outPattern);
bool checkCollision(float toonX, float toonZ, float ringX, float ringZ);
int calcScore(int ringsCollected, bool perfect, int numPlayers);

// RingTracks: the path one ring follows over a normalized t in [0, 1).
struct RingTrack {
    enum Kind : uint8_t {
        Static, Circle, VertInfinity, HorizInfinity, VertSlot, HorizSlot, PlusUp, PlusDown, PlusLeft, PlusRight
    };
    Kind kind = Static;
    double a = 0, b = 0; // Static: the position; slots: the fixed coordinate
    void eval(double t, double* x, double* z) const;
};

// RingTrackGroup + RingGroup: one ring per player, ring i in colorIndices[i].
struct RingTrackGroup {
    int numRings = 1;
    RingTrack tracks[kMaxRings];
    double trackTOffsets[kMaxRings] = {};
    double period = 1.0;
    bool reverse = false;
    double tOffset = 0.0;
    // RingGroup.setT then Ring.setT: ring i's (x, z) at seconds t, scaled by MAX_TOONXZ.
    void ringPos(int ring, double t, float* x, float* z) const;
};

// RingTrackGroups.getRandomRingTrackGroup(type, numRings, rng).
void getRandomRingTrackGroup(int type, int numRings, RandomNumGen& rng, RingTrackGroup* out);

// The 16 groups in __generateRings order.
void generateRings(uint32_t hood, int numRings, RandomNumGen& rng, RingTrackGroup* outGroups);

} // namespace tt::game::ringgame
