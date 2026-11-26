#include "core/gpu_sync.h"

#include "core/log.h"

namespace tt::gpu {
namespace {
volatile bool gVblankLive = true;
}

bool vblankLive() { return gVblankLive; }
void setVblankLive(bool live) { gVblankLive = live; }

bool joinBounded(Thread t, const char* name, u64 timeoutNs) {
    if (!t) return true;
    const Result rc = threadJoin(t, gVblankLive ? U64_MAX : timeoutNs);
    if (R_FAILED(rc)) {
        TT_LOG("exit: %s thread did not stop in %u ms (0x%08lX); leaving it", name,
               unsigned(timeoutNs / 1000000ULL), (unsigned long)rc);
        tt::log::flush();
        return false;
    }
    threadFree(t);
    return true;
}

} // namespace tt::gpu
