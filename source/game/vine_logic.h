#pragma once

#include <cstdint>

namespace tt::game::vine {

// DistributedVineGame / VineGameGlobals constants (names follow the upstream globals).
constexpr int kNumVines = 20;          // VineGameGlobals.NumVines
constexpr float kVineSpacing = 30.0f;  // VineXIncrement
constexpr float kVineTopZ = 30.0f;     // VineHeight: the pivot every SwingVine hangs from
constexpr float kGameDuration = 70.0f; // GameDuration
constexpr float kBaseBonus = 5.0f;     // DistributedVineGameAI.checkForEndVine
constexpr float kBonusPerSecond = 0.4f; // BonusPerSecondLeft
constexpr float kGravity = -32.0f;     // DistributedVineGame.Gravity (applied as -Gravity)
constexpr float kJumpSpeed = 64.0f;    // DistributedVineGame.JumpSpeed, scaled by the vine t
constexpr float kToonVerticalRate = 0.25f; // ToonVerticalRate: t per second while climbing
constexpr float kLocalPhysicsRadius = 1.5f; // DistributedVineGame.LocalPhysicsRadius
constexpr float kTubeRadius = 0.5f;    // SwingVine.setupTubes radius
constexpr float kVineStartingT = 0.25f; // VineGameGlobals.VineStartingT
constexpr float kVineFellDownT = 0.1f;  // VineGameGlobals.VineFellDownT
constexpr float kJumpTimeBuffer = 0.5f; // VineGameGlobals.JumpTimeBuffer
constexpr float kPi = 3.14159265f;

enum class State : uint8_t {
    OnVine,
    Jumping,
    Finished,
};

// One vine of the course (a CourseVine from the server's sections).
struct VineSpec {
    float length;
    float baseAngleDeg;
    float period;
};

// Toon physics and attachment, in the game's (x, z) plane.
struct VineToonState {
    State state = State::OnVine;
    int currentVine = 0;   // the vine held, or the vine last jumped to
    int lastJumpVine = 0;  // DistributedVineGame.lastJumpVine
    float vineT = kVineStartingT;   // 0 at the pivot, 1 at the bottom of the cable
    float posX = 0.0f;
    float posZ = 0.0f;
    float velX = 0.0f;
    float velZ = 0.0f;
    float jumpTime = 0.0f; // game clock at the last jump (JumpTimeBuffer)
    bool facingRight = true;
};

// Fills the course for the server's vineSections (VineGameGlobals.CourseSections). Each section
// contributes kVinesPerSection vines in order. Returns false for an out-of-range section.
bool buildCourse(const uint8_t* sections, int numSections, VineSpec* out);

// The swing: DistributedVineGame's SwingVine.startSwing / swingForward / swingBack, which move the
// bottom link from -90-baseAngle to -90+baseAngle over half a period and back. Returns radians.
float swingAngle(const VineSpec& vine, float gameTime);

// The cable's bottom point for a pivot at vine index, angle and t (0 at the pivot, 1 at the end).
void cablePoint(int vineIndex, const VineSpec& vine, float angleRad, float t, float& outX, float& outZ);

// Attaches the toon to a vine at t (DistributedVineGame.attachLocalToonToVine).
void attachToVine(VineToonState& s, int vineIndex, const VineSpec& vine, float angleRad, float t);

// Leaves the vine (DistributedVineGame.makeLocalToonJump). forward picks the normal pointing toward
// the next vine; the velocity is that normal times JumpSpeed * t, so the swing speed does not matter.
void jumpFromVine(VineToonState& s, float angleRad, bool forward, float gameTime);

// Integrates the free fall with gravity (LinearVectorForce world-forces).
void updateJump(VineToonState& s, float dt);

// Whether a jumping toon touches a vine tube within lastJumpVine +- 1 (SwingVine tube collisions with
// LocalPhysicsRadius). On a catch it sets vineOut and tOut, the t projected on the cable.
bool findCatch(const VineToonState& s, const VineSpec* course, float gameTime, int* vineOut, float* tOut);

// Whether a jump from the held vine at t and the current swing lands on targetVine. Used by the
// test autoplay to choose a timing that the real physics will also catch.
bool jumpReaches(const VineToonState& start, const VineSpec* course, float gameTime, int targetVine, bool forward);

// Final score calculation matching DistributedVineGameAI.py:checkForEndVine
int calculateScore(int treasures, float timeLeft, bool reachedEnd);

} // namespace tt::game::vine
