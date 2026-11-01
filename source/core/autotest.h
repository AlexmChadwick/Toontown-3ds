#include "core/trolley_progress.h" // qafix
// Scripted input for unattended emulator/hardware runs. Enabled only when
// sdmc:/3ds/toontown3ds/autotest exists; logs per-second stats to log.txt so
// runs can be compared without a person at the controls.
#pragma once

#include <3ds.h>
#include <cstring>

namespace tt::autotest {

struct Step {
    float seconds;      // duration of this step
    s16 padX, padY;     // circle pad
    u32 held;           // buttons held for the whole step
    u32 downOnce;       // buttons pressed on the first frame of the step
    int touchX, touchY; // touch on the first frame (-1 = none)
    int stressToons;    // -1 = unchanged
    const char* label;
    const char* shot = nullptr; // screenshot name taken on the step's last frame;
                                // "name*" also shoots once a second as name_001, name_002, ...
                                // and "name**" every 0.25 s.
};

// "pose <zone> <eye x y z> <target x y z> <toon x y z h>" in the autotest file: the N-th
// such line is held by the step "pose: N" (main.cpp), for close-ups of any spot.
struct Pose {
    unsigned zone;
    float eye[3], target[3], toon[4];
};

class Runner {
public:
    static constexpr int kMaxPoses = 8;
    void setTrolleyProgress(const TrolleyProgress& progress) { trolleyProgress_ = progress; }
    bool begin(); // returns false (and stays inactive) if the trigger file is absent
    bool active() const { return active_; }
    // Started from autotest_fe: drives the front end instead of the world.
    bool frontEndScript() const { return frontEnd_; }
    // "tutorial" in autotest_fe continues with the Toontorial driver after Create!; "skip" skips it.
    bool tutorialPath() const { return tutorial_; }
    // "battle hold <s>": the scripted gag pick waits this long into each input round, so shots
    // can show the open gag menu (0: the usual 0.7 s).
    u32 battleHoldMs() const { return battleHoldMs_; }
    // Overrides input for this frame. Returns false when the script is finished.
    bool apply(float dt, circlePosition& pad, u32& held, u32& down, touchPosition& touch, int& stressToons);
    const char* label() const;
    // A script started part-way ("from <label>") holds still until the game is in a zone.
    void setReady() { ready_ = true; }
    bool waiting() const { return active_ && !ready_; }
    float stepTime() const { return t_; } // seconds into the current step
    void hold(float dt) { t_ = (t_ > dt) ? (t_ - dt) : 0.0f; }
    // Ends the current step on the next frame (its goal is already reached).
    void finishStep() { t_ = 1e9f; }
    // True on the first frame the main loop sees each step (a long frame cannot skip it).
    bool stepBegan() const { return active_ && ready_ && stepFrames_ == 1; }
    // Non-null for one frame when the script wants a screenshot.
    const char* pendingShot() const { return shotsEnabled_ ? pendingShot_ : nullptr; }
    // "drop at <label>": true on the first frame of that step, which then drops the connection
    // (the reconnect path, like a dead socket after sleep).
    bool dropNow() const { return dropAt_[0] && stepBegan() && !strcmp(label(), dropAt_); }
    // The current "pose: N" step's pose, or null.
    const Pose* pose() const;

private:
    TrolleyProgress trolleyProgress_;
    bool trolleyWaiting_ = false;
    bool shotsEnabled_ = true;
    bool active_ = false;
    bool frontEnd_ = false;
    bool tutorial_ = false;
    u32 battleHoldMs_ = 0;
    const Step* script_ = nullptr;
    int numSteps_ = 0;
    int step_ = 0;
    bool ready_ = true; // false: started part-way, waiting for setReady()
    float t_ = 0.0f;
    bool stepStarted_ = false;
    int stepFrames_ = 0; // frames of the current step so far
    const char* pendingShot_ = nullptr;
    float nextSeriesShot_ = 0.0f;
    int seriesIndex_ = 0;
    char seriesName_[40] = {};
    char dropAt_[64] = {};
    char only_[192] = {}; // "only" line: comma-separated label prefixes (empty = every step)
    Pose poses_[kMaxPoses] = {};
    int numPoses_ = 0;
    bool selected(const char* label) const;
    bool skipped(const char* label) const;
};

} // namespace tt::autotest
