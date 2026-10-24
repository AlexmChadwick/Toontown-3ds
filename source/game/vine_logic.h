#pragma once

#include <cstdint>

namespace tt::game::vine {

constexpr int kNumVines = 20;
constexpr float kVineSpacing = 30.0f;
constexpr float kVineHeight = 30.0f;
constexpr float kVineTopZ = 30.0f;
constexpr float kGameDuration = 70.0f;
constexpr float kBaseBonus = 5.0f;
constexpr float kBonusPerSecond = 0.4f;
constexpr float kGravity = -32.0f;

enum class State : uint8_t {
    OnVine,
    Jumping,
    Falling,
    Finished,
};

struct VineToonState {
    State state = State::OnVine;
    int currentVine = 0;       // 0..19
    float vineT = 0.5f;        // 0..1 along vine
    float swingTime = 0.0f;
    float posX = 0.0f;
    float posZ = 15.0f;
    float velX = 0.0f;
    float velZ = 0.0f;
    int treasuresClaimed = 0;
};

// Computes vine angle (radians) at given time
float getVineAngle(float swingTime, float period = 3.0f, float maxAngleRad = 0.785398f);

// Computes toon position while hanging on vine
void getVineToonPos(int vineIndex, float vineT, float angleRad, float& outX, float& outZ);

// Initiates jump from vine
void jumpFromVine(VineToonState& s, float angleRad, float angularVel);

// Updates free-fall trajectory in air
void updateJump(VineToonState& s, float dt);

// Checks if toon can grab the next vine: returns true if caught
bool checkVineCatch(VineToonState& s);

// Final score calculation matching DistributedVineGameAI.py:checkForEndVine
int calculateScore(int treasures, float timeLeft, bool reachedEnd);

} // namespace tt::game::vine
