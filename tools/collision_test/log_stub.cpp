// Host stand-in for source/core/log.cpp: prints to stderr.
#include <cstdarg>
#include <cstdio>

#include "core/log.h"

namespace tt::log {

void init() {}
void shutdown() {}
void write(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
const char* recent(int) { return ""; }

} // namespace tt::log
