#include "core/profile.h"

#if TT_PROFILE

#include <cstddef>
#include <cstdio>

#include "core/log.h"

#ifdef __3DS__
#include <3ds.h>
#else
#include <ctime>
#endif

namespace tt::prof {

namespace {

constexpr double kTickHz = 268111856.0; // SYSCLOCK_ARM11

const char* const kNames[kCount] = {
    "input",  "player",   "place",    "net",      "remote",   "actors",    "dplace", "dshadow",
    "dtoons", "dact",     "balln",    "hud",      "ui",       "audio",     "wait",   "fend",
    "feupd",  "fedraw",   "bossupd",  "bossdraw", "cogdoupd", "cogdodraw", "levupd", "rooms",
    "props",  "levgames", "minigame", "racing",   "golf",     "estate",    "loads",  "other",
    "dmini",  "dracing",  "dgolf",    "destate",  "shot"};

uint64_t defaultClock() {
#ifdef __3DS__
    return svcGetSystemTick();
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>((static_cast<double>(ts.tv_sec) * 1e9 + static_cast<double>(ts.tv_nsec)) *
                                 (kTickHz / 1e9));
#endif
}

TickFn gClock = defaultClock;

// snprintf at offset n; returns the new offset, never past the end of the buffer.
int append(char* buf, size_t cap, int n, const char* fmt, const char* a, double b) {
    if (n < 0 || static_cast<size_t>(n) >= cap)
        return n;
    const int w = snprintf(buf + n, cap - static_cast<size_t>(n), fmt, a, b);
    if (w < 0)
        return n;
    return static_cast<size_t>(n + w) >= cap ? static_cast<int>(cap) - 1 : n + w;
}

struct State {
    bool hitchPending = false;
    float hitchTotal = 0, hitchBusy = 0, hitchLargest = 0;
    Section hitchSection = kOther;
    uint64_t frameStart = 0;
    uint64_t lastLap = 0;
    bool open = false;
    uint64_t frameAcc[kCount] = {};
    uint64_t sum[kCount] = {};
    uint64_t peak[kCount] = {};
    uint64_t sumTotal = 0;
    uint32_t frames = 0;
    uint64_t windowStart = 0;
    // Last completed one-second window.
    float avg[kCount] = {};
    float max[kCount] = {};
    float totalAvg = 0.0f, busyAvg = 0.0f;
    int framesShown = 0;
    uint32_t windows = 0;
    uint32_t loggedWindows = 0;
};
State gS;

} // namespace

const char* name(Section s) {
    return s >= 0 && s < kCount ? kNames[s] : "";
}

float ticksToMs(uint64_t ticks) {
    return static_cast<float>(static_cast<double>(ticks) * (1000.0 / kTickHz));
}

void setClockForTest(TickFn fn) {
    gClock = fn ? fn : defaultClock;
}

void init() {
    gS = State();
}

void frameBegin() {
    State& s = gS;
    const uint64_t now = gClock();
    if (s.open) {
        // Uninstrumented tails stay visible; never mislabel an early owner as audio.
        s.frameAcc[kOther] += now - s.lastLap;
        // A five-second log cadence must not miss a bad intervening frame.
        constexpr float kHitchMs = 100.0f; // task's interactive-frame ceiling
        const uint64_t total = now - s.frameStart;
        if (ticksToMs(total) >= kHitchMs) {
            s.hitchPending = true;
            s.hitchTotal = ticksToMs(total);
            s.hitchBusy = ticksToMs(total - s.frameAcc[kWait]);
            s.hitchLargest = 0;
            for (int index = 0; index < kCount; ++index) {
                const float value = ticksToMs(s.frameAcc[index]);
                if (value > s.hitchLargest) {
                    s.hitchLargest = value;
                    s.hitchSection = Section(index);
                }
            }
        }
        for (int i = 0; i < kCount; i++) {
            const uint64_t t = s.frameAcc[i];
            s.sum[i] += t;
            if (t > s.peak[i])
                s.peak[i] = t;
            s.frameAcc[i] = 0;
        }
        s.sumTotal += now - s.frameStart;
        s.frames++;
    }
    if (s.windowStart == 0)
        s.windowStart = now;
    if (now - s.windowStart >= static_cast<uint64_t>(kTickHz) && s.frames > 0) {
        const double inv = 1.0 / s.frames;
        for (int i = 0; i < kCount; i++) {
            s.avg[i] = ticksToMs(static_cast<uint64_t>(static_cast<double>(s.sum[i]) * inv));
            s.max[i] = ticksToMs(s.peak[i]);
            s.sum[i] = s.peak[i] = 0;
        }
        s.totalAvg = ticksToMs(static_cast<uint64_t>(static_cast<double>(s.sumTotal) * inv));
        s.busyAvg = s.totalAvg - s.avg[kWait];
        s.framesShown = static_cast<int>(s.frames);
        s.sumTotal = 0;
        s.frames = 0;
        s.windowStart = now;
        s.windows++;
    }
    s.frameStart = s.lastLap = now;
    s.open = true;
}

void lap(Section sec) {
    State& s = gS;
    const uint64_t now = gClock();
    s.frameAcc[sec] += now - s.lastLap;
    s.lastLap = now;
}

float avgMs(Section s) {
    return s >= 0 && s < kCount ? gS.avg[s] : 0.0f;
}
float maxMs(Section s) {
    return s >= 0 && s < kCount ? gS.max[s] : 0.0f;
}
float totalAvgMs() {
    return gS.totalAvg;
}
float busyAvgMs() {
    return gS.busyAvg;
}
int windowFrames() {
    return gS.framesShown;
}

void logWindow(const char* label) {
    State& s = gS;
    if (s.hitchPending) {
        log::write("prof: [%s] hitch total %.2f busy %.2f ms largest %s %.2f", label, double(s.hitchTotal),
                   double(s.hitchBusy), name(s.hitchSection), double(s.hitchLargest));
        s.hitchPending = false;
    }
    if (s.windows == 0 || s.windows - s.loggedWindows < static_cast<uint32_t>(kLogPeriodSec))
        return;
    s.loggedWindows = s.windows;
    char line[200];
    int n = snprintf(line, sizeof(line), "prof: [%s] frames %d total %.2f busy %.2f ms avg:", label,
                     s.framesShown, static_cast<double>(s.totalAvg), static_cast<double>(s.busyAvg));
    for (int i = 0; i < kCount; i++) {
        if (i && i % 6 == 0) {
            log::write("%s", line);
            n = snprintf(line, sizeof(line), "prof: [%s] avg:", label);
        }
        n = append(line, sizeof(line), n, " %s %.2f", kNames[i], static_cast<double>(s.avg[i]));
    }
    log::write("%s", line);
    n = snprintf(line, sizeof(line), "prof: [%s] max:", label);
    for (int i = 0; i < kCount; i++) {
        if (i && i % 6 == 0) {
            log::write("%s", line);
            n = snprintf(line, sizeof(line), "prof: [%s] max:", label);
        }
        n = append(line, sizeof(line), n, " %s %.1f", kNames[i], static_cast<double>(s.max[i]));
    }
    log::write("%s", line);
}

} // namespace tt::prof

#endif // TT_PROFILE
