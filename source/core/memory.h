// Memory budgets. Budgets are defined in AGENTS.md ("Memory budget"); usage is
// measured live on the bottom-screen Debug tab.
#pragma once

#include <cstddef>

namespace tt::mem {

// Hard caps for Old 3DS (64 MB APPMEMTYPE 0). Keep in sync with AGENTS.md.
constexpr size_t kLinearBudget = 24u * 1024 * 1024; // GPU-visible: VBOs, textures, cmdbufs
constexpr size_t kHeapBudget = 24u * 1024 * 1024;   // regular heap: game state, net, parse buffers

} // namespace tt::mem
