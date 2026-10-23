#include "core/memory.h"

#include <3ds/types.h>

// libctru reads these at startup to size the heaps. Pinning them explicitly keeps
// the split identical across Old/New 3DS and HBL/CIA launches instead of depending
// on whatever memory the host process happened to leave.
extern "C" {
u32 __ctru_heap_size = tt::mem::kHeapBudget;
u32 __ctru_linear_heap_size = tt::mem::kLinearBudget;
}
