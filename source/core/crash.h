// Crash reporting. A CPU fault (data / prefetch abort, undefined instruction, VFP) on a thread
// that installed the handler, any svcBreak and any abort() end up in panic(): the reason goes to
// the log (flushed to the SD card) and on both screens, then the process exits.
#pragma once

namespace tt::crash {

// Installs the handler for the calling thread (call once from each long-lived thread).
void installForThisThread();
// gfx is initialized: panic() may draw its report with the CPU console.
void screensReady();
// True once a panic started (other threads stop drawing over the report).
bool panicking();
[[noreturn]] void panic(const char* what, const char* line1, const char* line2);

} // namespace tt::crash
