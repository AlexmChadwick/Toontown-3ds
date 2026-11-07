// Host stand-in for source/core/log.cpp (which needs libctru): prints to stdout
// with a millisecond timestamp so test logs line up with the fake server.
#include "core/log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace tt::log {

namespace {
char gRing[kRingLines][kLineLength];
int gHead = 0;
} // namespace

void init() {}
void shutdown() {}
void flush() {}
void setFlushEveryLine(bool) {}

void write(const char* fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    tm t;
    localtime_r(&ts.tv_sec, &t);
    printf("[%02d:%02d:%02d.%03ld] %s\n", t.tm_hour, t.tm_min, t.tm_sec, ts.tv_nsec / 1000000, line);
    fflush(stdout);
    gHead = (gHead + kRingLines - 1) % kRingLines;
    memcpy(gRing[gHead], line, kLineLength - 1); // the ring keeps a prefix, like core/log.cpp
    gRing[gHead][kLineLength - 1] = 0;
}

const char* recent(int i) {
    if (i < 0 || i >= kRingLines) return "";
    return gRing[(gHead + i) % kRingLines];
}

} // namespace tt::log
