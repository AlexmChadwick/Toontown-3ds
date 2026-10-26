// Tag minigame pure logic (no 3DS headers). Tag mechanics, tagback cooldown, treasure scoring,
// upstream IT speeds, tank-style walking and the test autoplay's chase/flee goals.
#pragma once

#include <cstdint>

namespace tt::taggame {

constexpr float kDuration = 60.0f;
constexpr float kTagBackCooldown = 2.0f;
constexpr float kArenaRadius = 35.0f;
constexpr int kTreasureScore = 2;
constexpr int kMaxTreasures = 16;

// OTPGlobals.ToonForwardSpeed (16) and ToonRotateSpeed (80 degrees per second), ToonSpeedFactor 1.
constexpr float kToonForwardSpeed = 16.0f;
constexpr float kToonRotateSpeed = 80.0f;
// DistributedTagGame.IT_SPEED_INCREASE and IT_ROT_INCREASE, applied while the local toon is IT.
constexpr float kItSpeedIncrease = 1.3f;
constexpr float kItRotIncrease = 1.3f;
// Contact between toons: LocalAvatar.ccSphere has radius 1 on each toon. The remote
// distAvatarCollNode lives in the toon model, so this is the value the port assumes.
constexpr float kTagContactDist = 2.0f;
// Local tag sends are rate limited; the AI owns the real tagback cooldown.
constexpr float kTagSendIntervalSec = 0.25f;
// Test autoplay (not upstream): a non-IT toon runs when IT is this close, then collects treasure.
constexpr float kAutoFleeRange = 12.0f;
constexpr float kAutoFleeStep = 10.0f;
// Distance at which an autoplay goal counts as reached.
constexpr float kAutoGoalReach = 0.5f;
// Autoplay eases off inside this range, as an analog stick does: a full-speed toon turning at the
// rotate speed orbits in an 11.5 unit circle, so it cannot reach a treasure 3 units away.
constexpr float kAutoSlowRange = 4.0f;
constexpr float kAutoMinThrottle = 0.15f;

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
    // DistributedTagGame.setIt: the AI decides IT and broadcasts it, so the client adopts it.
    void setIt(uint32_t avId) { itAvId = avId; }
    bool grabTreasure(int index);
};

// A walking toon in arena coordinates. Heading uses the port's convention: forward is (-sin h, cos h).
struct Walker {
    float x = 0.0f;
    float y = 0.0f;
    float h = 0.0f;
};

float wrapHeading(float deg);
float headingTo(float fromX, float fromY, float toX, float toY);

// One frame of upstream-style walking: turn toward wantH at the rotate speed, then walk forward at
// the forward speed times throttle (0 to 1, the stick magnitude) while facing within 90 degrees of
// wantH. IT scales both speeds. Stays in the arena.
void stepWalker(Walker& w, float throttle, float wantH, bool isIt, float dt);

// Autoplay throttle: eases off near the goal and while the toon still faces away from it
// (cos of the heading error), so it turns onto the goal instead of orbiting past it.
float autoThrottle(float distance, float headingErrDeg);

struct Spot {
    uint32_t avId = 0;
    float x = 0.0f;
    float y = 0.0f;
};

// Test autoplay goal. As IT, chase the nearest other toon; otherwise run from IT when it is in range,
// and collect the nearest active treasure. Returns false when there is nothing to do.
bool chooseAutoGoal(const Simulation& sim, uint32_t localAvId, const Walker& me, const Spot* others, int numOthers,
                    float& goalX, float& goalY);

} // namespace tt::taggame
