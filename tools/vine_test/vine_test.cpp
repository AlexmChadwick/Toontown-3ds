#include "game/vine_logic.h"
#include <cstdio>
#include <cmath>

// Host check of source/game/vine_logic against tools/vine_test/vine_ref.py (stdout is diffed).
// Stderr: an autoplay run over each course, as the test autoplay plays it (informational).
int main() {
    using namespace tt::game::vine;

    const uint8_t sections[4] = {0, 1, 2, 3};
    VineSpec course[kNumVines];
    if (!buildCourse(sections, 4, course)) {
        std::printf("course build failed\n");
        return 1;
    }
    for (int i = 0; i < 4; ++i)
        std::printf("course %02d: length=%.1f base=%.1f period=%.2f\n", i, course[i].length, course[i].baseAngleDeg,
                    course[i].period);

    const VineSpec& v0 = course[0];
    for (int step = 0; step < 10; ++step) {
        const float t = step * 0.3f;
        const float a = swingAngle(v0, t);
        float x, z;
        cablePoint(0, v0, a, 1.0f, x, z);
        std::printf("swing step %02d: t=%5.2f angle=%7.3f tip=(%7.3f, %7.3f)\n", step, t, a, x, z);
    }

    // Jump from vine 0 at t = 0.5, game time 1.0, forward, 40 frames of 1/30 s (vine_ref.py).
    const float gt = 1.0f;
    const float dt = 1.0f / 30.0f;
    VineToonState s;
    const float a = swingAngle(v0, gt);
    attachToVine(s, 0, v0, a, 0.5f);
    jumpFromVine(s, a, true, gt);
    // Signed zeros from cos(-pi/2) are normalised so the printout matches the Python reference.
    auto clean = [](float v) { return std::fabs(v) < 0.0005f ? 0.0f : v; };
    std::printf("launch: pos=(%.3f, %.3f) vel=(%.3f, %.3f)\n", clean(s.posX), clean(s.posZ), clean(s.velX), clean(s.velZ));
    int caught = -1;
    for (int frame = 0; frame < 40; ++frame) {
        updateJump(s, dt);
        const float now = gt + (frame + 1) * dt;
        int vine = -1;
        float t = 0.0f;
        if (findCatch(s, course, now, &vine, &t)) caught = vine;
        std::printf("jump frame %02d: pos=(%7.3f, %7.3f) vel=(%7.3f, %7.3f) caught=%d\n", frame, s.posX, s.posZ,
                    s.velX, s.velZ, caught);
        if (caught >= 0) break;
    }

    const struct { int treas; float timeLeft; bool reached; } scoreTests[6] = {
        {0, 0.0f, false}, {5, 0.0f, false}, {10, 0.0f, false},
        {5, 10.0f, true}, {10, 25.0f, true}, {15, 45.0f, true}
    };
    for (const auto& st : scoreTests) {
        const int sc = calculateScore(st.treas, st.timeLeft, st.reached);
        std::printf("score: treas=%2d time=%4.1f end=%d -> %2d\n", st.treas, st.timeLeft, int(st.reached), sc);
    }

    // Autoplay as the game runs it: the best climb t whose jump lands on the next vine, else wait.
    const uint8_t courses[3][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {6, 7, 8, 9}};
    for (const auto& sec : courses) {
        VineSpec c[kNumVines];
        buildCourse(sec, 4, c);
        VineToonState p;
        float gameT = 0.0f;
        int reached = 0;
        float reachedAt = -1.0f;
        int falls = 0;
        attachToVine(p, 0, c[0], swingAngle(c[0], 0.0f), kVineStartingT);
        const float maxT = kGameDuration;
        while (gameT < maxT) {
            gameT += dt;
            if (p.state == State::OnVine) {
                if (p.currentVine == kNumVines - 1) break;
                const int cur = p.currentVine;
                const float ang = swingAngle(c[cur], gameT);
                for (float cand = 0.25f; cand <= 0.95f + 1e-4f; cand += 0.05f) {
                    VineToonState probe = p;
                    attachToVine(probe, cur, c[cur], ang, cand);
                    if (jumpReaches(probe, c, gameT, cur + 1, true)) {
                        attachToVine(p, cur, c[cur], ang, cand);
                        jumpFromVine(p, ang, true, gameT);
                        break;
                    }
                }
            } else if (p.state == State::Jumping) {
                updateJump(p, dt);
                if (p.posZ < 0.0f) {
                    ++falls;
                    const int back = p.lastJumpVine;
                    attachToVine(p, back, c[back], swingAngle(c[back], gameT), kVineFellDownT);
                } else {
                    int vine = -1;
                    float t = 0.0f;
                    if (findCatch(p, c, gameT, &vine, &t)) {
                        attachToVine(p, vine, c[vine], swingAngle(c[vine], gameT), t);
                        if (vine == kNumVines - 1) {
                            reached = vine + 1;
                            reachedAt = gameT;
                            break;
                        }
                    }
                }
            }
        }
        std::fprintf(stderr, "autoplay sections %d%d%d%d: vine %d/%d falls=%d reached at %.1f s (limit %.0f s)\n",
                     sec[0], sec[1], sec[2], sec[3], reached, kNumVines, falls, reachedAt, maxT);
    }
    return 0;
}
