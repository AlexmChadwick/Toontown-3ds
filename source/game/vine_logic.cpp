#include "game/vine_logic.h"

#include <cmath>
#include <algorithm>

namespace tt::game::vine {

float getVineAngle(float swingTime, float period, float maxAngleRad) {
    if (period <= 0.0f) period = 3.0f;
    const float omega = (2.0f * 3.14159265f) / period;
    float val = maxAngleRad * std::sin(omega * swingTime);
    if (std::abs(val) < 1e-5f) val = 0.0f;
    return val;
}

void getVineToonPos(int vineIndex, float vineT, float angleRad, float& outX, float& outZ) {
    const float pivotX = vineIndex * kVineSpacing;
    const float pivotZ = kVineTopZ;
    const float length = vineT * kVineHeight;

    outX = pivotX + length * std::sin(angleRad);
    outZ = pivotZ - length * std::cos(angleRad);
}

void jumpFromVine(VineToonState& s, float angleRad, float angularVel) {
    s.state = State::Jumping;
    const float length = s.vineT * kVineHeight;
    // Tangential speed
    const float tangSpeed = length * angularVel;

    // Velocity decomposition + jump impulse forward
    s.velX = tangSpeed * std::cos(angleRad) + 12.0f;
    s.velZ = -tangSpeed * std::sin(angleRad) + 6.0f;
    if (s.velX < 4.0f) s.velX = 4.0f;
}

void updateJump(VineToonState& s, float dt) {
    if (s.state != State::Jumping) return;

    s.velZ += kGravity * dt;
    s.posX += s.velX * dt;
    s.posZ += s.velZ * dt;

    // Check fell below jungle floor
    if (s.posZ < -5.0f) {
        s.state = State::Falling;
        // Respawn at current vine
        s.posZ = 15.0f;
        s.velX = 0.0f;
        s.velZ = 0.0f;
        s.state = State::OnVine;
        s.swingTime = 0.0f;
    }
}

bool checkVineCatch(VineToonState& s) {
    if (s.state != State::Jumping) return false;

    // Next vine target
    const int nextVine = s.currentVine + 1;
    if (nextVine >= kNumVines) {
        // Reached end platform
        if (s.posX >= (kNumVines - 1) * kVineSpacing - 2.0f && s.posZ >= 5.0f) {
            s.state = State::Finished;
            s.currentVine = kNumVines - 1;
            return true;
        }
        return false;
    }

    const float targetX = nextVine * kVineSpacing;
    // Catch window: within 4.0 units horizontally and height between 4 and 28
    if (std::abs(s.posX - targetX) < 4.0f && s.posZ >= 4.0f && s.posZ <= 28.0f) {
        s.currentVine = nextVine;
        s.state = (nextVine == kNumVines - 1) ? State::Finished : State::OnVine;
        s.vineT = (kVineTopZ - s.posZ) / kVineHeight;
        if (s.vineT < 0.2f) s.vineT = 0.2f;
        else if (s.vineT > 0.85f) s.vineT = 0.85f;
        s.swingTime = 0.25f; // start with slight swing momentum
        s.velX = 0.0f;
        s.velZ = 0.0f;
        return true;
    }

    return false;
}

int calculateScore(int treasures, float timeLeft, bool reachedEnd) {
    int score = treasures;
    if (reachedEnd && timeLeft > 0.0f) {
        int bonus = int(kBaseBonus + kBonusPerSecond * timeLeft);
        if (bonus < 0) bonus = 0;
        score += bonus;
    }
    return (score < 1) ? 1 : score;
}

} // namespace tt::game::vine
