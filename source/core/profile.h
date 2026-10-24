// Frame-phase profiler: svcGetSystemTick based, a fixed array of named sections, per-second
// averages, no allocation. Main thread only (it is not synchronized).
//
//   prof::frameBegin();                // top of the main loop; folds the previous frame
//   ... input ...        prof::lap(prof::kInput);    // time since the last lap -> section
//   ... player ...       prof::lap(prof::kPlayer);
//
// A lap charges the time since the previous lap (or frameBegin) to its section, so every
// line of the frame is accounted for, a `continue` simply ends the frame early, and the
// edits in the caller stay single lines. The sum of the sections is the frame's wall time
// (including kWait, the C3D_FrameBegin vblank/GPU sync). Averages and maxima over the last
// completed one-second window are read with avgMs()/maxMs(); logWindow() writes the "prof:"
// log lines every kLogPeriodSec. Define TT_PROFILE=0 to compile every call to nothing.
#pragma once

#include <cstdint>

#ifndef TT_PROFILE
#define TT_PROFILE 1
#endif

namespace tt::prof {

enum Section : int {
    kInput = 0,  // hid, autotest, UI input
    kPlayer,     // player.update (slide, ground, camera ray), local toon, footsteps
    kPlace,      // place.update (zone streaming, cellAt), exits, fades, zone changes
    kNet,        // net.update, events, outgoing pose/anim
    kRemote,     // remoteToons.update
    kActors,     // zone actors, boat, cchars, suits, stress toons
    kDrawPlace,  // place.draw (zone chunks)
    kDrawShadow, // drop shadows
    kDrawToons,  // local + remote + stress toons
    kDrawActors, // NPCs, boat, cchars, suits
    kBalloons,   // projection + nametags/speech balloons
    kHud,        // top-screen HUD + fade overlay
    kBottomUi,   // map markers, context, bottom screen UI
    kAudio,      // audio::update and bookkeeping after the frame ends
    kWait,       // C3D_FrameBegin: waits for the previous frame's GPU work and vblank
    kFrameEnd,   // C3D_FrameEnd (queues the frame)
    kCount
};

constexpr int kLogPeriodSec = 5;

#if TT_PROFILE
const char* name(Section s);
// Resets all accumulators.
void init();
// Top of the main loop: folds the previous frame into the window and starts the next.
void frameBegin();
// Charges the time since the previous lap to `s`.
void lap(Section s);
// Last completed one-second window (0 before the first one): mean over frames, and the
// largest single frame, in milliseconds. totalAvgMs() is the mean wall time of a frame,
// busyAvgMs() the mean excluding kWait (the CPU the main thread actually used).
float avgMs(Section s);
float maxMs(Section s);
float totalAvgMs();
float busyAvgMs();
int windowFrames();
// Call once per frame after frameBegin(): once kLogPeriodSec windows have completed,
// writes the averages and maxima as "prof: [label] ..." lines.
void logWindow(const char* label);

// Ticks to milliseconds, exposed for tests.
float ticksToMs(uint64_t ticks);
// Test hook: the clock the profiler reads (defaults to svcGetSystemTick on the 3DS).
using TickFn = uint64_t (*)();
void setClockForTest(TickFn fn);
#else
inline const char* name(Section) { return ""; }
inline void init() {}
inline void frameBegin() {}
inline void lap(Section) {}
inline float avgMs(Section) { return 0.0f; }
inline float maxMs(Section) { return 0.0f; }
inline float totalAvgMs() { return 0.0f; }
inline float busyAvgMs() { return 0.0f; }
inline int windowFrames() { return 0; }
inline void logWindow(const char*) {}
#endif

} // namespace tt::prof
