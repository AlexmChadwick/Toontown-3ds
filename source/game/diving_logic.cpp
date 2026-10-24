#include "game/diving_logic.h"

#include <cmath>
#include <algorithm>

namespace tt::game::diving {

void updateSwimmer(SwimmerState& s, float inputX, float inputZ, float dt) {
    if (s.freezeTimer > 0.0f) {
        s.freezeTimer -= dt;
        if (s.freezeTimer < 0.0f) s.freezeTimer = 0.0f;
        // In freeze state, cannot accelerate
        inputX = 0.0f;
        inputZ = 0.0f;
    }

    // Velocity drag matching DistributedDivingGame.py:871
    s.xVel *= 0.99f;
    s.zVel *= 0.99f;

    // Apply input acceleration
    constexpr float kAccelX = 35.0f;
    constexpr float kAccelZ = 30.0f;
    s.xVel += inputX * kAccelX * dt;
    s.zVel += inputZ * kAccelZ * dt;

    // Velocity limits matching upstream (x: [-20, 20], z: [-10, 10])
    if (s.xVel > 20.0f) s.xVel = 20.0f;
    else if (s.xVel < -20.0f) s.xVel = -20.0f;
    if (s.zVel > 10.0f) s.zVel = 10.0f;
    else if (s.zVel < -10.0f) s.zVel = -10.0f;

    // Position integration
    s.x += s.xVel * dt;
    s.z += s.zVel * dt;

    // Buoyancy matching DistributedDivingGame.py:916-919:
    // Sinks by -0.8 * dt when carrying treasure, rises by +0.8 * dt when free
    if (s.hasTreasure) {
        s.z -= 0.8f * dt;
    } else {
        s.z += 0.8f * dt;
    }

    // Clamping to pool bounds
    if (s.x < kMinX) { s.x = kMinX; s.xVel = 0.0f; }
    else if (s.x > kMaxX) { s.x = kMaxX; s.xVel = 0.0f; }

    if (s.z < kMinZ) { s.z = kMinZ; s.zVel = 0.0f; }
    else if (s.z > kMaxZ) { s.z = kMaxZ; s.zVel = 0.0f; }
}

bool checkChestPickup(SwimmerState& s, ChestState& chest) {
    if (s.hasTreasure || !chest.available || s.freezeTimer > 0.0f) return false;
    const float dx = s.x - chest.x;
    const float dz = s.z - chest.z;
    constexpr float kPickupRadius = 2.0f;
    if (dx * dx + dz * dz < kPickupRadius * kPickupRadius) {
        s.hasTreasure = true;
        chest.available = false;
        return true;
    }
    return false;
}

bool checkBoatRecovery(SwimmerState& s) {
    if (!s.hasTreasure || s.freezeTimer > 0.0f) return false;
    if (s.z >= kBoatZ && std::abs(s.x) <= kBoatRadiusX) {
        s.hasTreasure = false;
        return true;
    }
    return false;
}

void applyObstacleHit(SwimmerState& s) {
    s.hasTreasure = false;
    s.freezeTimer = 3.0f;
    s.xVel = 0.0f;
    s.zVel = 0.0f;
}

int calculateScore(int recoveries) {
    // Upstream: solo TTC score is 10 * 1.5 * (0.25 + 0.75) = 15 points per recovery, min 5
    int points = recoveries * 15;
    return (points < 5) ? 5 : points;
}

} // namespace tt::game::diving
