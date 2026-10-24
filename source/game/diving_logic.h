#pragma once

#include <cstdint>

namespace tt::game::diving {

constexpr float kMinX = -20.0f;
constexpr float kMaxX = 20.0f;
constexpr float kMinZ = -38.0f;
constexpr float kMaxZ = 36.0f;
constexpr float kSeabedZ = -36.0f;
constexpr float kBoatZ = 32.0f;
constexpr float kBoatRadiusX = 6.0f;
constexpr float kGameDuration = 65.0f;
constexpr int kNumSpawners = 6;
constexpr float kSpawnerZ[kNumSpawners] = {25.0f, 16.0f, 6.0f, -4.0f, -15.0f, -23.0f};
constexpr int kSpawnerDir[kNumSpawners] = {1, -1, 1, -1, 1, -1};

struct SwimmerState {
    float x = 0.0f;
    float z = 34.0f;
    float xVel = 0.0f;
    float zVel = 0.0f;
    bool hasTreasure = false;
    float freezeTimer = 0.0f;
};

struct ChestState {
    float x = 0.0f;
    float z = kSeabedZ;
    bool available = true;
};

// Updates swimmer physics: drag, input acceleration, buoyancy, bounds clamping
void updateSwimmer(SwimmerState& s, float inputX, float inputZ, float dt);

// Chest collision check: returns true if chest was picked up
bool checkChestPickup(SwimmerState& s, ChestState& chest);

// Boat recovery check: returns true if treasure was recovered
bool checkBoatRecovery(SwimmerState& s);

// Collision with fish or crab: drops treasure and sets 3.0s freeze
void applyObstacleHit(SwimmerState& s);

// Score formula: solo TTC gives 15 points per recovery, min 5
int calculateScore(int recoveries);

} // namespace tt::game::diving
