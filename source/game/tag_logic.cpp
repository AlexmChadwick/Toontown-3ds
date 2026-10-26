#include "game/tag_logic.h"

#include <cmath>

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

float autoThrottle(float distance, float headingErrDeg) {
    if (distance <= kAutoGoalReach) return 0.0f;
    constexpr float kDegToRad = 0.0174532925f;
    const float align = std::cos(headingErrDeg * kDegToRad);
    if (align <= 0.0f) return 0.0f;
    float t = distance / kAutoSlowRange;
    if (t > 1.0f) t = 1.0f;
    if (t < kAutoMinThrottle) t = kAutoMinThrottle;
    return t * align;
}

float wrapHeading(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg <= -180.0f) deg += 360.0f;
    return deg;
}

float headingTo(float fromX, float fromY, float toX, float toY) {
    constexpr float kRadToDeg = 57.2957795f;
    return std::atan2(-(toX - fromX), toY - fromY) * kRadToDeg;
}

void stepWalker(Walker& w, float throttle, float wantH, bool isIt, float dt) {
    const float speedScale = isIt ? kItSpeedIncrease : 1.0f;
    const float rotScale = isIt ? kItRotIncrease : 1.0f;

    const float err = wrapHeading(wantH - w.h);
    const float maxTurn = kToonRotateSpeed * rotScale * dt;
    float turn = err;
    if (turn > maxTurn) turn = maxTurn;
    if (turn < -maxTurn) turn = -maxTurn;
    w.h = wrapHeading(w.h + turn);

    if (throttle > 0.0f && std::fabs(wrapHeading(wantH - w.h)) < 90.0f) {
        constexpr float kDegToRad = 0.0174532925f;
        const float step = kToonForwardSpeed * speedScale * (throttle > 1.0f ? 1.0f : throttle) * dt;
        w.x += -std::sin(w.h * kDegToRad) * step;
        w.y += std::cos(w.h * kDegToRad) * step;
    }

    const float dist = std::sqrt(w.x * w.x + w.y * w.y);
    if (dist > kArenaRadius) {
        w.x = w.x / dist * kArenaRadius;
        w.y = w.y / dist * kArenaRadius;
    }
}

bool chooseAutoGoal(const Simulation& sim, uint32_t localAvId, const Walker& me, const Spot* others, int numOthers,
                    float& goalX, float& goalY) {
    const bool localIt = sim.itAvId == localAvId;
    int itIndex = -1;
    float bestDist = 1.0e9f;
    int nearest = -1;
    for (int i = 0; i < numOthers; ++i) {
        const float dx = others[i].x - me.x;
        const float dy = others[i].y - me.y;
        const float d = dx * dx + dy * dy;
        if (d < bestDist) {
            bestDist = d;
            nearest = i;
        }
        if (others[i].avId == sim.itAvId) itIndex = i;
    }

    if (localIt) {
        if (nearest >= 0) {
            goalX = others[nearest].x;
            goalY = others[nearest].y;
            return true;
        }
    } else if (itIndex >= 0) {
        const float dx = me.x - others[itIndex].x;
        const float dy = me.y - others[itIndex].y;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d < kAutoFleeRange) {
            const float len = d > 0.01f ? d : 1.0f;
            const float ux = d > 0.01f ? dx / len : 0.0f;
            const float uy = d > 0.01f ? dy / len : 1.0f;
            goalX = me.x + ux * kAutoFleeStep;
            goalY = me.y + uy * kAutoFleeStep;
            return true;
        }
    }

    float best = 1.0e9f;
    bool found = false;
    for (int i = 0; i < sim.numTreasures; ++i) {
        if (!sim.treasures[i].active) continue;
        const float dx = sim.treasures[i].x - me.x;
        const float dy = sim.treasures[i].y - me.y;
        const float d = dx * dx + dy * dy;
        if (d < best) {
            best = d;
            goalX = sim.treasures[i].x;
            goalY = sim.treasures[i].y;
            found = true;
        }
    }
    return found;
}

} // namespace tt::taggame
