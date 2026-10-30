// Host test for source/net/platform_badge.* (cases mirror tools/server/platform/test_platform.py).
#include <cstdio>
#include <cstring>

#include "net/platform_badge.h"

using namespace tt::net;

static int fails = 0, checks = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        ++checks;                                                   \
        if (!(c)) {                                                 \
            ++fails;                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
        }                                                           \
    } while (0)

static void enumMapping() {
    // Wire values / atlas cells are fixed by the README table.
    CHECK(kPlatformUnknown == 0 && kPlatformWeb == 1 && kPlatformMac == 2 && kPlatformIos == 3);
    CHECK(kPlatformAndroid == 4 && kPlatformWindows == 5 && kPlatformVr == 6 && kPlatformN3ds == 7);
    CHECK(kPlatformLinux == 8 && kNumPlatforms == 9 && kPlatformManagerId == 4721);
    const char* names[] = {"UNKNOWN", "WEB", "MAC", "IOS", "ANDROID", "WINDOWS", "VR", "N3DS", "LINUX"};
    for (int i = 0; i < kNumPlatforms; i++) {
        CHECK(platformFromName(names[i]) == i);
        CHECK(!strcmp(platformName(uint8_t(i)), names[i]));
        char num[4];
        snprintf(num, sizeof(num), "%d", i);
        CHECK(platformFromName(num) == i);
    }
    CHECK(platformFromName("mac") == kPlatformMac);
    CHECK(platformFromName(" 3ds \n") == kPlatformN3ds);
    CHECK(platformFromName("n3ds") == kPlatformN3ds);
    CHECK(platformFromName("iPhone") == kPlatformIos && platformFromName("ipad") == kPlatformIos);
    CHECK(platformFromName("ipados") == kPlatformIos && platformFromName("macos") == kPlatformMac);
    CHECK(platformFromName("darwin") == kPlatformMac && platformFromName("win32") == kPlatformWindows);
    CHECK(platformFromName("win") == kPlatformWindows && platformFromName("quest") == kPlatformVr);
    CHECK(platformFromName("browser") == kPlatformWeb);
    CHECK(platformFromName("9") == kPlatformUnknown && platformFromName("255") == kPlatformUnknown);
    CHECK(platformFromName("99999999999") == kPlatformUnknown);
    CHECK(platformFromName("penguin") == kPlatformUnknown && platformFromName("") == kPlatformUnknown);
    CHECK(platformFromName(nullptr) == kPlatformUnknown);
    CHECK(platformFromName("a-very-long-platform-name") == kPlatformUnknown);
    CHECK(clampPlatform(8) == 8 && clampPlatform(9) == 0 && clampPlatform(0xffffffffu) == 0);
    CHECK(!strcmp(platformName(200), "UNKNOWN"));
}

static void shouldShow() {
    CHECK(shouldShowPlatformBadge(kPlatformN3ds, kPlatformMac, false));
    CHECK(!shouldShowPlatformBadge(kPlatformN3ds, kPlatformN3ds, false)); // same platform
    CHECK(shouldShowPlatformBadge(kPlatformMac, kPlatformIos, false));    // exact compare:
    CHECK(shouldShowPlatformBadge(kPlatformIos, kPlatformMac, false));    // Mac != iPhone
    CHECK(!shouldShowPlatformBadge(kPlatformN3ds, kPlatformUnknown, false));
    CHECK(!shouldShowPlatformBadge(kPlatformN3ds, 42, false)); // out of range = unknown
    CHECK(!shouldShowPlatformBadge(kPlatformN3ds, kPlatformMac, true)); // never on our own toon
    CHECK(shouldShowPlatformBadge(kPlatformUnknown, kPlatformWeb, false)); // we are unknown
    CHECK(shouldShowPlatformBadge(42, kPlatformWeb, false));
}

static void batching() {
    static PlatformBook b;
    b.reset();
    CHECK(!b.note(0, 5, 1));    // no avatar
    CHECK(!b.note(5, 5, 1));    // our own toon
    CHECK(b.note(100, 5, 1));
    CHECK(!b.note(100, 5, 2));  // already queued
    uint32_t ids[kMaxPlatformIdsPerRequest];
    CHECK(b.take(ids, kMaxPlatformIdsPerRequest) == 1 && ids[0] == 100);
    CHECK(!b.note(100, 5, 3));  // already sent: the server pushes later values
    CHECK(b.take(ids, kMaxPlatformIdsPerRequest) == 0);
    for (uint32_t i = 0; i < 70; i++) CHECK(b.note(1000 + i, 5, 10 + i));
    CHECK(b.queued() == 70);
    CHECK(b.take(ids, kMaxPlatformIdsPerRequest) == 50);
    bool order = true;
    for (int i = 0; i < 50; i++) order &= ids[i] == 1000u + uint32_t(i); // oldest first
    CHECK(order);
    CHECK(b.take(ids, kMaxPlatformIdsPerRequest) == 20 && ids[0] == 1050 && ids[19] == 1069);
    CHECK(b.take(ids, kMaxPlatformIdsPerRequest) == 0);
    // Answers and pushes.
    CHECK(b.get(100) == kPlatformUnknown);
    b.set(100, kPlatformMac, 200);
    CHECK(b.get(100) == kPlatformMac);
    b.set(100, kPlatformVr, 201); // VR enter push
    CHECK(b.get(100) == kPlatformVr);
    b.set(100, 77, 202);          // clamped
    CHECK(b.get(100) == kPlatformUnknown);
    b.set(4242, kPlatformWeb, 203); // unsolicited push for an id we never noted
    CHECK(b.get(4242) == kPlatformWeb && !b.note(4242, 5, 204));
    b.set(0, kPlatformWeb, 205);
    CHECK(b.get(0) == kPlatformUnknown);
    b.reset();
    CHECK(b.size() == 0 && b.get(100) == kPlatformUnknown && b.note(100, 5, 1));
}

static void bounded() {
    static PlatformBook b;
    b.reset();
    uint32_t ids[kMaxPlatformIdsPerRequest];
    for (uint32_t i = 0; i < PlatformBook::kSlots; i++) b.note(1 + i, 0, i);
    CHECK(b.size() == PlatformBook::kSlots);
    CHECK(!b.note(9999, 0, 500)); // every slot queued: nothing is evicted
    while (b.take(ids, kMaxPlatformIdsPerRequest) > 0) {
    }
    b.note(1, 0, 600); // touched again: newest
    CHECK(b.note(9999, 0, 601)); // evicts the least recently seen sent id (2)
    CHECK(b.size() == PlatformBook::kSlots);
    CHECK(b.take(ids, kMaxPlatformIdsPerRequest) == 1 && ids[0] == 9999);
    CHECK(b.note(2, 0, 602)); // evicted id is requested again when seen
}

int main() {
    enumMapping();
    shouldShow();
    batching();
    bounded();
    printf("platform_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
