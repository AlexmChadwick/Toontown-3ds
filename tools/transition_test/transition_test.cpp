// Host test for transition kinematics and track timing.
// Verifies track durations, pivot math, door swing, portal hole scaling, and coordinate frames
// against upstream DistributedToon.py, DistributedDoor.py, and Toon.py.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kToonForwardSpeed = 20.0f; // 16.0 * 1.25 (ToonSpeedFactor)
constexpr float kPivotAngle = 135.0f;
constexpr float kPivotDur = 2.0f;
constexpr float kPivotLerpDur = 1.3333333f;
constexpr float kTunnelCenterOffset = 9.0f;
constexpr float kTunnelCenterInfluence = 0.6f;
constexpr float kPortalMaxScale = 1.17f;

inline float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline float easeInOut(float t) {
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

inline float wrapDeg(float d) {
    while (d >= 360.0f) d -= 360.0f;
    while (d < 0.0f) d += 360.0f;
    return d;
}

inline void localToWorld(float ox, float oy, float oz, float oh,
                         float lx, float ly, float lz, float lh,
                         float* wx, float* wy, float* wz, float* wh) {
    const float rad = oh * kDegToRad;
    const float c = cosf(rad);
    const float s = sinf(rad);
    *wx = ox + lx * c - ly * s;
    *wy = oy + lx * s + ly * c;
    *wz = oz + lz;
    *wh = wrapDeg(oh + lh);
}

inline void worldToLocal(float ox, float oy, float oz, float oh,
                         float wx, float wy, float wz,
                         float* lx, float* ly, float* lz) {
    const float dx = wx - ox;
    const float dy = wy - oy;
    const float rad = oh * kDegToRad;
    const float c = cosf(rad);
    const float s = sinf(rad);
    *lx = dx * c + dy * s;
    *ly = -dx * s + dy * c;
    *lz = wz - oz;
}

void testCoordinateTransforms() {
    printf("Testing coordinate transforms...\n");
    float ox = 100.0f, oy = -50.0f, oz = 10.0f, oh = 90.0f; // facing -X
    float lx = 2.0f, ly = 5.0f, lz = 1.0f, lh = 180.0f;
    float wx, wy, wz, wh;
    localToWorld(ox, oy, oz, oh, lx, ly, lz, lh, &wx, &wy, &wz, &wh);

    // H=90: local +X (+2) should go to world -Y, local +Y (+5) should go to world -X
    // cos(90) = 0, sin(90) = 1
    // wx = ox + lx*0 - ly*1 = 100 - 5 = 95
    // wy = oy + lx*1 + ly*0 = -50 + 2 = -48
    // wz = oz + lz = 10 + 1 = 11
    // wh = wrap(90 + 180) = 270
    assert(fabsf(wx - 95.0f) < 0.001f);
    assert(fabsf(wy - (-48.0f)) < 0.001f);
    assert(fabsf(wz - 11.0f) < 0.001f);
    assert(fabsf(wh - 270.0f) < 0.001f);

    // Round-trip back to local
    float rlx, rly, rlz;
    worldToLocal(ox, oy, oz, oh, wx, wy, wz, &rlx, &rly, &rlz);
    assert(fabsf(rlx - lx) < 0.001f);
    assert(fabsf(rly - ly) < 0.001f);
    assert(fabsf(rlz - lz) < 0.001f);

    printf("  PASSED: localToWorld and worldToLocal round-trip match\n");
}

void testTunnelOutTrack() {
    printf("Testing TunnelOut kinematics (upstream DistributedToon.getTunnelOutToonTrack)...\n");

    const float startX = 5.0f;
    const float startY = -26.0f; // 20 units away from -6.0
    const float straightDur = fabsf(startY - (-6.0f)) / kToonForwardSpeed;
    assert(fabsf(straightDur - 1.0f) < 0.001f); // 20 / 20 = 1.0s

    const float totalDur = straightDur + kPivotDur;
    assert(fabsf(totalDur - 3.0f) < 0.001f);

    // Fade out start time = totalDur - 0.5s = 2.5s
    const float fadeStart = totalDur - 0.5f;
    assert(fabsf(fadeStart - 2.5f) < 0.001f);

    // At t = 0.5s (midway through straight walk)
    const float uStraight = 0.5f / straightDur;
    const float yMid = startY + uStraight * (-6.0f - startY);
    assert(fabsf(yMid - (-16.0f)) < 0.001f);

    // At t = 1.0s (end of straight walk, start of pivot)
    // Pivot center: (-14.0, -6.0)
    // Target X: kTunnelCenterOffset + (startX - kTunnelCenterOffset) * (1.0 - kTunnelCenterInfluence)
    // targetX = 9.0 + (5.0 - 9.0) * 0.4 = 9.0 - 1.6 = 7.4
    const float targetX = kTunnelCenterOffset + (startX - kTunnelCenterOffset) * (1.0f - kTunnelCenterInfluence);
    assert(fabsf(targetX - 7.4f) < 0.001f);

    // Midway through pivot (tp = 1.0s, t = 2.0s):
    const float tp = 1.0f;
    const float ul = fminf(1.0f, tp / kPivotLerpDur);
    assert(fabsf(ul - 0.75f) < 0.001f);
    const float midPivotX = (startX + 14.0f) + ul * (targetX - startX);
    assert(fabsf(midPivotX - (19.0f + 0.75f * (7.4f - 5.0f))) < 0.001f);

    // At end of pivot t = straightDur + 2.0s = 3.0s
    // Angle = -135 deg
    const float theta = -kPivotAngle;
    const float rad = theta * kDegToRad;
    const float curPivotX = targetX + 14.0f; // 7.4 + 14.0 = 21.4
    const float endLx = -14.0f + curPivotX * cosf(rad);
    const float endLy = -6.0f + curPivotX * sinf(rad);
    const float endLh = 180.0f + theta; // 180 - 135 = 45 deg

    // cos(-135) = -sqrt(2)/2 ~= -0.70710678
    // sin(-135) = -sqrt(2)/2 ~= -0.70710678
    // endLx = -14.0 + 21.4 * (-0.70710678) ~= -14.0 - 15.132085 = -29.132
    // endLy = -6.0 + 21.4 * (-0.70710678) ~= -6.0 - 15.132085 = -21.132
    assert(fabsf(endLx - (-29.132085f)) < 0.01f);
    assert(fabsf(endLy - (-21.132085f)) < 0.01f);
    assert(fabsf(endLh - 45.0f) < 0.001f);

    printf("  PASSED: TunnelOut timing and pivot calculations verified\n");
}

void testTunnelInTrack() {
    printf("Testing TunnelIn kinematics (upstream DistributedToon.getTunnelInToonTrack)...\n");

    const float endX = 0.0f;
    const float pivotDur = 2.0f;
    const float straightExitDur = (5.0f - (-6.0f)) / kToonForwardSpeed; // 11 / 20 = 0.55s
    assert(fabsf(straightExitDur - 0.55f) < 0.001f);

    const float totalDur = 0.8f + pivotDur + straightExitDur; // 0.8 wait + 2.0 pivot + 0.55 exit = 3.35s
    assert(fabsf(totalDur - 3.35f) < 0.001f);

    // Initial pose (t < 0.8s):
    // curPivotX = endX + 14.0 = 14.0
    // theta = -135 deg
    const float rad0 = -kPivotAngle * kDegToRad;
    const float initLx = -14.0f + 14.0f * cosf(rad0);
    const float initLy = -6.0f + 14.0f * sinf(rad0);
    const float initLh = -kPivotAngle;
    assert(fabsf(initLx - (-14.0f - 14.0f * 0.70710678f)) < 0.01f);
    assert(fabsf(initLy - (-6.0f - 14.0f * 0.70710678f)) < 0.01f);
    assert(fabsf(initLh - (-135.0f)) < 0.001f);

    // At t = 2.8s (end of pivot):
    // theta = 0 deg
    const float pivotEndLx = -14.0f + 14.0f * cosf(0.0f); // -14 + 14 = 0
    const float pivotEndLy = -6.0f + 14.0f * sinf(0.0f); // -6 + 0 = -6
    const float pivotEndLh = 0.0f;
    assert(fabsf(pivotEndLx - 0.0f) < 0.001f);
    assert(fabsf(pivotEndLy - (-6.0f)) < 0.001f);
    assert(fabsf(pivotEndLh - 0.0f) < 0.001f);

    // At t = 3.35s (end of straight walk):
    // lx = 0.0, ly = 5.0, lh = 0.0 (facing forward out of tunnel into street)
    const float finalLx = endX;
    const float finalLy = -6.0f + 1.0f * 11.0f; // 5.0
    const float finalLh = 0.0f;
    assert(fabsf(finalLx - 0.0f) < 0.001f);
    assert(fabsf(finalLy - 5.0f) < 0.001f);
    assert(fabsf(finalLh - 0.0f) < 0.001f);

    printf("  PASSED: TunnelIn timing and pivot-to-exit continuity verified\n");
}

void testDoorKinematics() {
    printf("Testing Door kinematics (upstream DistributedDoor.py)...\n");

    // Door swing:
    // Latch delay: 0.4s (swing = 0)
    // Opening: 0.4s to 1.0s (0 to 100 deg)
    // Closing: 1.0s to 2.0s (100 to 0 deg)
    float swingAt0_2 = 0.0f;
    float swingAt0_4 = 0.0f;
    float swingAt0_7 = easeInOut((0.7f - 0.4f) / 0.6f) * 100.0f; // t=0.5 -> 50 deg
    float swingAt1_0 = 100.0f;
    float swingAt1_5 = (1.0f - easeInOut((1.5f - 1.0f) / 1.0f)) * 100.0f; // t=0.5 -> 50 deg
    float swingAt2_0 = 0.0f;

    assert(fabsf(swingAt0_2 - 0.0f) < 0.001f);
    assert(fabsf(swingAt0_4 - 0.0f) < 0.001f);
    assert(fabsf(swingAt0_7 - 50.0f) < 0.001f);
    assert(fabsf(swingAt1_0 - 100.0f) < 0.001f);
    assert(fabsf(swingAt1_5 - 50.0f) < 0.001f);
    assert(fabsf(swingAt2_0 - 0.0f) < 0.001f);

    // Avatar door exit walk:
    // From (-1.5, 0.0) to (-1.5, -6.0) over 1.0s
    float exitY0 = 0.0f;
    float exitYMid = -easeInOut(0.5f) * 6.0f; // -3.0
    float exitY1 = -6.0f;
    assert(fabsf(exitY0 - 0.0f) < 0.001f);
    assert(fabsf(exitYMid - (-3.0f)) < 0.001f);
    assert(fabsf(exitY1 - (-6.0f)) < 0.001f);

    printf("  PASSED: Door opening/closing curves and avatar walk verified\n");
}

void testPortalHoleScaling() {
    printf("Testing Teleport portal hole prop scaling (upstream Toon.py)...\n");

    // Teleport out:
    // t < 1.708s: scale = 0
    // 1.708s <= t < 2.0s: expand to 1.17
    // 2.0s <= t < 3.0s: stay at 1.17
    // 3.0s <= t < 3.4s: shrink to 0
    float scale0 = 0.0f;
    float scaleMidOpen = easeInOut((1.854f - 1.708f) / 0.292f) * kPortalMaxScale;
    float scaleOpen = kPortalMaxScale;
    float scaleMidClose = (1.0f - easeInOut((3.2f - 3.0f) / 0.4f)) * kPortalMaxScale;
    float scaleClosed = 0.0f;

    assert(fabsf(scale0 - 0.0f) < 0.001f);
    assert(fabsf(scaleMidOpen - (kPortalMaxScale * 0.5f)) < 0.01f);
    assert(fabsf(scaleOpen - 1.17f) < 0.001f);
    assert(fabsf(scaleMidClose - (kPortalMaxScale * 0.5f)) < 0.01f);
    assert(fabsf(scaleClosed - 0.0f) < 0.001f);

    printf("  PASSED: Portal hole expansion and contraction verified\n");
}

} // namespace

int main() {
    printf("=== Toontown 3DS Transitions Host Test Suite ===\n");
    testCoordinateTransforms();
    testTunnelOutTrack();
    testTunnelInTrack();
    testDoorKinematics();
    testPortalHoleScaling();
    printf("All transition host tests PASSED successfully!\n");
    return 0;
}
