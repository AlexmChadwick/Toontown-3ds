#include "core/log.h"

#include <3ds.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace tt::log {

namespace {

FILE* gFile = nullptr;
// Background threads (toon loader, network) log too; serialize ring + file writes.
LightLock gLock;
bool gLockReady = false;
u64 gLastFlushMs = 0;
char gRing[kRingLines][kLineLength];
int gHead = 0;

} // namespace

void init() {
    LightLock_Init(&gLock);
    gLockReady = true;
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/toontown3ds", 0777);
    gFile = fopen("sdmc:/3ds/toontown3ds/log.txt", "w");
}

void flush() {
    if (gLockReady) LightLock_Lock(&gLock);
    if (gFile) fflush(gFile);
    if (gLockReady) LightLock_Unlock(&gLock);
}

void shutdown() {
    flush();
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
    if (gLockReady) LightLock_Lock(&gLock);
    if (gFile) {
        const u64 now = osGetTime();
        fprintf(gFile, "[%llu] %s\n", now, line);
        // SD writes can stall for milliseconds on hardware: flush at most once a second
        // (flush() is also called on suspend/sleep and exit).
        if (now - gLastFlushMs >= 1000) {
            fflush(gFile);
            gLastFlushMs = now;
        }
    }
    gHead = (gHead + kRingLines - 1) % kRingLines;
    snprintf(gRing[gHead], kLineLength, "%s", line);
    if (gLockReady) LightLock_Unlock(&gLock);
}

const char* recent(int i) {
    if (i < 0 || i >= kRingLines) return "";
    return gRing[(gHead + i) % kRingLines];
}

} // namespace tt::log
