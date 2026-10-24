#include "game/tag_logic.h"

namespace tt::taggame {

void Simulation::init(uint32_t initialIt) {
    itAvId = initialIt;
    tagBackTimer = 0.0f;
    localScore = 0;
    timeRemaining = kDuration;
    numTreasures = 4;
    treasures[0] = {10.0f, 10.0f, true};
    treasures[1] = {-10.0f, 10.0f, true};
    treasures[2] = {10.0f, -10.0f, true};
    treasures[3] = {-10.0f, -10.0f, true};
}

void Simulation::step(float dt) {
    if (timeRemaining > 0.0f) {
        timeRemaining -= dt;
        if (timeRemaining < 0.0f) timeRemaining = 0.0f;
    }
    if (tagBackTimer > 0.0f) {
        tagBackTimer -= dt;
        if (tagBackTimer < 0.0f) tagBackTimer = 0.0f;
    }
}

bool Simulation::canTag() const {
    return tagBackTimer <= 0.0f;
}

bool Simulation::attemptTag(uint32_t taggerAvId, uint32_t targetAvId) {
    if (taggerAvId != itAvId) return false;
    if (!canTag()) return false;
    if (taggerAvId == targetAvId) return false;

    itAvId = targetAvId;
    tagBackTimer = kTagBackCooldown;
    return true;
}

bool Simulation::grabTreasure(int index) {
    if (index < 0 || index >= numTreasures) return false;
    if (!treasures[index].active) return false;
    treasures[index].active = false;
    localScore += kTreasureScore;
    return true;
}

} // namespace tt::taggame
