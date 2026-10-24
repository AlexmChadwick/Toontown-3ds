#pragma once

#include <cstdint>
#include "game/panda_rng.h"

namespace tt::game::ringgame {

constexpr int kNumRingGroups = 16;
constexpr float kMaxToonXZ = 10.0f;
constexpr float kToonSpeed = 10.0f; // units / sec in XZ plane
constexpr float kRingRadius = 3.0f;
constexpr float kRingRadiusSqrd = (3.0f * 1.025f) * (3.0f * 1.025f); // 9.455625f
constexpr float kRingGroupArrivalPeriod = 3.0f; // seconds between ring groups
constexpr float kFirstGroupArrival = 4.0f;      // seconds before first group arrives
constexpr float kRingSwimVel = 20.0f;           // units/sec along Y

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

void getDifficultyPattern(uint32_t hood, RandomNumGen& rng, int* outPattern);
bool checkCollision(float toonX, float toonZ, float ringX, float ringZ);
int calcScore(int ringsCollected, bool perfect, int numPlayers);

// Generates ring positions (x, z) for 16 groups for solo or multiplayer
struct RingGroupData {
    float x = 0.0f;
    float z = 0.0f;
    int colorIndex = 0;
};

void generateRings(uint32_t hood, RandomNumGen& rng, int colorIndex, RingGroupData* outGroups);

} // namespace tt::game::ringgame
