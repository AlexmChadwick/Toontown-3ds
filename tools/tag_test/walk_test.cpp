// Tag walking (upstream IT speed and rotate increases), the 90 degree move gate, and the
// autoplay chase/flee goals. Each check prints its outcome; the exit code gates run_tests.sh.
#include <cmath>
#include <cstdio>
#include "game/tag_logic.h"

using namespace tt::taggame;

int main() {
    int failures = 0;
    auto check = [&failures](bool ok, const char* what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
        if (!ok) failures++;
    };

    // IT_SPEED_INCREASE: forward speed 16 * 1.3 while IT, 16 otherwise.
    Walker a;
    stepWalker(a, 1.0f, 0.0f, true, 0.1f);
    check(std::fabs(a.y - kToonForwardSpeed * kItSpeedIncrease * 0.1f) < 1e-3f, "IT walks 1.3x forward");
    Walker b;
    stepWalker(b, 1.0f, 0.0f, false, 0.1f);
    check(std::fabs(b.y - kToonForwardSpeed * 0.1f) < 1e-3f, "non-IT walks at the base forward speed");

    // IT_ROT_INCREASE: rotate 80 * 1.3 degrees per second while IT.
    Walker c;
    stepWalker(c, 0.0f, 180.0f, true, 0.1f);
    check(std::fabs(c.h - kToonRotateSpeed * kItRotIncrease * 0.1f) < 1e-3f, "IT turns 1.3x faster");
    Walker d;
    stepWalker(d, 0.0f, 180.0f, false, 0.1f);
    check(std::fabs(d.h - kToonRotateSpeed * 0.1f) < 1e-3f, "non-IT turns at the base rotate speed");

    // Facing more than 90 degrees away turns in place.
    Walker e;
    e.h = 0.0f;
    stepWalker(e, 1.0f, 150.0f, false, 0.1f);
    check(e.x == 0.0f && e.y == 0.0f, "no forward motion until within 90 degrees of the goal heading");

    // Heading convention: heading toward +x is -90 (forward is (-sin h, cos h)).
    const float hx = headingTo(0.0f, 0.0f, 1.0f, 0.0f);
    check(std::fabs(hx + 90.0f) < 1e-3f, "heading toward +x is -90");

    // Chase and flee goals.
    Simulation sim;
    sim.init(7);
    Walker me;
    me.x = 0.0f;
    me.y = 0.0f;
    const Spot others[2] = {{8, 20.0f, 0.0f}, {9, -3.0f, 4.0f}};
    float gx = 0.0f, gy = 0.0f;
    // Local IT chases the nearest other toon (9, distance 5).
    sim.setIt(1);
    check(chooseAutoGoal(sim, 1, me, others, 2, gx, gy) && gx == -3.0f && gy == 4.0f,
          "local IT chases the nearest other toon");
    // Non-IT: IT (8) is 20 away, outside the flee range, so collect the nearest treasure (10, 10).
    sim.setIt(8);
    check(chooseAutoGoal(sim, 1, me, others, 2, gx, gy) && gx == 10.0f && gy == 10.0f,
          "non-IT with IT out of range collects the nearest treasure");
    // Non-IT with IT close: run directly away from it.
    const Spot close[1] = {{8, 0.0f, -6.0f}};
    check(chooseAutoGoal(sim, 1, me, close, 1, gx, gy) && gx == 0.0f && gy > 0.0f,
          "non-IT runs away from IT inside the flee range");

    // Autoplay run: from the start the toon must reach treasures, not stand still.
    Simulation run;
    run.init(8); // a remote toon is IT, and the local toon is not
    Walker w;
    w.y = -15.0f;
    const Spot far[1] = {{8, 0.0f, 30.0f}};
    const float dt = 1.0f / 30.0f;
    float moved = 0.0f;
    for (float t = 0.0f; t < 60.0f && run.localScore < 4 * kTreasureScore; t += dt) {
        run.step(dt);
        float gxr = 0.0f, gyr = 0.0f;
        const float x0 = w.x, y0 = w.y;
        if (chooseAutoGoal(run, 1, w, far, 1, gxr, gyr)) {
            const float ddx = gxr - w.x, ddy = gyr - w.y;
            const float want = headingTo(w.x, w.y, gxr, gyr);
            stepWalker(w, autoThrottle(std::sqrt(ddx * ddx + ddy * ddy), wrapHeading(want - w.h)), want, false, dt);
        }
        moved += std::sqrt((w.x - x0) * (w.x - x0) + (w.y - y0) * (w.y - y0));
        for (int i = 0; i < run.numTreasures; ++i) {
            if (!run.treasures[i].active) continue;
            const float tx = run.treasures[i].x - w.x, ty = run.treasures[i].y - w.y;
            if (tx * tx + ty * ty < 9.0f) run.grabTreasure(i);
        }
    }
    std::printf("autoplay: score=%d moved=%.1f\n", run.localScore, moved);
    check(run.localScore == 4 * kTreasureScore, "autoplay collects all four treasures");
    check(moved > 50.0f, "autoplay walks the arena instead of standing still");

    // Local IT chases a toon that stands still: contact (kTagContactDist) within 10 seconds.
    Simulation chase;
    chase.init(1);
    Walker hunter;
    hunter.x = -10.0f;
    hunter.y = 0.0f;
    const Spot prey[1] = {{2, 10.0f, 0.0f}};
    float contactT = -1.0f;
    for (float t = 0.0f; t < 10.0f && contactT < 0.0f; t += dt) {
        float gxc = 0.0f, gyc = 0.0f;
        if (chooseAutoGoal(chase, 1, hunter, prey, 1, gxc, gyc)) {
            const float want = headingTo(hunter.x, hunter.y, gxc, gyc);
            const float d = std::sqrt((gxc - hunter.x) * (gxc - hunter.x) + (gyc - hunter.y) * (gyc - hunter.y));
            stepWalker(hunter, autoThrottle(d, wrapHeading(want - hunter.h)), want, true, dt);
        }
        const float cx = prey[0].x - hunter.x, cy = prey[0].y - hunter.y;
        if (cx * cx + cy * cy < kTagContactDist * kTagContactDist) contactT = t;
    }
    std::printf("chase: contact at %.2fs\n", contactT);
    check(contactT >= 0.0f, "local IT reaches a toon it chases");

    return failures == 0 ? 0 : 1;
}
