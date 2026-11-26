#include "game/vine_logic.h"

#include "game/vine_tables.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace tt::game::vine {

namespace {

constexpr float kDegToRad = kPi / 180.0f;
constexpr float kVerticalDeg = -90.0f; // SwingVine: the cable hangs straight down at -90 degrees
constexpr float kSimStep = 1.0f / 30.0f;   // reach test step
constexpr float kSimHorizon = 2.0f;        // seconds a jump is checked for a catch

// Squared distance from (px, pz) to the cable segment of a vine.
float cableDistance(float px, float pz, int vineIndex, const VineSpec& vine, float angleRad) {
    float bx, bz;
    cablePoint(vineIndex, vine, angleRad, 1.0f, bx, bz);
    const float ax = vineIndex * kVineSpacing;
    const float az = kVineTopZ;
    const float dx = bx - ax, dz = bz - az;
    const float len2 = dx * dx + dz * dz;
    float u = 0.0f;
    if (len2 > 0.0f) u = std::clamp(((px - ax) * dx + (pz - az) * dz) / len2, 0.0f, 1.0f);
    const float ex = ax + u * dx - px, ez = az + u * dz - pz;
    return std::sqrt(ex * ex + ez * ez);
}

// Launch velocity for a jump from a cable point (DistributedVineGame.makeLocalToonJump): the normal
// of the cable tangent, cross(tangent, +Y), times JumpSpeed * t.
void launchVelocity(float angleRad, float t, bool forward, float& vx, float& vz, bool& right) {
    // The tangent from the pivot to the bottom is (cos, sin); cross((tx,0,tz),(0,1,0)) = (-tz, 0, tx).
    float nx = -std::sin(angleRad), nz = std::cos(angleRad);
    if (!forward) {
        nx = -nx;
        nz = -nz;
    }
    const float speed = kJumpSpeed * t;
    vx = nx * speed;
    vz = nz * speed;
    right = nx >= 0.0f;
}

} // namespace

bool buildCourse(const uint8_t* sections, int numSections, VineSpec* out) {
    int n = 0;
    for (int i = 0; i < numSections && n < kNumVines; ++i) {
        const int sec = sections[i];
        if (sec < 0 || sec >= kNumCourseSections) return false;
        for (int k = 0; k < kVinesPerSection && n < kNumVines; ++k, ++n) {
            const CourseVine& v = kCourseSections[sec][k];
            out[n] = {v.length, v.baseAngle, v.period, v.spiderPeriod};
        }
    }
    return n == kNumVines;
}

float swingAngle(const VineSpec& vine, float gameTime) {
    // startSwing begins at the minimum angle; each half period eases to the other end.
    const float period = vine.period > 0.0f ? vine.period : 1.0f;
    const float deg = kVerticalDeg - vine.baseAngleDeg * std::cos(2.0f * kPi * gameTime / period);
    return deg * kDegToRad;
}

// SwingVine's Rope: an order 4 curve through its four links (a Bezier): the pivot, link0 a
// third of the cable straight down (never moved), link1 two thirds out at two thirds of the
// swing (positionLink1) and the bottom link at the swing angle.
void cablePoint(int vineIndex, const VineSpec& vine, float angleRad, float t, float& outX, float& outZ) {
    const float L = vine.length;
    const float a1 = -0.5f * kPi + (angleRad + 0.5f * kPi) * (2.0f / 3.0f);
    const float px[4] = {0.0f, 0.0f, std::cos(a1) * L * (2.0f / 3.0f), std::cos(angleRad) * L};
    const float pz[4] = {0.0f, -L / 3.0f, std::sin(a1) * L * (2.0f / 3.0f), std::sin(angleRad) * L};
    const float u = 1.0f - t;
    const float b[4] = {u * u * u, 3.0f * u * u * t, 3.0f * u * t * t, t * t * t};
    outX = vineIndex * kVineSpacing + b[0] * px[0] + b[1] * px[1] + b[2] * px[2] + b[3] * px[3];
    outZ = kVineTopZ + b[0] * pz[0] + b[1] * pz[1] + b[2] * pz[2] + b[3] * pz[3];
}

static float easeInOut(float t) { return t * t * (3.0f - 2.0f * t); } // Panda EaseInOutBlendType

bool spiderPose(int vineIndex, const VineSpec& vine, float gameTime, float* x, float* z, float* rollDeg) {
    if (vine.spiderPeriod <= 0.0f) return false;
    const float half = vine.spiderPeriod * 0.5f;
    float ph = std::fmod(std::max(gameTime, 0.0f), vine.spiderPeriod);
    const bool down = ph < half;
    const float e = easeInOut(down ? ph / half : (ph - half) / half);
    const float t = down ? e : 1.0f - e; // moveSpiderDown / moveSpiderUp
    const float angle = swingAngle(vine, gameTime);
    float px, pz, ax, az, bx, bz;
    cablePoint(vineIndex, vine, angle, t, px, pz);
    cablePoint(vineIndex, vine, angle, std::max(t - 0.01f, 0.0f), ax, az);
    cablePoint(vineIndex, vine, angle, std::min(t + 0.01f, 1.0f), bx, bz);
    const float deg = std::atan2(bz - az, bx - ax) * (180.0f / kPi);
    if (x) *x = px;
    if (z) *z = pz;
    if (rollDeg) *rollDeg = down ? -(deg + 90.0f) : -(deg + 90.0f) - 180.0f;
    return true;
}

