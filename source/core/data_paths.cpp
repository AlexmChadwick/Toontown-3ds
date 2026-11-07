// Game data ships inside the CIA's RomFS (romfs:/data/...), so a blank SD card is enough.
// Code keeps using the historical sdmc:/3ds/toontown3ds/data/... paths; every read of one is
// tried in the RomFS first and falls back to the SD card (a dev copy, or a 3dsx build without
// bundled data). Writes are never redirected. Linked with -Wl,--wrap=fopen,--wrap=stat.
#include <sys/stat.h>

#include <cstdio>
#include <cstring>

extern "C" {
FILE* __real_fopen(const char* path, const char* mode);
int __real_stat(const char* path, struct stat* st);
}

namespace {

constexpr char kSdPrefix[] = "sdmc:/3ds/toontown3ds/data/";
constexpr char kRomPrefix[] = "romfs:/data/";

// Returns false when path is not under the SD data dir (or too long to remap).
bool toRomfs(const char* path, char* out, size_t cap) {
    if (!path || strncmp(path, kSdPrefix, sizeof(kSdPrefix) - 1) != 0) return false;
    const int n = snprintf(out, cap, "%s%s", kRomPrefix, path + sizeof(kSdPrefix) - 1);
    return n > 0 && static_cast<size_t>(n) < cap;
}

} // namespace

extern "C" {

FILE* __wrap_fopen(const char* path, const char* mode) {
    char rom[320];
    if (mode && mode[0] == 'r' && !strchr(mode, '+') && toRomfs(path, rom, sizeof(rom))) {
        if (FILE* f = __real_fopen(rom, mode)) return f;
    }
    return __real_fopen(path, mode);
}

int __wrap_stat(const char* path, struct stat* st) {
    char rom[320];
    if (toRomfs(path, rom, sizeof(rom)) && __real_stat(rom, st) == 0) return 0;
    return __real_stat(path, st);
}

} // extern "C"
