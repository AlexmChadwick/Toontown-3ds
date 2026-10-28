#include "core/memory.h"

#include <3ds.h>
#include <malloc.h>

#include "core/log.h"

// libctru reads these at startup to size the heaps. Pinning them explicitly keeps
// the split identical across Old/New 3DS and HBL/CIA launches instead of depending
// on whatever memory the host process happened to leave.
extern "C" {
u32 __ctru_heap_size = tt::mem::kHeapBudget;
u32 __ctru_linear_heap_size = tt::mem::kLinearBudget;
}

namespace tt::mem {

namespace {
LightLock gLinearLock;
bool gLinearLockReady = false;
size_t gPeakHeap = 0, gPeakLinear = 0, gPeakVram = 0;
size_t gLoggedHeap = 0, gLoggedLinear = 0, gLoggedVram = 0;
bool gLowLogged = false;
constexpr size_t kVramTotal = 6u * 1024 * 1024;
} // namespace

void sampleWatermarks() {
    const size_t heap = static_cast<size_t>(mallinfo().uordblks);
    const size_t linFree = linearSpaceFree();
    const size_t linUsed = kLinearBudget > linFree ? kLinearBudget - linFree : 0;
    const size_t vFree = vramSpaceFree();
    const size_t vramUsed = kVramTotal > vFree ? kVramTotal - vFree : 0;
    if (heap > gPeakHeap) gPeakHeap = heap;
    if (linUsed > gPeakLinear) gPeakLinear = linUsed;
    if (vramUsed > gPeakVram) gPeakVram = vramUsed;
    if (gPeakHeap >= gLoggedHeap + kPeakStepBytes || gPeakLinear >= gLoggedLinear + kPeakStepBytes ||
        gPeakVram >= gLoggedVram + kPeakStepBytes) {
        gLoggedHeap = gPeakHeap;
        gLoggedLinear = gPeakLinear;
        gLoggedVram = gPeakVram;
        TT_LOG("mem: peak heap %u KB, linear %u KB, vram %u KB", static_cast<unsigned>(gPeakHeap / 1024),
               static_cast<unsigned>(gPeakLinear / 1024), static_cast<unsigned>(gPeakVram / 1024));
    }
    if (!gLowLogged && linFree < kLinearReserveBytes) {
        gLowLogged = true;
        TT_LOG("mem: LOW linear %u KB free (reserve %u KB)", static_cast<unsigned>(linFree / 1024),
               static_cast<unsigned>(kLinearReserveBytes / 1024));
    }
}

void linearLockInit() {
    LightLock_Init(&gLinearLock);
    gLinearLockReady = true;
}

void linearLock() {
    if (gLinearLockReady) LightLock_Lock(&gLinearLock);
}

void linearUnlock() {
    if (gLinearLockReady) LightLock_Unlock(&gLinearLock);
}

} // namespace tt::mem
