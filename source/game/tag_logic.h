// Tag minigame pure logic (no 3DS headers). Tag mechanics, tagback cooldown, and treasure scoring.
#pragma once

#include <cstdint>

namespace tt::taggame {

constexpr float kDuration = 60.0f;
constexpr float kTagBackCooldown = 2.0f;
constexpr float kArenaRadius = 35.0f;
constexpr int kTreasureScore = 2;
constexpr int kMaxTreasures = 16;

struct Treasure {
    float x = 0;
    float y = 0;
    bool active = false;
};

struct Simulation {
    uint32_t itAvId = 0;
    float tagBackTimer = 0.0f;
    int localScore = 0;
    float timeRemaining = kDuration;
    Treasure treasures[kMaxTreasures];
    int numTreasures = 0;

    void init(uint32_t initialIt);
    void step(float dt);
    bool canTag() const;
    bool attemptTag(uint32_t taggerAvId, uint32_t targetAvId);
    bool grabTreasure(int index);
};

} // namespace tt::taggame
