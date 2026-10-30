// Platform badges (tools/server/platform/README.md): the platform enum, the badge rule and
// the fixed-size per-session table of other toons' platforms. Pure logic, no libctru: the
// host test tools/platform_test/ checks it against the cases of test_platform.py.
#pragma once

#include <cstdint>

namespace tt::net {

// Wire value of TTPlatformManager.setMyPlatform(uint8) / setPlatforms(..., uint8[]), and the
// cell index in tools/platform-badges/atlas_*.png.
enum Platform : uint8_t {
    kPlatformUnknown = 0,
    kPlatformWeb = 1,
    kPlatformMac = 2,
    kPlatformIos = 3,
    kPlatformAndroid = 4,
    kPlatformWindows = 5,
    kPlatformVr = 6,
    kPlatformN3ds = 7,
    kPlatformLinux = 8,
};
constexpr int kNumPlatforms = 9;
constexpr uint32_t kPlatformManagerId = 4721; // global TTPlatformManager doId
constexpr int kMaxPlatformIdsPerRequest = 50;
constexpr uint32_t kPlatformFlushMs = 250;

// Any out-of-range value is UNKNOWN.
inline uint8_t clampPlatform(uint32_t v) { return v < uint32_t(kNumPlatforms) ? uint8_t(v) : uint8_t(kPlatformUnknown); }
// "mac", "N3DS", "3ds", "ipados", "7" ... -> enum; UNKNOWN when unrecognised (or null).
uint8_t platformFromName(const char* name);
const char* platformName(uint8_t p);
// Badge rule: exact compare, unknown hidden, never on our own toon.
inline bool shouldShowPlatformBadge(uint8_t own, uint8_t other, bool isLocal) {
    other = clampPlatform(other);
    return !isLocal && other != kPlatformUnknown && other != clampPlatform(own);
}

// avId -> platform for the session, plus the request queue (RequestBatcher in platforms.py).
// An id is requested once; the server pushes later changes on its own. When the table is
// full, the least recently seen answered/sent id is dropped (it is requested again if seen).
class PlatformBook {
public:
    static constexpr int kSlots = 128;

    void reset();
    // A toon we see. Returns true if it was queued for a request (new id).
    bool note(uint32_t avId, uint32_t ownAvId, uint32_t tick);
    // Up to max queued ids, oldest first; they become "sent". Returns the count.
    int take(uint32_t* out, int max);
    // A value from setPlatforms (answer or push). UNKNOWN is stored too (it clears a badge).
    void set(uint32_t avId, uint8_t platform, uint32_t tick);
    uint8_t get(uint32_t avId) const;
    int queued() const;
    int size() const { return used_; }

private:
    enum : uint8_t { kFree = 0, kQueued = 1, kSent = 2 };
    struct Slot {
        uint32_t avId;
        uint32_t seq;  // queue order
        uint32_t tick; // last note/set (eviction)
        uint8_t state;
        uint8_t platform;
    };
    Slot* find(uint32_t avId);
    const Slot* find(uint32_t avId) const;
    Slot* alloc();

    Slot slots_[kSlots] = {};
    int used_ = 0;
    uint32_t seq_ = 0;
};

} // namespace tt::net
