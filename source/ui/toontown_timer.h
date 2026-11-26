// ToontownTimer (otp/otpbase/OTPTimer.py): the orange alarm clock with the seconds left on its
// face, black, red for the last five seconds. One copy of romfs:/baked/timer_clock.ttm
// (bake_timer_clock.py), loaded on first use on the main thread.
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include <cstddef>

namespace tt::ui {

class ToontownTimer {
public:
    bool init();
    void shutdown();
    // Centred on (cx, cy), size pixels square. Returns false when the clock is missing.
    bool draw(int seconds, float cx, float cy, float size, float z = 0.5f);

private:
    C2D_TextBuf buf_ = nullptr;
    C2D_Text text_{};
    int shown_ = -1;
};

// Minigame status lines carry the countdown as text ("Time: 54", "Flips: 3  Time: 40 s",
// "Depth: 2.0 m  (Time: 61 s)"). Copies `line` to `out` without that part and returns the
// seconds, or -1 (out = the line) when it has none.
int splitTimeLeft(const char* line, char* out, size_t outSize);

} // namespace tt::ui
