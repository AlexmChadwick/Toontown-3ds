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
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "grab treasure", "grab"},
    {4.0f, 0, 0, 0, 0, -1, -1, 0, "meet mickey", "mickey"},
    {4.0f, 0, 0, 0, 0, -1, -1, 0, "exit: door sz19", "exit_door"},
    {4.0f, 0, 0, 0, 0, -1, -1, 0, "exit: interior door", "exit_interior"},
    {4.0f, 0, 0, 0, 0, -1, -1, 0, "exit: tunnel 2132", "exit_tunnel"},
    {5.0f, 0, 0, 0, 0, -1, -1, 0, "exit: street door", "street_interior"},
    {5.0f, 0, 0, 0, 0, -1, -1, 0, "exit: interior door", "street_back"},
    {1.0f, 0, 0, 0, KEY_TOUCH, tabX(0), kTabY, 0, "tab: map (teleport)"},
    {8.0f, 0, 0, 0, KEY_TOUCH, 60, 191, 0, "teleport DD", "hood_dd"},
    {8.0f, 0, 0, 0, KEY_TOUCH, 100, 191, 0, "teleport DG", "hood_dg"},
    {8.0f, 0, 0, 0, KEY_TOUCH, 180, 191, 0, "teleport BR", "hood_br"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "done"},
};
constexpr int kWorldSteps = sizeof(kScript) / sizeof(kScript[0]);

// Front end (source/game/front_end.cpp layout): title -> Play online -> slot 1 ->
// Make a Toon (cat girl, colour, shirt) -> Create -> playground. Taps fire on release,
// so each tap step holds the touch for its first frame only.
constexpr int kFeNextX = 255, kFeNextY = 217;
const Step kFrontEndScript[] = {
    {2.0f, 0, 0, 0, 0, -1, -1, -1, "fe: title", "fe_title"},
    {6.0f, 0, 0, 0, KEY_TOUCH, 160, 94, -1, "fe: play online", "fe_pick"},
    {1.0f, 0, 0, 0, KEY_TOUCH, 82, 52, -1, "fe: pick slot 1"},
    {2.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: make a toon"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 160, 51, -1, "fe: species cat", "fe_species"},
    {1.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> body"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 255, 171, -1, "fe: girl", "fe_body"},
    {1.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> colour"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 191, 76, -1, "fe: colour 6", "fe_color"},
    {1.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> clothes"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 280, 70, -1, "fe: next shirt", "fe_clothes"},
    {2.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> confirm", "fe_confirm"},
    {10.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: create", "fe_world"},
    {3.0f, 0, 150, 0, 0, -1, -1, -1, "fe: run in world", "fe_run"},
};
constexpr int kFrontEndSteps = sizeof(kFrontEndScript) / sizeof(kFrontEndScript[0]);

} // namespace

bool Runner::begin() {
    FILE* f = fopen("sdmc:/3ds/toontown3ds/autotest_fe", "r");
    frontEnd_ = f != nullptr;
    if (!f) f = fopen("sdmc:/3ds/toontown3ds/autotest", "r");
    if (!f) return false;
    fclose(f);
    active_ = true;
    script_ = frontEnd_ ? kFrontEndScript : kScript;
    numSteps_ = frontEnd_ ? kFrontEndSteps : kWorldSteps;
    TT_LOG("autotest: start %s script (%d steps)", frontEnd_ ? "front-end" : "world", numSteps_);
    return true;
}

const char* Runner::label() const { return active_ && step_ < numSteps_ ? script_[step_].label : ""; }

bool Runner::apply(float dt, circlePosition& pad, u32& held, u32& down, touchPosition& touch, int& stressToons) {
    pendingShot_ = nullptr;
    if (!active_) return true;
    if (step_ >= numSteps_) {
        active_ = false;
        TT_LOG("autotest: finished");
        return false;
    }
    const Step& s = script_[step_];
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
