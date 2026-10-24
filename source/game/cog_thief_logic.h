// Cog Thief minigame pure logic (no 3DS headers). Cog movement, pie trajectory, barrel
// tracking, and scoring formula matching DistributedCogThiefGameAI.
#pragma once

#include <cstdint>

namespace tt::cogthief {

constexpr int kNumBarrels = 4;
constexpr int kMaxCogs = 8;
constexpr float kGameDuration = 60.0f;
constexpr float kToonSpeed = 9.0f;
constexpr float kCogSpeedTTC = 6.0f;
constexpr float kStageHalfWidth = 25.0f;
constexpr float kStageHalfHeight = 18.0f;
constexpr int kMaxScore = 20;
constexpr int kMinScore = 3;

// Barrel positions
struct Vec3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

const Vec3 kBarrelStart[kNumBarrels] = {
    {4.3f, 4.0f, 0.0f},
    {4.3f, -4.0f, 0.0f},
    {-4.3f, 4.0f, 0.0f},
    {-4.3f, -4.0f, 0.0f}
};

const Vec3 kCogStart[kMaxCogs] = {
    {35.0f, 18.0f, 0.0f},
    {35.0f, 0.0f, 0.0f},
    {35.0f, -18.0f, 0.0f},
    {-35.0f, 18.0f, 0.0f},
    {-35.0f, 0.0f, 0.0f},
    {-35.0f, -18.0f, 0.0f},
    {0.0f, 27.0f, 0.0f},
    {0.0f, -27.0f, 0.0f}
};

const Vec3 kCogReturn[6] = {
    {-35.0f, 28.0f, 0.0f},
    {35.0f, 28.0f, 0.0f},
    {35.0f, 0.0f, 0.0f},
    {35.0f, -28.0f, 0.0f},
    {-35.0f, -28.0f, 0.0f},
    {-35.0f, 0.0f, 0.0f}
};

enum class CogGoal : uint8_t {
    None = 0,
    Barrel = 1,
    RunAway = 3,
    Stunned = 4
};

struct Barrel {
    Vec3 pos = {};
    int carriedByCog = -1; // -1 on ground
    bool stolen = false;
};

struct Cog {
    Vec3 pos = {};
    CogGoal goal = CogGoal::None;
    int targetBarrel = -1;
    int returnIndex = -1;
    int carryingBarrel = -1;
    float stunTimer = 0.0f;
};

struct Pie {
    bool active = false;
    Vec3 pos = {};
    Vec3 vel = {};
};

// Calculates base score given time elapsed (0..60).
int calcScore(float timeElapsed);

// Evaluates final score for solo (including perfect bonus if 0 barrels stolen).
int evaluateFinalScore(float timeElapsed, int barrelsStolen);

struct Simulation {
    Barrel barrels[kNumBarrels];
    Cog cogs[kMaxCogs];
    int numCogs = 5;
    float cogSpeed = kCogSpeedTTC;
    Pie pie;

    void init(int cogCount = 5, float speed = kCogSpeedTTC);
    void step(float dt);
    bool throwPie(float toonX, float toonY, float toonHeading);
    int barrelsStolenCount() const;
    bool allBarrelsStolen() const;
};

} // namespace tt::cogthief
