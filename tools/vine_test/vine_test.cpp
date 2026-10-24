#include "game/vine_logic.h"
#include <cstdio>
#include <cmath>

int main() {
    using namespace tt::game::vine;

    // 1. Swing test
    for (int step = 0; step < 20; ++step) {
        float t = step * 0.15f;
        float angle = getVineAngle(t);
        float x, z;
        getVineToonPos(0, 0.5f, angle, x, z);
        std::printf("swing step %02d: t=%5.2f angle=%6.3f pos=(%6.2f, %6.2f)\n",
                    step, t, angle, x, z);
    }

    // 2. Jump test
    VineToonState s;
    s.state = State::OnVine;
    s.currentVine = 0;
    s.vineT = 0.5f;
    float t = 0.75f;
    float omega = (2.0f * 3.14159265f) / 3.0f;
    float angle = getVineAngle(t);
    float angVel = 0.785398f * omega * std::cos(omega * t);
    getVineToonPos(0, s.vineT, angle, s.posX, s.posZ);
    jumpFromVine(s, angle, angVel);

    const float dt = 0.033f;
    bool caught = false;
    for (int frame = 0; frame < 40; ++frame) {
        updateJump(s, dt);
        if (checkVineCatch(s) && !caught) {
            caught = true;
        }
        std::printf("jump frame %02d: pos=(%6.2f, %6.2f) vel=(%6.2f, %6.2f) caught=%d\n",
                    frame, s.posX, s.posZ, s.velX, s.velZ, int(caught));
    }

    // 3. Score calculation tests
    const struct { int treas; float timeLeft; bool reached; } scoreTests[6] = {
        {0, 0.0f, false}, {5, 0.0f, false}, {10, 0.0f, false},
        {5, 10.0f, true}, {10, 25.0f, true}, {15, 45.0f, true}
    };
    for (int i = 0; i < 6; ++i) {
        int sc = calculateScore(scoreTests[i].treas, scoreTests[i].timeLeft, scoreTests[i].reached);
        std::printf("score: treas=%2d time=%4.1f end=%d -> %2d\n",
                    scoreTests[i].treas, scoreTests[i].timeLeft, int(scoreTests[i].reached), sc);
    }

    return 0;
}
