// Where a zone load spends its time. The 3DS build writes the same text to
// sdmc:/3ds/toontown3ds/load_profile.txt; host tools can call report() too.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#ifdef __3DS__
#include <3ds.h>
#endif

namespace tt::loadprof {

struct Counters {
    uint64_t readBytes = 0;
    uint32_t readUs = 0;
    uint32_t readCalls = 0;
    uint32_t parseUs = 0;
    uint32_t texUs = 0;
    uint32_t texCount = 0;
    uint32_t metaUs = 0;
    uint32_t playableMs = 0;
    uint32_t streamMs = 0;
    char zone[160] = {};
};

inline Counters& counters() {
    static Counters c;
    return c;
}

inline uint64_t nowUs() {
#ifdef __3DS__
    return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000000);
#else
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ull + static_cast<uint64_t>(ts.tv_nsec) / 1000ull;
#endif
}

inline void addRead(uint32_t bytes, uint32_t us) {
    Counters& c = counters();
    c.readBytes += bytes;
    c.readUs += us;
    c.readCalls += 1;
}

inline void addParse(uint32_t us) { counters().parseUs += us; }

inline void addTexUpload(uint32_t us) {
    counters().texUs += us;
    counters().texCount += 1;
}

inline void addMeta(uint32_t us) { counters().metaUs += us; }

inline void noteZone(const char* path) {
    if (!path) return;
    std::snprintf(counters().zone, sizeof(counters().zone), "%s", path);
}

inline void notePlayable(uint32_t ms) { counters().playableMs = ms; }
inline void noteStream(uint32_t ms) { counters().streamMs = ms; }

// Fills `out` (NUL-terminated) and returns it.
inline const char* report(char* out, uint32_t cap) {
    const Counters& c = counters();
    std::snprintf(out, cap,
                  "load profile\n"
                  "zone: %s\n"
                  "sd/romfs read: %u calls, %llu bytes, %u us\n"
                  "parse/index rebase: %u us\n"
                  "texture upload: %u textures, %u us\n"
                  "zone metadata: %u us\n"
                  "stream until resident: %u ms\n"
                  "first playable: %u ms\n",
                  c.zone[0] ? c.zone : "(none)", static_cast<unsigned>(c.readCalls),
                  static_cast<unsigned long long>(c.readBytes), static_cast<unsigned>(c.readUs),
                  static_cast<unsigned>(c.parseUs), static_cast<unsigned>(c.texCount),
                  static_cast<unsigned>(c.texUs), static_cast<unsigned>(c.metaUs),
                  static_cast<unsigned>(c.streamMs), static_cast<unsigned>(c.playableMs));
    return out;
}

inline bool dump(const char* path) {
    char text[640];
    report(text, sizeof(text));
    if (!path) return false;
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const size_t n = std::strlen(text);
    const bool ok = std::fwrite(text, 1, n, f) == n;
    std::fclose(f);
    return ok;
}

} // namespace tt::loadprof
