#include "core/log.h"

#include <3ds.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace tt::log {

namespace {

FILE* gFile = nullptr;
char gRing[kRingLines][kLineLength];
int gHead = 0;

} // namespace

void init() {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/toontown3ds", 0777);
    gFile = fopen("sdmc:/3ds/toontown3ds/log.txt", "w");
}

void shutdown() {
    if (gFile) fclose(gFile);
    gFile = nullptr;
}

void write(const char* fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) return;

    svcOutputDebugString(line, strlen(line));
    if (gFile) {
        fprintf(gFile, "[%llu] %s\n", osGetTime(), line);
        fflush(gFile);
    }
    gHead = (gHead + kRingLines - 1) % kRingLines;
    snprintf(gRing[gHead], kLineLength, "%s", line);
}

const char* recent(int i) {
    if (i < 0 || i >= kRingLines) return "";
    return gRing[(gHead + i) % kRingLines];
}

} // namespace tt::log
