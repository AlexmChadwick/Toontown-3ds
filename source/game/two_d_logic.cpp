#include "game/two_d_logic.h"

#include <cmath>

namespace tt::twodgame {

float Stomper::currentZ(float time) const {
    float phase = std::fmod(time + offset, period) / period;
    // Stomper slams down fast (0.0..0.2), stays at bottom (0.2..0.4), rises slowly (0.4..1.0)
    float h = 0.0f;
    if (phase < 0.2f) {
        h = 10.0f * (1.0f - (phase / 0.2f));
    } else if (phase < 0.4f) {
        h = 0.0f;
    } else {
        h = 10.0f * ((phase - 0.4f) / 0.6f);
    }
    return zBase + h;
}

int calculateJellybeans(int rawScore, float timeLeft, bool completed, uint32_t safezoneId) {
    (void)safezoneId;
    int totalRaw = rawScore;
    if (completed && timeLeft > 0.0f) {
        totalRaw += kBaseCompletionBonusTTC + static_cast<int>(kBonusPerSecondLeft * timeLeft);
    }
    if (totalRaw < 0) totalRaw = 0;
    float beans = std::sqrt(static_cast<float>(totalRaw * kScoreMultTTC));
    int finalScore = static_cast<int>(beans + 0.5f);
    return finalScore > 0 ? finalScore : 1;
}

void PlayerPhysics::jump() {
    if (onGround) {
        vz = kJumpVelocity;
        onGround = false;
    }
}

void PlayerPhysics::step(float moveInput, const AABB* platforms, int numPlatforms, float dt) {
    if (moveInput > 0.1f) {
        vx = kRunSpeed;
        facingRight = true;
    } else if (moveInput < -0.1f) {
        vx = -kRunSpeed;
        facingRight = false;
    } else {
        vx = 0.0f;
    }

    x += vx * dt;
    vz -= kGravity * dt;
    z += vz * dt;

    onGround = false;
    // Basic floor check
    if (z <= 0.0f) {
        z = 0.0f;
        vz = 0.0f;
        onGround = true;
    }

    // Platform collisions
    for (int i = 0; i < numPlatforms; i++) {
        const AABB& p = platforms[i];
        if (x >= p.x1 && x <= p.x2) {
            // Landing on top of platform
            if (z >= p.z2 - 1.0f && z <= p.z2 + 1.5f && vz <= 0.0f) {
                z = p.z2;
                vz = 0.0f;
                onGround = true;
            }
        }
    }
}

} // namespace tt::twodgame
