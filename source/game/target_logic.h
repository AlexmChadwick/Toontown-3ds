// Target minigame pure logic (no 3DS headers). Slingshot glide physics, CPython PRNG target
// placement matching DistributedTargetGameAI, and collision detection.
#pragma once

#include <cstdint>

namespace tt::targetgame {

constexpr float kEnvironWidth = 150.0f;
constexpr float kEnvironLength = 300.0f;
constexpr float kFieldWidth = kEnvironWidth * 3.0f;       // 450.0
constexpr float kFieldLength = kEnvironLength * 3.7f;     // 1110.0
constexpr float kMaxLateral = 5.0f;
constexpr float kMaxToonXZ = 15.0f;
constexpr float kSpeedLaunch = 60.0f;
constexpr float kSpeedStall = 30.0f;
constexpr float kAirResistance = 0.1f;
constexpr float kUmbrellaResistance = 0.11f;
constexpr float kGravityFly = 12.5f;
constexpr float kGravityFall = 4.0f;
constexpr float kFlyZStart = 20.0f;

constexpr int kMaxTargets = 64;

struct TargetRing {
    float x = 0;
    float y = 0;
    float radius = 0;
    int value = 0;
};

// CPython MT19937 PRNG state matching Python's `random.seed(seed)` + `random.random()`.
struct Random {
    uint32_t state[624];
    int index = 624;

    void seed(uint32_t s);
    uint32_t gen32();
    double random(); // 53-bit float in [0.0, 1.0)
};

// Sets up target positions from the seed matching DistributedTargetGameAI.setupTargets.
int setupTargets(uint32_t seed, uint32_t safezoneId, TargetRing* outTargets, int maxTargets);

// Evaluates landing position (x, y) against placed targets, returning top score (minimum 1).
int evaluateScore(const TargetRing* targets, int numTargets, float scoreX, float scoreY);

// Flight state for slingshot simulation
enum class FlightMode : uint8_t {
    Aim,
    Fly,
    Fall,
    Landed
};

struct FlightState {
    FlightMode mode = FlightMode::Aim;
    float x = 0;
    float y = 0;
    float z = kFlyZStart;
    float speedForward = 0;
    float lateral = 0;
    float zVel = 0;
    float driftX = 0;
    float driftY = 0;
    float power = 60.0f; // 40..120
    float aimLateral = 0; // -5..5

    void launch();
    void openUmbrella();
    void step(float inputSteerX, float inputSteerY, float dt);
};

} // namespace tt::targetgame
