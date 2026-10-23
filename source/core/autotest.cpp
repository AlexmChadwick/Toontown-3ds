#include "core/autotest.h"

#include <cstdio>

#include "core/log.h"

namespace tt::autotest {

namespace {

// Bottom-screen tab centres (render/ui.cpp: 5 tabs across 320 px, bar at y >= 208).
constexpr int kTabY = 224;
constexpr int tabX(int i) { return 32 + i * 64; }

const Step kScript[] = {
    {2.0f, 0, 0, 0, 0, -1, -1, -1, "idle"},
    {3.0f, 0, 150, 0, 0, -1, -1, -1, "run forward", "run"},
    {2.0f, 120, 150, 0, 0, -1, -1, -1, "run + turn right"},
    {1.0f, 0, 150, 0, KEY_A, -1, -1, -1, "running jump"},
    {2.0f, 0, 150, KEY_B, 0, -1, -1, -1, "walk (B)"},
    {2.0f, 0, 0, KEY_L, 0, -1, -1, -1, "camera orbit (L)", "orbit"},
    {2.0f, 0, -150, 0, 0, -1, -1, -1, "reverse"},
    {1.5f, 0, 0, 0, KEY_TOUCH, tabX(0), kTabY, -1, "tab: map", "map"},
    {1.5f, 0, 0, 0, KEY_TOUCH, tabX(2), kTabY, -1, "tab: toon"},
    {1.0f, 0, 0, 0, KEY_TOUCH, 8 + 4 * 34 + 15, 44 + 15, -1, "toon: pick color 4", "toon"},
    {1.0f, 0, 0, 0, KEY_TOUCH, tabX(4), kTabY, -1, "tab: debug"},
    {3.0f, 0, 0, 0, 0, -1, -1, 8, "stress 8 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 16, "stress 16 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 32, "stress 32 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 48, "stress 48 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 64, "stress 64 toons", "stress64"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "done"},
};
constexpr int kNumSteps = sizeof(kScript) / sizeof(kScript[0]);

} // namespace

bool Runner::begin() {
    FILE* f = fopen("sdmc:/3ds/toontown3ds/autotest", "r");
    if (!f) return false;
    fclose(f);
    active_ = true;
    TT_LOG("autotest: start (%d steps)", kNumSteps);
    return true;
}

const char* Runner::label() const { return active_ && step_ < kNumSteps ? kScript[step_].label : ""; }

bool Runner::apply(float dt, circlePosition& pad, u32& held, u32& down, touchPosition& touch, int& stressToons) {
    pendingShot_ = nullptr;
    if (!active_) return true;
    if (step_ >= kNumSteps) {
        active_ = false;
        TT_LOG("autotest: finished");
        return false;
    }
    const Step& s = kScript[step_];
    if (!stepStarted_) {
        TT_LOG("autotest: step %d '%s'", step_, s.label);
        down |= s.downOnce;
        if (s.touchX >= 0) {
            touch.px = static_cast<u16>(s.touchX);
            touch.py = static_cast<u16>(s.touchY);
            held |= KEY_TOUCH;
        }
        if (s.stressToons >= 0) stressToons = s.stressToons;
        stepStarted_ = true;
    }
    pad.dx = s.padX;
    pad.dy = s.padY;
    held |= s.held;
    t_ += dt;
    if (t_ >= s.seconds) {
        pendingShot_ = s.shot;
        t_ = 0.0f;
        stepStarted_ = false;
        step_++;
    }
    return true;
}

} // namespace tt::autotest
