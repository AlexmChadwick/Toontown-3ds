// Host stub of <3ds.h> for the GUI panel tests (no emulator, no libctru).
// Only what the compiled bottom-screen sources use; shaped after tools/qafix_test and
// tools/perf_test stubs.
#pragma once

#include <cstdint>

#define __3DS__ 1 // sources branch on this to include <3ds.h>; ours resolves to this stub

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

using Result = s32;
using Handle = u32;
using LightLock = s32;
using Thread = void*;

// libctru light event: a plain flag is enough for the host build.
struct LightEvent {
    bool signalled = false;
};
inline void LightEventInit(LightEvent* e, bool autoReset) { (void)autoReset; e->signalled = false; }
inline void LightEventClear(LightEvent* e) { e->signalled = false; }
inline void LightEventSignal(LightEvent* e) { e->signalled = true; }
inline bool LightEvent_TryWait(LightEvent* e) {
    if (!e->signalled) return false;
    e->signalled = false;
    return true;
}

struct touchPosition {
    u16 px = 0, py = 0;
};

struct circlePosition {
    s16 dx = 0, dy = 0;
};

enum : u32 {
    KEY_A = 1u << 0,
    KEY_B = 1u << 1,
    KEY_X = 1u << 10,
    KEY_Y = 1u << 11,
    KEY_L = 1u << 2,
    KEY_R = 1u << 9,
    KEY_ZL = 1u << 14,
    KEY_ZR = 1u << 15,
    KEY_SELECT = 1u << 12,
    KEY_START = 1u << 3,
    KEY_TOUCH = 1u << 20,
    KEY_DUP = 1u << 6,
    KEY_DDOWN = 1u << 8,
    KEY_DLEFT = 1u << 5,
    KEY_DRIGHT = 1u << 4,
    KEY_CSTICK = 1u << 22, // unused by the compiled sources; present for completeness
    KEY_UP = KEY_DUP,
    KEY_DOWN = KEY_DDOWN,
    KEY_LEFT = KEY_DLEFT,
    KEY_RIGHT = KEY_DRIGHT,
};

// Deterministic host clock (the tests set it; recorded and replayed APT-style behaviour is
// not needed, only monotonic ms).
extern u64 gHostNowMs;
inline u64 osGetTime() { return gHostNowMs; }
inline u64 svcGetSystemTick() { return gHostNowMs * 268111856ULL / 1000ULL; } // ~3DS tick rate
constexpr u64 SYSCLOCK_ARM11 = 268111856ULL;

inline u32 hidKeysHeld() { return 0; }
inline u32 hidKeysDown() { return 0; }
inline void hidTouchRead(touchPosition*) {}
inline void hidCircleRead(circlePosition*) {}

