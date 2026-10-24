// Two-D (Toon Escape / ToonBlitz) minigame pure logic (no 3DS headers).
// 2D platforming physics in XZ plane, stomper timing, and score formula matching DistributedTwoDGameAI.
#pragma once

#include <cstdint>

namespace tt::twodgame {

constexpr float kGameDurationTTC = 150.0f;
constexpr int kScoreMultTTC = 5;
constexpr int kBaseCompletionBonusTTC = 15;
constexpr float kBonusPerSecondLeft = 0.8f;
constexpr float kGravity = 28.0f;
constexpr float kJumpVelocity = 18.0f;
constexpr float kRunSpeed = 12.0f;

struct AABB {
    float x1, z1, x2, z2;

    bool contains(float px, float pz) const {
        return px >= x1 && px <= x2 && pz >= z1 && pz <= z2;
    }
};

struct Stomper {
    float x = 0;
    float zBase = 0;
    float period = 2.0f;
    float offset = 0.0f;

    float currentZ(float time) const;
};

// Calculates final jellybean payout given raw score and time left matching DistributedTwoDGameAI.gameOver.
int calculateJellybeans(int rawScore, float timeLeft, bool completed, uint32_t safezoneId = 2000);

struct PlayerPhysics {
    float x = 0.0f;
    float z = 14.0f;
    float vx = 0.0f;
    float vz = 0.0f;
    bool onGround = false;
    bool facingRight = true;

    void jump();
    void step(float moveInput, const AABB* platforms, int numPlatforms, float dt);
};

} // namespace tt::twodgame
