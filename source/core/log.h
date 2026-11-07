// Logging: every line goes to the emulator/debugger (svcOutputDebugString), to
// sdmc:/3ds/toontown3ds/log.txt, and to a small ring buffer for the Debug tab.
#pragma once

#include <cstddef>

namespace tt::log {

void init();
void shutdown();
void flush(); // force buffered lines to the SD card
// On (the default, for boot): flush after every line. Off: at most once a second.
void setFlushEveryLine(bool on);
void write(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

constexpr int kRingLines = 8;
constexpr int kLineLength = 64;
// i = 0 is the most recent line; returns "" past the end.
const char* recent(int i);

} // namespace tt::log

#define TT_LOG(...) ::tt::log::write(__VA_ARGS__)
