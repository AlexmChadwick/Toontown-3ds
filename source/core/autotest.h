// Scripted input for unattended emulator/hardware runs. Enabled only when
// sdmc:/3ds/toontown3ds/autotest exists; logs per-second stats to log.txt so
// runs can be compared without a person at the controls.
#pragma once

#include <3ds.h>

namespace tt::autotest {

struct Step {
    float seconds;      // duration of this step
    s16 padX, padY;     // circle pad
    u32 held;           // buttons held for the whole step
    u32 downOnce;       // buttons pressed on the first frame of the step
    int touchX, touchY; // touch on the first frame (-1 = none)
    int stressToons;    // -1 = unchanged
    const char* label;
    const char* shot = nullptr; // screenshot name taken on the step's last frame
};

class Runner {
public:
    bool begin(); // returns false (and stays inactive) if the trigger file is absent
    bool active() const { return active_; }
    // Started from autotest_fe: drives the front end instead of the world.
    bool frontEndScript() const { return frontEnd_; }
    // Overrides input for this frame. Returns false when the script is finished.
    bool apply(float dt, circlePosition& pad, u32& held, u32& down, touchPosition& touch, int& stressToons);
    const char* label() const;
    // Non-null for one frame when the script wants a screenshot.
    const char* pendingShot() const { return pendingShot_; }

private:
    bool active_ = false;
    bool frontEnd_ = false;
    const Step* script_ = nullptr;
    int numSteps_ = 0;
    int step_ = 0;
    float t_ = 0.0f;
    bool stepStarted_ = false;
    const char* pendingShot_ = nullptr;
};

} // namespace tt::autotest
