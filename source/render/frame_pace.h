// Frame pacing for the main loop (AGENTS.md section 4: fixed 30 Hz, one frame per vblank pair).
//
// The loop waits for its frame slot at the top of each iteration, before it reads input and
// pumps the network, so whatever arrived during the wait (most of a 33 ms frame) is simulated
// and drawn in this frame. With the wait inside C3D_FrameBegin(SYNCDRAW), after the net pump,
// every remote pose, chat line and button press waited one whole extra frame for the screen.
// Whoever begins the GPU frame this iteration (the main world pass, the front end, a boss or
// Cogdo frame owner) passes frameBeginFlags(): no second vblank wait once the loop has synced.
// C3D_FrameBegin(0) still fences the previous frame's GPU work, so resource publication rules
// (ToonCache::pump, the shared loader) are unchanged.
#pragma once

#include <citro3d.h>

namespace tt::render {

inline bool& frameSyncedFlag() {
    static bool synced = false;
    return synced;
}

// Top of the main loop: wait for the next frame slot.
inline void frameSyncTop() {
    C3D_FrameSync();
    frameSyncedFlag() = true;
}

// Flags for this iteration's C3D_FrameBegin.
inline u8 frameBeginFlags() {
    const bool synced = frameSyncedFlag();
    frameSyncedFlag() = false;
    return synced ? 0 : C3D_FRAME_SYNCDRAW;
}

} // namespace tt::render
