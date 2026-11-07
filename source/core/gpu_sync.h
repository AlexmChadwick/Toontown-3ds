#pragma once
// Exit-safe GPU waits and bounded thread joins.
//
// citro3d's APT hook turns its VBlank callbacks off on APTHOOK_ONSUSPEND and back on only on
// APTHOOK_ONRESTORE. HOME -> Close never restores us (APT wakes the app with WAKEUP_CANCEL /
// EXIT, no GPU right, no ONRESTORE), so C3D_FrameSync()'s frame counters never move again and
// any C3D_FrameSync() in a shutdown path waits forever: the HOME -> Close softlock.
// Use tt::gpu::frameSync() everywhere instead of C3D_FrameSync().
#include <3ds.h>
#include <citro3d.h>

namespace tt::gpu {

// False between ONSUSPEND and ONRESTORE, and from ONEXIT on. Set by main.cpp's APT hook.
// Host builds have no APT hook, so VBlank stays live and frameSync is C3D_FrameSync.
#ifdef __3DS__
bool vblankLive();
void setVblankLive(bool live);
#else
inline bool vblankLive() { return true; }
inline void setVblankLive(bool) {}
#endif

// C3D_FrameSync() that returns at once while VBlank is not delivered (suspended / closing).
// Skipping the fence at exit is safe: nothing is drawn any more and the process is ending.
inline void frameSync() {
    if (vblankLive()) C3D_FrameSync();
}

// threadJoin that cannot hang the exit. While the app runs normally (vblankLive) it waits as
// long as it takes, exactly like before; once APT has asked us to exit (or we are suspended)
// it gives up after timeoutNs, logs, and leaves the thread (not freed: freeing a running
// thread's stack would crash it); the process exit reclaims it. Returns true if joined.
#ifdef __3DS__
bool joinBounded(Thread t, const char* name, u64 timeoutNs = 500ULL * 1000 * 1000);
#else
inline bool joinBounded(Thread t, const char*, u64 = 500ULL * 1000 * 1000) {
    if (!t) return true;
    threadJoin(t, U64_MAX);
    threadFree(t);
    return true;
}
#endif

} // namespace tt::gpu
