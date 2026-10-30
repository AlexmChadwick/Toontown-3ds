#include "net/platform_badge.h"

#include <cctype>
#include <cstring>

namespace tt::net {

namespace {

const char* const kNames[kNumPlatforms] = {"UNKNOWN", "WEB", "MAC", "IOS", "ANDROID", "WINDOWS", "VR", "N3DS", "LINUX"};

struct Alias {
    const char* name;
    uint8_t platform;
};
const Alias kAliases[] = {{"3DS", kPlatformN3ds},     {"IPHONE", kPlatformIos},  {"IPAD", kPlatformIos},
                          {"IPADOS", kPlatformIos},   {"MACOS", kPlatformMac},   {"DARWIN", kPlatformMac},
                          {"WIN", kPlatformWindows},  {"WIN32", kPlatformWindows}, {"QUEST", kPlatformVr},
                          {"BROWSER", kPlatformWeb}};

} // namespace

uint8_t platformFromName(const char* name) {
    if (!name) return kPlatformUnknown;
    while (*name == ' ' || *name == '\t') name++;
    char key[16];
    int n = 0;
    for (; name[n] && n < int(sizeof(key)) - 1; n++) key[n] = char(toupper(static_cast<unsigned char>(name[n])));
    if (name[n]) return kPlatformUnknown; // longer than any name
    while (n > 0 && (key[n - 1] == ' ' || key[n - 1] == '\t' || key[n - 1] == '\r' || key[n - 1] == '\n')) n--;
    key[n] = '\0';
    if (n == 0) return kPlatformUnknown;
    for (const Alias& a : kAliases)
        if (!strcmp(key, a.name)) return a.platform;
    bool digits = true;
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        if (key[i] < '0' || key[i] > '9') {
            digits = false;
            break;
        }
        v = v * 10 + uint32_t(key[i] - '0');
        if (v > 255) v = 255;
    }
    if (digits) return clampPlatform(v);
    for (int i = 0; i < kNumPlatforms; i++)
        if (!strcmp(key, kNames[i])) return uint8_t(i);
    return kPlatformUnknown;
}

const char* platformName(uint8_t p) { return kNames[clampPlatform(p)]; }

void PlatformBook::reset() {
    memset(slots_, 0, sizeof(slots_));
    used_ = 0;
    seq_ = 0;
}

PlatformBook::Slot* PlatformBook::find(uint32_t avId) {
    for (Slot& s : slots_)
        if (s.state != kFree && s.avId == avId) return &s;
    return nullptr;
}

const PlatformBook::Slot* PlatformBook::find(uint32_t avId) const {
    for (const Slot& s : slots_)
        if (s.state != kFree && s.avId == avId) return &s;
    return nullptr;
}

PlatformBook::Slot* PlatformBook::alloc() {
    Slot* victim = nullptr;
    for (Slot& s : slots_) {
        if (s.state == kFree) {
            used_++;
            return &s;
        }
        // Queued ids are never evicted (their request has not gone out yet).
        if (s.state != kQueued && (!victim || int32_t(s.tick - victim->tick) < 0)) victim = &s;
    }
    return victim; // null only if every slot is queued
}

bool PlatformBook::note(uint32_t avId, uint32_t ownAvId, uint32_t tick) {
    if (avId == 0 || avId == ownAvId) return false;
    if (Slot* s = find(avId)) {
        s->tick = tick;
        return false;
    }
    Slot* s = alloc();
    if (!s) return false;
    *s = Slot{avId, seq_++, tick, kQueued, kPlatformUnknown};
    return true;
}

int PlatformBook::take(uint32_t* out, int max) {
    int n = 0;
    while (n < max) {
        Slot* first = nullptr;
        for (Slot& s : slots_)
            if (s.state == kQueued && (!first || int32_t(s.seq - first->seq) < 0)) first = &s;
        if (!first) break;
        first->state = kSent;
        out[n++] = first->avId;
    }
    return n;
}

void PlatformBook::set(uint32_t avId, uint8_t platform, uint32_t tick) {
    if (avId == 0) return;
    Slot* s = find(avId);
    if (!s) {
        s = alloc();
        if (!s) return;
        *s = Slot{avId, seq_++, tick, kSent, kPlatformUnknown};
    }
    if (s->state == kQueued) s->state = kSent; // the server already knows we watch it
    s->platform = clampPlatform(platform);
    s->tick = tick;
}

uint8_t PlatformBook::get(uint32_t avId) const {
    const Slot* s = find(avId);
    return s ? s->platform : uint8_t(kPlatformUnknown);
}

int PlatformBook::queued() const {
    int n = 0;
    for (const Slot& s : slots_) n += s.state == kQueued;
    return n;
}

} // namespace tt::net
