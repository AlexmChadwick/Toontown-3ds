#include "core/memory.h"

#include <3ds.h>

// Fallback defaults; the startup allocator selects the granted-region profile.
extern "C" {
u32 __ctru_heap_size = tt::mem::kHeapBudget;
u32 __ctru_linear_heap_size = tt::mem::kLinearBudget;
}

// libctru's stock __system_allocateHeaps calls svcBreak (the HOME Menu's "An error has occurred,
// forcing the software to close") before main() when the fixed budgets exceed what the process
// may commit. A CIA's commit limit on real hardware can be lower than Azahar's, so fit the budgets
// to what is actually free instead: linear first (GPU data), the rest as heap, both shrunk if
// needed, and never break. main() reports a too-small result on screen (tt::mem::heapReport).
extern "C" {
extern u32 __ctru_heap;
extern u32 __ctru_linear_heap;
extern char* fake_heap_start;
extern char* fake_heap_end;
void mappableInit(u32 addrMin, u32 addrMax);

static u32 gRemainingAtBoot = 0;
static u32 gAllocFailures = 0;
static u32 gApplicationRegion = 0;
static u32 gAppMemType = 0;

void __system_allocateHeaps(void) {
    gApplicationRegion = osGetMemRegionSize(MEMREGION_APPLICATION);
    gAppMemType = *reinterpret_cast<volatile const u8*>(0x1FF80030);
    const auto profile = tt::mem::memoryProfile(gApplicationRegion);
    __ctru_heap_size = profile.heap;
    __ctru_linear_heap_size = profile.linear;
    Handle reslimit = 0;
    s64 maxCommit = 0, curCommit = 0;
    if (R_SUCCEEDED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE))) {
        ResourceLimitType type = RESLIMIT_COMMIT;
        svcGetResourceLimitLimitValues(&maxCommit, reslimit, &type, 1);
        svcGetResourceLimitCurrentValues(&curCommit, reslimit, &type, 1);
        svcCloseHandle(reslimit);
    }
    u32 remaining = maxCommit > curCommit ? static_cast<u32>(maxCommit - curCommit) & ~0xFFFu : 0;
    gRemainingAtBoot = remaining;
    // Leave a little slack for the kernel-side commit of thread stacks the system makes for us.
    constexpr u32 kSlack = 256 * 1024;
    u32 avail = remaining > kSlack ? remaining - kSlack : 0;
    if (avail == 0) avail = __ctru_heap_size + __ctru_linear_heap_size; // unknown limit: just try
    u32 linear = __ctru_linear_heap_size;
    u32 heap = __ctru_heap_size;
    if (linear + heap > avail) { // shrink both, keeping the 1:1 split
        linear = (avail / 2) & ~0xFFFu;
        heap = (avail - linear) & ~0xFFFu;
    }
    // Allocate, stepping down 1 MB at a time if the kernel still says no.
    constexpr u32 kStep = 1024 * 1024;
    while (heap >= kStep) {
        __ctru_heap = 0;
        u32 addr = 0;
        if (R_SUCCEEDED(svcControlMemory(&addr, OS_HEAP_AREA_BEGIN, 0, heap, MEMOP_ALLOC, (MemPerm)(MEMPERM_READ | MEMPERM_WRITE)))) {
            __ctru_heap = addr;
            break;
        }
        gAllocFailures++;
        heap -= kStep;
    }
    if (__ctru_heap == 0) heap = 0;
    while (linear >= kStep) {
        u32 addr = 0;
        if (R_SUCCEEDED(svcControlMemory(&addr, 0, 0, linear, MEMOP_ALLOC_LINEAR, (MemPerm)(MEMPERM_READ | MEMPERM_WRITE)))) {
            __ctru_linear_heap = addr;
            break;
        }
        gAllocFailures++;
        linear -= kStep;
    }
    if (__ctru_linear_heap == 0) linear = 0;
    __ctru_heap_size = heap;
    __ctru_linear_heap_size = linear;
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
    fake_heap_start = reinterpret_cast<char*>(__ctru_heap);
    fake_heap_end = reinterpret_cast<char*>(__ctru_heap + heap);
}
}

namespace tt::mem {

namespace {
LightLock gLinearLock;
bool gLinearLockReady = false;
} // namespace

HeapReport heapReport() {
    HeapReport r;
    r.applicationRegion = gApplicationRegion;
    r.appMemType = gAppMemType;
    r.remainingAtBoot = gRemainingAtBoot;
    r.heap = __ctru_heap_size;
    r.linear = __ctru_linear_heap_size;
    r.allocFailures = gAllocFailures;
    return r;
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
