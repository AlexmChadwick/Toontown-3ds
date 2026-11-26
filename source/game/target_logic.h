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
    void initGenrand(uint32_t s); // MT init_genrand, as Panda's Mersenne(seed)
    uint32_t gen32();
    double random(); // 53-bit float in [0.0, 1.0)
    uint32_t getrandbits(int k); // k <= 32
    int randbelow(int n);        // random._randbelow, as random.choice uses
};

// Sets up target positions from the seed matching DistributedTargetGameAI.setupTargets.
int setupTargets(uint32_t seed, uint32_t safezoneId, TargetRing* outTargets, int maxTargets);

// The client's setupTargets also places the jump trampolines and 30 cacti from the same seed.
constexpr int kMaxJumps = 4;
constexpr int kNumCacti = 30;
struct Cactus {
    float x, y, h, scale;
    int kind; // 0 cactus_a, 1 cactus_b
};
struct Field {
    struct { float x, y; } jumps[kMaxJumps];
    int numJumps = 0;
    float jumpSize = 0;
    Cactus cacti[kNumCacti];
};
// As setupTargets, plus the field; outTypes (if set) gets each ring's target type.
int setupField(uint32_t seed, uint32_t safezoneId, TargetRing* outTargets, int maxTargets, Field* field,
               int* outTypes);

// Evaluates landing position (x, y) against placed targets, returning top score (minimum 1).
int evaluateScore(const TargetRing* targets, int numTargets, float scoreX, float scoreY);

// Flight state: DistributedTargetGame's prelaunch / launch (Aim), fly, fall, bouncing and score
// (Landed) as __updateLocalToonTask moves the local Toon.
enum class FlightMode : uint8_t {
    Aim,
    Fly,
    Fall,
    Bouncing,
    Landed
};

constexpr float kToonStartX = 10.0f;  // getToonPlace: placeShift 10 (one Toon), TOON_Y 0, z 1
constexpr float kToonStartZ = 1.0f;
constexpr float kTrampZ = 3.6f;       // defineConstants trampZ
constexpr float kMaxFieldSpan = 135.0f; // TargetGameGlobals.MAX_FIELD_SPAN
constexpr float kMaxDist = 3.0f * kEnvironLength; // onstage: (maxI - 1) * ENVIRON_LENGTH
constexpr float kToonSphere = 1.5f;   // TargetGameGlobals.CollisionRadius

struct FlightState {
    FlightMode mode = FlightMode::Aim;
    float x = kToonStartX;
    float y = 0;
    float z = kToonStartZ;
    float speedForward = 0;
    float lateral = 0;
    float zVel = 0;
    float driftX = 0;
    float driftY = 0;
    float gravity = kGravityFly;
    float power = 60.0f;    // 40..120 (the power bar; the Circle Pad stands in for the key mashing)
    float aimLateral = 0;   // launchLaterial, -MAX_LAT..MAX_LAT
    float stretchY = 10.0f; // startPower; the Toon is pulled back stretchY * 0.12 into the band
    int trampBounces = 0;

    void launch();
    void openUmbrella();
    // onTramp: the Toon's sphere is inside a jump circle's (enterJump / exitJump).
    void step(float inputSteerX, float inputSteerY, float dt, bool onTramp = false);
};

} // namespace tt::targetgame