float swingFrame(const VineSpec& vine, float gameTime, float t, bool facingRight) {
    const float period = vine.period > 0.0f ? vine.period : 1.0f;
    const float ph = std::fmod(std::max(gameTime, 0.0f), period) / period;
    const bool forward = ph < 0.5f;
    const float u = forward ? ph * 2.0f : (ph - 0.5f) * 2.0f; // through this half swing
    const float angleDeg = t * vine.baseAngleDeg;
    const bool full = (forward && facingRight) || (!forward && !facingRight);
    if (angleDeg > 10.0f) {
        // setupSwingAnimFull: 108 -> 143, 0 -> 35 one way; 35 -> 107 the other.
        if (full) return std::fmod(108.0f + u * 72.0f, 144.0f);
        return 35.0f + u * 72.0f;
    }
    if (angleDeg > 2.0f) {
        // setupSwingAnimSmall: 5 -> 0, 1 -> 15 one way; 15 -> 20, 20 -> 5 the other.
        if (full) return u < 0.25f ? 5.0f - u * 20.0f : 1.0f + (u - 0.25f) / 0.75f * 14.0f;
        return u < 0.25f ? 15.0f + u * 20.0f : 20.0f - (u - 0.25f) / 0.75f * 15.0f;
    }
    // setupSwingAnimMinimal: 88 -> 84 -> 88 over the half swing.
    return u < 0.5f ? 88.0f - u * 8.0f : 84.0f + (u - 0.5f) * 8.0f;
}

void attachToVine(VineToonState& s, int vineIndex, const VineSpec& vine, float angleRad, float t) {
    s.state = State::OnVine;
    s.falling = 0;
    s.currentVine = vineIndex;
    s.vineT = std::clamp(t, 0.0f, 1.0f);
    cablePoint(vineIndex, vine, angleRad, s.vineT, s.posX, s.posZ);
    s.velX = 0.0f;
    s.velZ = 0.0f;
}

void jumpFromVine(VineToonState& s, float angleRad, bool forward, float gameTime) {
    launchVelocity(angleRad, s.vineT, forward, s.velX, s.velZ, s.facingRight);
    s.state = State::Jumping;
    s.lastJumpVine = s.currentVine;
    s.jumpTime = gameTime;
}

void updateJump(VineToonState& s, float dt) {
    if (s.state != State::Jumping) return;
    s.velZ += kGravity * dt;
    s.posX += s.velX * dt;
    s.posZ += s.velZ * dt;
}

bool findCatch(const VineToonState& s, const VineSpec* course, float gameTime, int* vineOut, float* tOut) {
    // handleCatchCollision: a Toon knocked off by a spider or bat catches nothing until it lands.
    if (s.state != State::Jumping || s.falling) return false;
    const float reach = kLocalPhysicsRadius + kTubeRadius;
    for (int j = std::max(0, s.lastJumpVine - 1); j <= std::min(kNumVines - 1, s.lastJumpVine + 1); ++j) {
        if (j == s.lastJumpVine && gameTime - s.jumpTime < kJumpTimeBuffer) continue;
        const float angle = swingAngle(course[j], gameTime);
        if (cableDistance(s.posX, s.posZ, j, course[j], angle) >= reach) continue;
        // calcTFromTubeHit: the position along the cable, clamped to the cable.
        const float ax = j * kVineSpacing, az = kVineTopZ;
        float bx, bz;
        cablePoint(j, course[j], angle, 1.0f, bx, bz);
        const float dx = bx - ax, dz = bz - az;
        const float len2 = dx * dx + dz * dz;
        const float u = len2 > 0.0f ? ((s.posX - ax) * dx + (s.posZ - az) * dz) / len2 : 0.0f;
        if (vineOut) *vineOut = j;
        if (tOut) *tOut = std::clamp(u, 0.0f, 1.0f);
        return true;
    }
    return false;
}

bool jumpReaches(const VineToonState& start, const VineSpec* course, float gameTime, int targetVine, bool forward) {
    if (targetVine < 0 || targetVine >= kNumVines) return false;
    const int held = start.currentVine;
    if (std::abs(targetVine - held) != 1) return false;
    const float angle = swingAngle(course[held], gameTime);
    float px, pz;
    cablePoint(held, course[held], angle, start.vineT, px, pz);
    VineToonState sim = start;
    sim.posX = px;
    sim.posZ = pz;
    sim.state = State::Jumping;
    sim.lastJumpVine = held;
    sim.jumpTime = gameTime;
    launchVelocity(angle, start.vineT, forward, sim.velX, sim.velZ, sim.facingRight);
    const float reach = kLocalPhysicsRadius + kTubeRadius;
    for (float tau = kSimStep; tau <= kSimHorizon; tau += kSimStep) {
        updateJump(sim, kSimStep);
        if (sim.posZ < 0.0f) return false;
        const float targetAngle = swingAngle(course[targetVine], gameTime + tau);
        if (cableDistance(sim.posX, sim.posZ, targetVine, course[targetVine], targetAngle) < reach) return true;
    }
    return false;
}

bool reportedPosition(const ReportedToon& r, const VineSpec* course, float gameTime, float elapsed, float& outX,
                      float& outZ) {
    if (elapsed < 0.0f) elapsed = 0.0f;
    if (r.attached) {
        if (r.vineIndex < 0 || r.vineIndex >= kNumVines) return false;
        const float t = std::clamp(r.vineT + float(r.climbDir) * kToonVerticalRate * elapsed, 0.0f, 1.0f);
        cablePoint(r.vineIndex, course[r.vineIndex], swingAngle(course[r.vineIndex], gameTime), t, outX, outZ);
        return true;
    }
    // The reports are a few seconds apart at most; a longer silence freezes the toon in the air.
    constexpr float kMaxExtrapolation = 4.0f;
    const float e = std::min(elapsed, kMaxExtrapolation);
    outX = r.posX + r.velX * e;
    outZ = r.posZ + r.velZ * e + 0.5f * kGravity * e * e;
    if (outZ < 0.0f) outZ = 0.0f;
    return true;
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
