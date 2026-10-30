// Host check for CollisionWorld::cameraSphereCast. The fixture is a tiny TTC
// written by tools/collision_test/run_camera_sphere.sh (no Panda, no baked zone).
//
// What it locks in:
//   * a head-on wall stops the sphere one radius short of the surface
//   * the same wall from the back is two-sided, same clearance
//   * a centre ray that misses a prop still hits when the sphere reaches it
//   * a triangle that lives in the next grid cell (not the ray's cell) still hits
//   * a WALL-only triangle is not a camera solid
//   * an infinite camera plane uses the same clearance
//   * already overlapping returns t = 0
#include "world/collision.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int gFailures = 0;

void expectHit(const char* name, bool hit, float t, float expect, float len) {
    const float err = hit ? fabsf(t - expect) * len : 1.0e9f;
    if (!hit || err > 0.03f) {
        std::printf("FAIL %s: hit=%d t=%.5f expect=%.5f world-err=%.4f\n", name, hit ? 1 : 0, t, expect, err);
        ++gFailures;
    } else {
        std::printf("ok   %s t=%.5f\n", name, t);
    }
}

void expectMiss(const char* name, bool hit, float t) {
    if (hit) {
        std::printf("FAIL %s: expected a miss, t=%.5f\n", name, t);
        ++gFailures;
    } else {
        std::printf("ok   %s miss\n", name);
    }
}

bool castSphere(const tt::world::CollisionWorld& w, float ax, float ay, float az, float bx, float by, float bz,
                float radius, float* t) {
    return w.cameraSphereCast({ax, ay, az}, {bx, by, bz}, radius, t);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: camera_sphere_test solids.ttc plane.ttc\n");
        return 2;
    }
    tt::world::CollisionWorld solids;
    tt::world::CollisionWorld plane;
    if (!solids.load(argv[1]) || !plane.load(argv[2])) {
        std::fprintf(stderr, "load failed\n");
        return 1;
    }

    constexpr float kR = 1.75f;
    float t = -1.0f;

    // Wall quad at x = 5, y in [-2, 2]. Segment length 10. Centre ray hits at 0.5;
    // the sphere must stop at x = 5 - 1.75.
    {
        constexpr float len = 10.0f;
        const float expect = (5.0f - kR) / len;
        const bool hit = castSphere(solids, 0, 0, 1, 10, 0, 1, kR, &t);
        expectHit("head-on wall", hit, t, expect, len);
        float rt = -1.0f;
        const bool ray = solids.cameraRaycast({0, 0, 1}, {10, 0, 1}, &rt);
        expectHit("centre ray still reaches the wall", ray, rt, 0.5f, len);
        // The WALL-only triangle at x = 2 would have reported (2 - 1.75) / 10 if camera
        // solids included it. 0.325 is the x = 5 answer; that check is the head-on test.
    }

    // Same wall, travelling the other way. Two-sided, so clearance is on the far side.
    {
        constexpr float len = 10.0f;
        const float expect = (10.0f - (5.0f + kR)) / len;
        const bool hit = castSphere(solids, 10, 0, 1, 0, 0, 1, kR, &t);
        expectHit("wall from the back", hit, t, expect, len);
    }

    // Centre already within the radius of the wall.
    {
        const bool hit = castSphere(solids, 4, 0, 1, 12, 0, 1, kR, &t);
        expectHit("already overlapping the wall", hit, t, 0.0f, 8.0f);
    }

    // Prop sphere at (8, 3, 1) r = 0.4. Ray at y = 4.5 misses the prop (gap 1.5 > 0.4)
    // and misses the wall (gap 2.5 > 1.75). The camera sphere (1.75 + 0.4) reaches it.
    {
        constexpr float len = 16.0f;
        constexpr float propR = 0.4f;
        constexpr float lateral = 1.5f;
        const float reach = kR + propR;
        const float dx = sqrtf(reach * reach - lateral * lateral);
        const float expect = (8.0f - dx) / len;
        const bool hit = castSphere(solids, 0, 4.5f, 1, 16, 4.5f, 1, kR, &t);
        expectHit("prop beside the ray", hit, t, expect, len);
        float rt = -1.0f;
        const bool ray = solids.cameraRaycast({0, 4.5f, 1}, {16, 4.5f, 1}, &rt);
        expectMiss("centre ray misses that prop", ray, rt);
    }

    // Triangle only in the next Y cell (y >= 8.1). Ray at y = 7.2 stays in cell 0.
    {
        constexpr float len = 16.0f;
        constexpr float edgeY = 8.1f;
        constexpr float rayY = 7.2f;
        const float lateral = edgeY - rayY;
        const float dx = sqrtf(kR * kR - lateral * lateral);
        const float expect = (5.0f - dx) / len;
        const bool hit = castSphere(solids, 0, rayY, 1, 16, rayY, 1, kR, &t);
        expectHit("wall in the neighbouring cell", hit, t, expect, len);
        float rt = -1.0f;
        const bool ray = solids.cameraRaycast({0, rayY, 1}, {16, rayY, 1}, &rt);
        expectMiss("centre ray misses the neighbouring wall", ray, rt);
    }

    // Clear of every solid.
    {
        const bool hit = castSphere(solids, 0, 6, 1, 10, 6, 1, kR, &t);
        expectMiss("clear air", hit, t);
    }

    // Starts outside the grid and still stops short of the wall.
    {
        constexpr float len = 12.0f;
        const float expect = (3.25f - (-2.0f)) / len;
        const bool hit = castSphere(solids, -2, 0, 1, 10, 0, 1, kR, &t);
        expectHit("approach from outside the grid", hit, t, expect, len);
    }

    // Infinite camera plane x = 5, free side x <= 5. Same clearance at any Y.
    {
        constexpr float len = 10.0f;
        const float expect = (5.0f - kR) / len;
        const bool hit = castSphere(plane, 0, 40, 1, 10, 40, 1, kR, &t);
        expectHit("infinite plane", hit, t, expect, len);
        const bool inside = castSphere(plane, 6, 0, 1, 12, 0, 1, kR, &t);
        expectHit("inside the plane's solid half", inside, t, 0.0f, 6.0f);
    }

    std::printf("%s (%d failure%s)\n", gFailures ? "FAILED" : "ALL PASSED", gFailures, gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
