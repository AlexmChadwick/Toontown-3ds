#pragma once

#include <cstdint>
#include "game/panda_rng.h"

namespace tt::game::cannongame {

constexpr float kGameTime = 90.0f;
constexpr int kMaxScore = 23;
constexpr int kMinScore = 5;

constexpr float kCannonRotMin = -20.0f;
constexpr float kCannonRotMax = 20.0f;
constexpr float kCannonRotVel = 15.0f; // degrees/sec
constexpr float kCannonAngleMin = 10.0f;
constexpr float kCannonAngleMax = 85.0f;
constexpr float kCannonAngleVel = 15.0f; // degrees/sec

constexpr float kInitialVelocity = 94.0f;
constexpr float kGravity = 32.0f;
constexpr float kCannonY = -130.0f;
constexpr float kCannonZ = 20.0f;
constexpr float kCannonXSpacing = 12.0f;
constexpr float kTowerHeight = 43.85f;
constexpr float kTowerRadius = 10.5f;
constexpr float kBucketHeight = 36.0f;
constexpr float kTowerYRange = 200.0f;
constexpr float kTowerXRange = 100.0f;
constexpr float kBarrelOffset = 6.0f;
constexpr float kBarrelPivotZ = 1.89703f;

enum class HitType : uint8_t { Ground = 0, Tower = 1, Water = 2 };

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct FlightResult {
    Vec3 startPos;
    Vec3 startVel;
    float timeOfImpact = 0.0f;
    HitType hitWhat = HitType::Ground;
    int score = 0;
};

int calcScore(float landTime);
Vec3 getTowerPosition(RandomNumGen& rng, float difficulty);
float cannonX(int slot, int numPlayers);
FlightResult calcFlight(float cx, float zRot, float angle, const Vec3& towerPos);
Vec3 trajectoryPos(const Vec3& startPos, const Vec3& startVel, float t);
Vec3 trajectoryVel(const Vec3& startVel, float t);

} // namespace tt::game::cannongame
