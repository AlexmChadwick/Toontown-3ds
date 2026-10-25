// CPU exception reporting: a fault (data / prefetch abort, undefined instruction, VFP) on a
// thread that installed the handler writes its type, address and registers to the log,
// flushes it to the SD card and exits, instead of the process vanishing without a trace.
#pragma once

namespace tt::crash {

// Installs the handler for the calling thread (call once from each long-lived thread).
void installForThisThread();

} // namespace tt::crash
