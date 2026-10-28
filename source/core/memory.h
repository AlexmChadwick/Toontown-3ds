// Memory budgets. Budgets are defined in AGENTS.md ("Memory budget"); usage is
// measured live on the bottom-screen Debug tab.
#pragma once

#include <cstddef>

namespace tt::mem {

// Hard caps for Old 3DS (64 MB APPMEMTYPE 0). Keep in sync with AGENTS.md.
constexpr size_t kLinearBudget = 24u * 1024 * 1024; // GPU-visible: VBOs, textures, cmdbufs
constexpr size_t kHeapBudget = 24u * 1024 * 1024;   // regular heap: game state, net, parse buffers

// Low-water tracking. sampleWatermarks() is cheap (three counters) and runs once per frame.
// It logs `mem: peak ...` each time heap use, linear use or VRAM use grows by at least
// kPeakStepBytes past the previous logged peak, and `mem: LOW linear ...` once when free
// linear memory drops below kLinearReserveBytes (the AGENTS.md 2 MB reserve).
// Tests can grep the log for the last `mem: peak` line instead of sampling the overlay.
constexpr size_t kPeakStepBytes = 512u * 1024;
constexpr size_t kLinearReserveBytes = 2u * 1024 * 1024;
void sampleWatermarks();

// libctru's linear allocator (and citro3d texture init/delete, which use it) is not
// thread-safe. Every runtime linearAlloc/linearFree/C3D_TexInit*/C3D_TexDelete must hold
// this lock; it is never held across file IO.
void linearLockInit();
void linearLock();
void linearUnlock();
struct LinearGuard {
    LinearGuard() { linearLock(); }
    ~LinearGuard() { linearUnlock(); }
    LinearGuard(const LinearGuard&) = delete;
    LinearGuard& operator=(const LinearGuard&) = delete;
};

} // namespace tt::mem
