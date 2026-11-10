// Groups finder logic shared by the 3DS client and tools/groups_test.
//
// Wire ids are the DCFile numbers for otp.dc, toon.dc, platform.dc, groups.dc
// in that order. TTPlatformManager is 2270 through 2273. The nine TTGroupInfo
// struct members take 2274 through 2282, because Panda numbers struct fields.
// TTGroupsManager therefore starts at 2283. avatarGone is field 2294, which is
// class-local index 11, not the wire id. The CLIENT_HELLO hash stays the stock
// otp+toon value. tools/groups_test pins every id below against DCFile.
// The UberDOG owns the board; this header is the client cache, the datagram
// layout, and the 320x240 sheet. See docs/design/groups.md.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "net/datagram.h"

namespace tt::groups {

// WCAG relative luminance. Groups button labels are checked against 4.5:1.
struct Rgb8 {
    uint8_t r, g, b;
};

inline double channelToLinear(double c) {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

inline double relativeLuminance(Rgb8 c) {
    return 0.2126 * channelToLinear(c.r / 255.0) + 0.7152 * channelToLinear(c.g / 255.0) +
           0.0722 * channelToLinear(c.b / 255.0);
}

inline double contrastRatio(Rgb8 a, Rgb8 b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double hi = la > lb ? la : lb;
    const double lo = la > lb ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

// Text on the button face. These are the theme.h tokens page_groups.cpp draws:
// enabled Join is kWhite on kGreenDark, disabled and Full are kInk on kGrey,
// own Leave is kWhite on kRedDark, filters are kWhite on kBlueDark or kInk on kGrey.
struct ButtonInk {
    const char* state;
    Rgb8 text;
    Rgb8 face;
};

inline constexpr ButtonInk kButtonInks[] = {
    {"enabled", {0xFF, 0xFF, 0xFF}, {0x2E, 0x7E, 0x26}},
    {"disabled", {0x2A, 0x1A, 0x0C}, {0xC8, 0xC2, 0xB6}},
    {"full", {0x2A, 0x1A, 0x0C}, {0xC8, 0xC2, 0xB6}},
    {"own Leave", {0xFF, 0xFF, 0xFF}, {0x9C, 0x1E, 0x16}},
    {"enabled filter", {0xFF, 0xFF, 0xFF}, {0x2B, 0x58, 0xA8}},
    {"disabled filter", {0x2A, 0x1A, 0x0C}, {0xC8, 0xC2, 0xB6}},
};

constexpr uint32_t kManagerDoId = 4722;
constexpr uint16_t kAvatarGoneField = 2294;
constexpr uint16_t kFieldBase = 2283; // kAvatarGoneField - 11
constexpr uint16_t kRequestBoard = kFieldBase + 0;
constexpr uint16_t kStopBoard = kFieldBase + 1;
constexpr uint16_t kSetContext = kFieldBase + 2;
constexpr uint16_t kCreateGroup = kFieldBase + 3;
constexpr uint16_t kJoinGroup = kFieldBase + 4;
constexpr uint16_t kLeaveGroup = kFieldBase + 5;
constexpr uint16_t kSetBoard = kFieldBase + 6;
constexpr uint16_t kGroupChanged = kFieldBase + 7;
constexpr uint16_t kGroupGone = kFieldBase + 8;
constexpr uint16_t kResult = kFieldBase + 9;
constexpr uint16_t kNoteActivity = kFieldBase + 10;
constexpr uint16_t kAvatarGone = kFieldBase + 11;
static_assert(kAvatarGone == kAvatarGoneField, "avatarGone wire id");

// Keyword bit, same as dc::kKwClsend. Outgoing fields below carry it.
// avatarGone and the board updates do not: the client must not send them.
constexpr uint16_t kKwClsend = 1u << 4;
constexpr uint16_t kwRequestBoard = kKwClsend;
constexpr uint16_t kwStopBoard = kKwClsend;
constexpr uint16_t kwSetContext = kKwClsend;
constexpr uint16_t kwCreateGroup = kKwClsend;
constexpr uint16_t kwJoinGroup = kKwClsend;
constexpr uint16_t kwLeaveGroup = kKwClsend;

constexpr int kBoss = 0, kBuilding = 1, kTask = 2;
constexpr int kSellbot = 0, kCashbot = 1, kLawbot = 2, kBossbot = 3;
constexpr int kMinSize = 2, kMaxSize = 8;
constexpr int kMaxMembers = 8;
constexpr int kMaxGroups = 32; // client cache; the UberDOG allows 200
constexpr int kMaxTaskHoods = 4;
constexpr uint8_t kFlagInProgress = 1;

constexpr int kActNone = 0, kActPlayground = 1, kActStreet = 2, kActBossLobby = 3, kActBuilding = 4;
constexpr int kOpCreate = 0, kOpJoin = 1, kOpLeave = 2;
constexpr int kOk = 0, kErrBadArgs = 1, kErrInGroup = 2, kErrNotFound = 3, kErrFull = 4, kErrRate = 5,
              kErrNotInGroup = 6;

constexpr int kGoNone = 0, kGoFriend = 1, kGoHood = 2, kGoHoodAfterShard = 3, kGoTell = 4;

constexpr uint8_t kCmdCreate = 1, kCmdJoin = 2, kCmdLeave = 3;

constexpr int kDefaultMaxSize[3] = {8, 4, 4};
constexpr int kMaxTarget[3] = {3, 3, 5};
constexpr uint32_t kTaskHoods[6] = {2000, 1000, 3000, 4000, 5000, 9000};
constexpr uint32_t kHqHood[4] = {11000, 12000, 13000, 10000}; // Sellbot, Cashbot, Lawbot, Bossbot

inline constexpr const char* kPageTitle = "Groups";
inline constexpr const char* kKindShort[3] = {"Boss", "Building", "Task"};
inline constexpr const char* kKindNames[3] = {"Boss Battle", "Cog Building", "Toon Task"};
inline constexpr const char* kDepartments[4] = {"Sellbot", "Cashbot", "Lawbot", "Bossbot"};
inline constexpr const char* kTaskNames[6] = {"Toontown Central", "Donald's Dock", "The Brrrgh",
                                              "Minnie's Melodyland", "Daisy Gardens", "Donald's Dreamland"};
inline constexpr const char* kEmpty = "No groups yet. Start one with Boss, Building or Task.";
inline constexpr const char* kConnecting = "Looking for groups.";
inline constexpr const char* kUnavailable = "Groups are not available right now.";
inline constexpr const char* kJoined = "You joined the group!";
inline constexpr const char* kCreated = "Your group is open!";
inline constexpr const char* kLeft = "You left the group.";
inline constexpr const char* kAlreadyHere = "You are already here!";
inline constexpr const char* kMine = "Your group";
inline constexpr const char* kInProgress = "Started";
inline constexpr const char* kJoin = "Join";
inline constexpr const char* kLeave = "Leave";
inline constexpr const char* kFull = "Full";
inline constexpr const char* kBack = "Back";
inline constexpr const char* kCreateTitle = "Start a group";
inline constexpr const char* kPick = "Pick one";
inline constexpr const char* kErrors[7] = {"",
                                           "Something went wrong. Try again.",
                                           "You are already in a group.",
                                           "That group is gone.",
                                           "That group is full.",
                                           "Please wait a moment.",
                                           "You are not in a group."};

struct GroupView {
    uint32_t id = 0;
    uint32_t leader = 0;
    uint32_t hood = 0;
    uint32_t shard = 0;
    uint8_t kind = 0;
    uint8_t target = 0;
    uint8_t maxSize = 0;
    uint8_t flags = 0;
    uint8_t memberCount = 0;
    uint32_t members[kMaxMembers] = {};
    char leaderName[24] = {};

    bool full() const { return memberCount >= maxSize; }
    bool has(uint32_t av) const {
        for (int i = 0; i < memberCount; i++)
            if (members[i] == av) return true;
        return false;
    }
};

// ---- 320x240 sheet (book content under the 24px header; the picker covers the tab bar) ----
constexpr float kScreenW = 320.0f;
constexpr float kScreenH = 240.0f;
constexpr float kHeaderH = 24.0f;
constexpr float kTabBarY = 212.0f;
constexpr float kFilterX = 8.0f;
constexpr float kFilterY = 46.0f;
constexpr float kFilterW = 98.0f;
constexpr float kFilterH = 48.0f;
constexpr float kFilterGap = 6.0f;
constexpr float kStatusY = 28.0f;
constexpr float kRowX = 8.0f;
constexpr float kRowY = 100.0f;
constexpr float kRowW = 304.0f;
constexpr float kRowH = 104.0f;
constexpr float kRowBtnX = 16.0f;
constexpr float kRowBtnY = 150.0f;
constexpr float kRowBtnW = 288.0f;
constexpr float kRowBtnH = 48.0f;
constexpr float kPickTitleY = 28.0f;
constexpr float kPickCellX = 8.0f;
constexpr float kPickCellY = 46.0f;
constexpr float kPickCellW = 148.0f;
constexpr float kPickCellH = 44.0f;
constexpr float kPickCellGapX = 8.0f;
constexpr float kPickCellGapY = 4.0f;
constexpr float kPickBackX = 80.0f;
constexpr float kPickBackY = 190.0f;
constexpr float kPickBackW = 160.0f;
constexpr float kPickBackH = 44.0f;

static_assert(kFilterX + 3 * kFilterW + 2 * kFilterGap <= kScreenW, "filters fit");
static_assert(kFilterH >= 48.0f && kRowBtnH >= 48.0f, "join and create meet 48px");
static_assert(kRowY + kRowH <= kTabBarY, "the row stays above the tabs");
static_assert(kRowBtnY + kRowBtnH <= kRowY + kRowH, "the row button sits in the card");
static_assert(kPickBackY + kPickBackH <= kScreenH, "Back stays on the 240px screen");
static_assert(kPickCellY + 3 * kPickCellH + 2 * kPickCellGapY <= kPickBackY, "six targets clear Back");

inline int pickCount(int kind) { return kind == kTask ? 6 : 4; }

inline void pickCell(int index, float* x, float* y) {
    const int col = index % 2;
    const int row = index / 2;
    *x = kPickCellX + col * (kPickCellW + kPickCellGapX);
    *y = kPickCellY + row * (kPickCellH + kPickCellGapY);
}

// ---- place / activity (context.py, the part the client reports) ----
inline uint32_t canonicalZone(uint32_t zone) {
    if (zone == 0) return 2000;
    if (zone >= 22000 && zone < 61000) {
        zone = zone % 2000;
        if (zone < 1000) return zone + 2000;
        return zone - 1000 + 8000;
    }
    return zone;
}

inline bool publishHood(uint32_t hood) {
    switch (hood) {
    case 1000:
    case 2000:
    case 3000:
    case 4000:
    case 5000:
    case 6000:
    case 8000:
    case 9000:
    case 17000:
    case 10000:
    case 11000:
    case 12000:
    case 13000:
        return true;
    default:
        return false;
    }
}

inline uint32_t hoodOfZone(uint32_t zone) {
    zone = canonicalZone(zone);
    if (zone == 0) return 0;
    const uint32_t hood = zone - (zone % 1000);
    return publishHood(hood) ? hood : 0;
}

// Hoods of the Toon's current task targets. zones are NPCToons zones (quest toNpc).
// Unknown, HQ and non-playground zones are dropped. The result is unique, sorted,
// and capped at kMaxTaskHoods, which is what Android's clean_task_hoods keeps.
inline int taskHoodsFromNpcZones(const int32_t* zones, int count, uint32_t* out, int cap) {
    uint32_t found[8];
    int n = 0;
    if (count > 8) count = 8;
    for (int i = 0; i < count; i++) {
        if (zones[i] <= 0) continue;
        const uint32_t hood = hoodOfZone(uint32_t(zones[i]));
        bool task = false;
        for (uint32_t h : kTaskHoods)
            if (h == hood) task = true;
        if (!task) continue;
        bool seen = false;
        for (int j = 0; j < n; j++)
            if (found[j] == hood) seen = true;
        if (seen || n >= 8) continue;
        found[n++] = hood;
    }
    for (int i = 1; i < n; i++) {
        const uint32_t key = found[i];
        int j = i;
        while (j > 0 && found[j - 1] > key) {
            found[j] = found[j - 1];
            j--;
        }
        found[j] = key;
    }
    if (cap > kMaxTaskHoods) cap = kMaxTaskHoods;
    if (n > cap) n = cap;
    for (int i = 0; i < n; i++) out[i] = found[i];
    return n;
}

inline void writeTaskHoods(net::DatagramWriter& w, const uint32_t* hoods, int n) {
    if (n < 0) n = 0;
    if (n > kMaxTaskHoods) n = kMaxTaskHoods;
    w.u16(uint16_t(n * 4));
    for (int i = 0; i < n; i++) w.u32(hoods[i]);
}

inline int activityFor(uint32_t zone) {
    const uint32_t hood = hoodOfZone(zone);
    if (hood == 10000 || hood == 11000 || hood == 12000 || hood == 13000) return kActBossLobby;
    if (hood == 0) return kActNone;
    const uint32_t sub = canonicalZone(zone) % 1000;
    if (sub >= 500) return kActBuilding;
    if (sub >= 100) return kActStreet;
    return kActPlayground;
}

inline const char* hoodName(uint32_t hood) {
    switch (hood) {
    case 2000: return "Toontown Central";
    case 1000: return "Donald's Dock";
    case 3000: return "The Brrrgh";
    case 4000: return "Minnie's Melodyland";
    case 5000: return "Daisy Gardens";
    case 9000: return "Donald's Dreamland";
    case 6000: return "Acorn Acres";
    case 8000: return "Goofy Speedway";
    case 17000: return "Goofy Speedway";
    case 11000: return "Sellbot HQ";
    case 12000: return "Cashbot HQ";
    case 13000: return "Lawbot HQ";
    case 10000: return "Bossbot HQ";
    default: return "a playground";
    }
}

inline const char* groupTitle(const GroupView& g, char* buf, int cap) {
    if (g.kind == kTask) {
        std::snprintf(buf, cap, "%s", kKindNames[kTask]);
        return buf;
    }
    const char* dept = g.target < 4 ? kDepartments[g.target] : "Cog";
    const char* kind = g.kind < 3 ? kKindNames[g.kind] : "Group";
    std::snprintf(buf, cap, "%s %s", dept, kind);
    return buf;
}

// Player names are drawn as citro2d text. Strip Panda text-property markup
// (\1...\1 and \2), other controls, and <tag> / & so a remote name cannot
// change colour, add a line, or look like UI markup. Letters, spaces and the
// NameCheck punctuation (.,'-) are kept. Returns the number of bytes written,
// not counting the terminator.
inline int copyPlainName(char* dst, int cap, const char* src) {
    if (!dst || cap <= 0) return 0;
    int n = 0;
    if (!src) {
        dst[0] = 0;
        return 0;
    }
    const auto* p = reinterpret_cast<const unsigned char*>(src);
    while (*p && n + 1 < cap) {
        if (*p == 0x01) {
            ++p;
            while (*p && *p != 0x01) ++p;
            if (*p == 0x01) ++p;
            continue;
        }
        if (*p == '\n' || *p == '\r' || *p == '\t') {
            if (n > 0 && dst[n - 1] != ' ') dst[n++] = ' ';
            ++p;
            continue;
        }
        if (*p < 0x20 || *p == 0x7F || *p == 0x02) {
            ++p;
            continue;
        }
        if (*p == '<') {
            const unsigned char* q = p + 1;
            int span = 0;
            while (*q && *q != '>' && span < 32) {
                ++q;
                ++span;
            }
            if (*q == '>') {
                p = q + 1;
                continue;
            }
            ++p;
            continue;
        }
        if (*p == '>' || *p == '&') {
            ++p;
            continue;
        }
        dst[n++] = static_cast<char>(*p++);
    }
    dst[n] = 0;
    return n;
}

inline const char* placeName(const GroupView& g) {
    if (g.kind == kTask) return g.target < 6 ? kTaskNames[g.target] : "a playground";
    return hoodName(g.hood);
}

inline bool matches(const GroupView& g, int activity, uint32_t hood, const uint32_t* taskHoods, int taskCount,
                    uint32_t selfId) {
    if (g.has(selfId) || g.full() || (g.flags & kFlagInProgress)) return false;
    if (g.kind == kBoss) return activity == kActBossLobby;
    if (g.kind == kBuilding) return (activity == kActStreet || activity == kActBuilding) && g.hood == hood;
    if (g.kind == kTask) {
        for (int i = 0; i < taskCount; i++)
            if (taskHoods[i] == g.hood) return true;
        return false;
    }
    return false;
}

inline int teleportPlan(const GroupView& g, uint32_t myShard, uint32_t myHood, bool leaderIsFriend, bool hoodAllowed) {
    if (g.shard == myShard && g.hood == myHood) return kGoNone;
    if (leaderIsFriend && g.shard == myShard) return kGoFriend;
    if (hoodAllowed) return g.shard != myShard ? kGoHoodAfterShard : kGoHood;
    return kGoTell;
}

inline int moveFocus(int index, int count, int step) {
    if (count <= 0) return -1;
    if (index < 0) return step > 0 ? 0 : count - 1;
    const int next = index + step;
    if (next < 0) return 0;
    if (next >= count) return count - 1;
    return next;
}

// ---- datagrams (u16 byte length in front of every variable array, as Panda packs them) ----
inline bool readGroup(net::DatagramReader& r, GroupView& g) {
    g = GroupView{};
    g.id = r.u32();
    g.leader = r.u32();
    g.kind = r.u8();
    g.target = r.u8();
    g.hood = r.u32();
    g.shard = r.u32();
    g.maxSize = r.u8();
    g.flags = r.u8();
    const uint16_t bytes = r.u16();
    if (!r.ok() || (bytes % 4) != 0 || bytes > kMaxMembers * 4) {
        r.fail();
        return false;
    }
    g.memberCount = uint8_t(bytes / 4);
    for (int i = 0; i < g.memberCount; i++) g.members[i] = r.u32();
    return r.ok();
}

// Reads the whole array. Stores at most cap groups and still consumes every byte.
inline int readGroupArray(net::DatagramReader& r, GroupView* out, int cap) {
    const uint16_t bytes = r.u16();
    const uint32_t start = r.pos();
    const uint32_t end = start + bytes;
    int n = 0;
    int stored = 0;
    while (r.ok() && r.pos() < end) {
        GroupView g;
        if (!readGroup(r, g)) return -1;
        if (stored < cap) out[stored++] = g;
        n++;
        if (n > 400) {
            r.fail();
            return -1;
        }
    }
    if (!r.ok() || r.pos() != end) {
        r.fail();
        return -1;
    }
    return stored;
}

inline void writeGroup(net::DatagramWriter& w, const GroupView& g) {
    w.u32(g.id);
    w.u32(g.leader);
    w.u8(g.kind);
    w.u8(g.target);
    w.u32(g.hood);
    w.u32(g.shard);
    w.u8(g.maxSize);
    w.u8(g.flags);
    w.u16(uint16_t(g.memberCount * 4));
    for (int i = 0; i < g.memberCount; i++) w.u32(g.members[i]);
}

// STATESERVER_OBJECT_SET_FIELD avatarGone, the post-remove body. Not sent by this client.
constexpr uint16_t kSetField = 2020;
constexpr uint16_t kAddPostRemove = 1110;

inline int avatarGoneBytes(uint8_t* dst, int cap, uint64_t clientChannel, uint32_t avId, uint32_t nonce) {
    net::DatagramWriter w(dst, uint32_t(cap));
    w.u8(1);
    w.u64(kManagerDoId); // channel: the manager
    w.u64(clientChannel);
    w.u16(kSetField);
    w.u32(kManagerDoId);
    w.u16(kAvatarGone);
    w.u32(avId);
    w.u32(nonce);
    return w.ok() ? int(w.size()) : -1;
}

struct GroupsState {
    uint32_t managerId = 0;
    bool watchWanted = false;
    bool subscribed = false;
    bool hasBoard = false;
    uint32_t version = 0;
    uint32_t selfId = 0;
    uint32_t revision = 0;
    int count = 0;
    GroupView groups[kMaxGroups]{};
    char status[96] = {};
    bool showEmpty = false;
    // Join travel. The main loop consumes goZone through groupsTakeTravel.
    // awaitFriend is a teleportQuery still waiting on teleportResponse.
    // hoodFallback is the playground used when that query cannot complete.
    uint32_t goZone = 0;
    bool goFromMap = false;
    uint32_t awaitFriend = 0;
    uint32_t hoodFallback = 0;

    void touch() { revision++; }

    int indexOf(uint32_t id) const {
        for (int i = 0; i < count; i++)
            if (groups[i].id == id) return i;
        return -1;
    }

    void upsert(const GroupView& g) {
        const int at = indexOf(g.id);
        if (at >= 0) groups[at] = g;
        else if (count < kMaxGroups) groups[count++] = g;
        hasBoard = true;
    }

    void remove(uint32_t id) {
        const int at = indexOf(id);
        if (at < 0) return;
        groups[at] = groups[count - 1];
        count--;
    }

    // Display order: our group, then open groups, then full ones, then id.
    int rows(int* idx) const {
        for (int i = 0; i < count; i++) idx[i] = i;
        for (int i = 1; i < count; i++) {
            const int key = idx[i];
            int j = i;
            while (j > 0 && before(groups[key], groups[idx[j - 1]])) {
                idx[j] = idx[j - 1];
                j--;
            }
            idx[j] = key;
        }
        return count;
    }

    const GroupView* own() const {
        for (int i = 0; i < count; i++)
            if (groups[i].has(selfId)) return &groups[i];
        return nullptr;
    }

    bool applyBoard(uint32_t ver, const GroupView* src, int n) {
        count = n < kMaxGroups ? n : kMaxGroups;
        for (int i = 0; i < count; i++) groups[i] = src[i];
        version = ver;
        hasBoard = true;
        touch();
        return true;
    }

    // False when a delta was missed (the caller asks for the board again).
    bool applyChanged(uint32_t ver, const GroupView& g) {
        const bool ok = ver <= version + 1;
        upsert(g);
        if (ver > version) version = ver;
        touch();
        return ok;
    }

    bool applyGone(uint32_t ver, uint32_t id) {
        const bool ok = ver <= version + 1;
        remove(id);
        if (ver > version) version = ver;
        hasBoard = true;
        touch();
        return ok;
    }

private:
    bool before(const GroupView& a, const GroupView& b) const {
        const int aOwn = a.has(selfId) ? 0 : 1;
        const int bOwn = b.has(selfId) ? 0 : 1;
        if (aOwn != bOwn) return aOwn < bOwn;
        if (a.full() != b.full()) return !a.full();
        return a.id < b.id;
    }
};

// Astron never sends ENTER for an uberdog. Once the session is logged in or in a
// zone, the client adopts DoId 4722 locally, the same way the Android client calls
// generateGlobalObject. This does not send anything. A session that is not live
// leaves the id at 0, so the page stays on the unavailable line.
inline bool adoptGlobalManager(GroupsState& g, bool sessionLive) {
    if (!sessionLive || g.managerId != 0) return false;
    g.managerId = kManagerDoId;
    g.touch();
    return true;
}

inline void listMessage(const GroupsState& g, const char** status, bool* empty) {
    if (g.managerId == 0) {
        *status = kUnavailable;
        *empty = false;
        return;
    }
    if (!g.hasBoard) {
        *status = kConnecting;
        *empty = false;
        return;
    }
    if (g.status[0]) {
        *status = g.status;
        *empty = false;
        return;
    }
    *status = "";
    *empty = g.count == 0;
}

} // namespace tt::groups
