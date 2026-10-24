// Memory budgets. Budgets are defined in AGENTS.md ("Memory budget"); usage is
// measured live on the bottom-screen Debug tab.
#pragma once

#include <cstddef>

namespace tt::mem {

// Hard caps for Old 3DS (64 MB APPMEMTYPE 0). Keep in sync with AGENTS.md.
constexpr size_t kLinearBudget = 24u * 1024 * 1024; // GPU-visible: VBOs, textures, cmdbufs
constexpr size_t kHeapBudget = 24u * 1024 * 1024;   // regular heap: game state, net, parse buffers

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
