// Host checks for the Groups client cache and the 320x240 sheet.
// The Python suite in this directory covers the UberDOG board. This file covers
// the C++ that ships on the 3DS: datagram layout, row order, and the rectangles.
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "net/groups_logic.h"

namespace {

int gFailures = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            ++gFailures;                                                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                               \
        }                                                                                                              \
    } while (0)

void checkWire() {
    CHECK(tt::groups::kAvatarGone == 2294);
    CHECK(tt::groups::kRequestBoard == 2283);
    CHECK(tt::groups::kAvatarGone == tt::groups::kFieldBase + 11);
    uint8_t buf[64];
    const int n = tt::groups::avatarGoneBytes(buf, int(sizeof(buf)), 0x100000004ull, 4, 11);
    CHECK(n == 33);
    static const uint8_t kExpect[] = {0x01, 0x72, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
                                      0x01, 0x00, 0x00, 0x00, 0xe4, 0x07, 0x72, 0x12, 0x00, 0x00, 0xf6, 0x08, 0x04,
                                      0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00};
    CHECK(n == int(sizeof(kExpect)));
    if (n == int(sizeof(kExpect))) CHECK(std::memcmp(buf, kExpect, sizeof(kExpect)) == 0);
}

tt::groups::GroupView sample(uint32_t id, uint32_t leader, int kind, uint32_t hood, int members, int maxSize) {
    tt::groups::GroupView g;
    g.id = id;
    g.leader = leader;
    g.kind = uint8_t(kind);
    g.hood = hood;
    g.shard = 3;
    g.maxSize = uint8_t(maxSize);
    g.memberCount = uint8_t(members);
    for (int i = 0; i < members; i++) g.members[i] = leader + uint32_t(i);
    return g;
}

void checkRoundTrip() {
    tt::groups::GroupView g = sample(9, 40, tt::groups::kBuilding, 2000, 2, 4);
    g.flags = tt::groups::kFlagInProgress;
    uint8_t buf[128];
    tt::net::DatagramWriter w(buf, sizeof(buf));
    w.u16(0);
    const uint32_t at = 0;
    tt::groups::writeGroup(w, g);
    const uint16_t bytes = uint16_t(w.size() - 2);
    buf[0] = uint8_t(bytes);
    buf[1] = uint8_t(bytes >> 8);
    (void)at;
    tt::net::DatagramReader r(buf, w.size());
    tt::groups::GroupView out[4];
    const int n = tt::groups::readGroupArray(r, out, 4);
    CHECK(n == 1);
    CHECK(out[0].id == 9 && out[0].memberCount == 2 && out[0].members[1] == 41);
    CHECK(out[0].flags == tt::groups::kFlagInProgress);
    CHECK(r.ok());
}

void checkCache() {
    tt::groups::GroupsState s;
    s.managerId = tt::groups::kManagerDoId;
    s.selfId = 4;
    tt::groups::GroupView full = sample(1, 1, tt::groups::kBoss, 11000, 2, 2);
    tt::groups::GroupView open = sample(2, 3, tt::groups::kTask, 2000, 1, 4);
    tt::groups::GroupView mine = sample(3, 4, tt::groups::kBuilding, 2000, 1, 4);
    CHECK(s.applyBoard(1, &full, 1));
    CHECK(s.applyChanged(2, open));
    CHECK(s.applyChanged(4, mine) == false); // version jumped over 3
    int idx[8];
    CHECK(s.rows(idx) == 3);
    CHECK(s.groups[idx[0]].id == 3); // ours first
    CHECK(s.groups[idx[1]].id == 2); // open before full
    CHECK(s.groups[idx[2]].id == 1);
    CHECK(s.own() && s.own()->id == 3);
    const char* status = nullptr;
    bool empty = true;
    tt::groups::listMessage(s, &status, &empty);
    CHECK(!empty && status && status[0] == 0);
    s.status[0] = 'x';
    tt::groups::listMessage(s, &status, &empty);
    CHECK(!empty && status[0] == 'x');
    s.status[0] = 0;
    s.count = 0;
    tt::groups::listMessage(s, &status, &empty);
    CHECK(empty);
    s.hasBoard = false;
    tt::groups::listMessage(s, &status, &empty);
    CHECK(!empty && std::strcmp(status, tt::groups::kConnecting) == 0);
    s.managerId = 0;
    tt::groups::listMessage(s, &status, &empty);
    CHECK(std::strcmp(status, tt::groups::kUnavailable) == 0 && !empty);
}

void checkRules() {
    tt::groups::GroupView boss = sample(1, 50, tt::groups::kBoss, 11000, 1, 4);
    uint32_t tasks[] = {2000};
    CHECK(tt::groups::matches(boss, tt::groups::kActBossLobby, 11000, tasks, 0, 9));
    CHECK(!tt::groups::matches(boss, tt::groups::kActStreet, 2000, tasks, 0, 9));
    CHECK(!tt::groups::matches(boss, tt::groups::kActBossLobby, 11000, tasks, 0, 50));
    CHECK(tt::groups::hoodOfZone(2100) == 2000);
    CHECK(tt::groups::hoodOfZone(61050) == 0);
    CHECK(tt::groups::hoodOfZone(17000) == 17000);
    CHECK(tt::groups::activityFor(11000) == tt::groups::kActBossLobby);
    CHECK(tt::groups::activityFor(2500) == tt::groups::kActBuilding);
    CHECK(tt::groups::activityFor(2100) == tt::groups::kActStreet);
    CHECK(tt::groups::activityFor(2000) == tt::groups::kActPlayground);
    tt::groups::GroupView here = sample(1, 50, tt::groups::kTask, 2000, 1, 4);
    here.shard = 3;
    CHECK(tt::groups::teleportPlan(here, 3, 2000, false, true) == tt::groups::kGoNone);
    CHECK(tt::groups::teleportPlan(here, 3, 1000, true, true) == tt::groups::kGoFriend);
    CHECK(tt::groups::teleportPlan(here, 9, 2000, false, true) == tt::groups::kGoHoodAfterShard);
    CHECK(tt::groups::teleportPlan(here, 3, 1000, false, false) == tt::groups::kGoTell);
    int32_t zones[] = {2100, 1100, 2105, 0, 7000, 1000, -1};
    uint32_t hoods[4];
    const int nHoods = tt::groups::taskHoodsFromNpcZones(zones, 7, hoods, 4);
    CHECK(nHoods == 2);
    CHECK(hoods[0] == 1000 && hoods[1] == 2000);
    uint8_t packed[32];
    tt::net::DatagramWriter hw(packed, sizeof(packed));
    tt::groups::writeTaskHoods(hw, hoods, nHoods);
    CHECK(hw.size() == 2 + 8);
    CHECK(packed[0] == 8 && packed[1] == 0);
    CHECK(tt::groups::moveFocus(-1, 3, 1) == 0);
    CHECK(tt::groups::moveFocus(2, 3, 1) == 2);
    CHECK(tt::groups::kFilterH >= 48.0f && tt::groups::kRowBtnH >= 48.0f);
    CHECK(tt::groups::kPickBackY + tt::groups::kPickBackH <= 240.0f);
    float x = 0, y = 0;
    tt::groups::pickCell(5, &x, &y);
    CHECK(y + tt::groups::kPickCellH <= tt::groups::kPickBackY);
    CHECK(std::strstr(tt::groups::kEmpty, "press Create") == nullptr);
    CHECK(std::strstr(tt::groups::kEmpty, "Boss") != nullptr);
    CHECK(std::strstr(tt::groups::kEmpty, "Building") != nullptr);
    CHECK(std::strstr(tt::groups::kEmpty, "Task") != nullptr);
}

void checkPlainName() {
    char leader[24];
    char member[24];
    const char* markedLeader = "\x01red\x01Professor Pete\x02";
    const char* markedMember = "Cool\n<b>Cat</b>&";
    CHECK(tt::groups::copyPlainName(leader, int(sizeof(leader)), markedLeader) > 0);
    CHECK(std::strcmp(leader, "Professor Pete") == 0);
    CHECK(tt::groups::copyPlainName(member, int(sizeof(member)), markedMember) > 0);
    CHECK(std::strcmp(member, "Cool Cat") == 0);
    CHECK(tt::groups::copyPlainName(leader, int(sizeof(leader)), "Ladybug") == 7);
    CHECK(std::strcmp(leader, "Ladybug") == 0);
    CHECK(tt::groups::copyPlainName(leader, int(sizeof(leader)), "\x01red") == 0);
    CHECK(leader[0] == 0);
    CHECK(tt::groups::copyPlainName(leader, int(sizeof(leader)), nullptr) == 0);
    char again[24];
    tt::groups::copyPlainName(again, int(sizeof(again)), member);
    CHECK(std::strcmp(again, member) == 0);
    CHECK(std::strchr(leader, '<') == nullptr);
    CHECK(std::strchr(member, '\n') == nullptr);
}

void checkAdopt() {
    // No ENTER is required. A live session takes DoId 4722. A session that is not
    // logged in leaves the id at 0, and the page stays on the unavailable line.
    tt::groups::GroupsState idle;
    CHECK(!tt::groups::adoptGlobalManager(idle, false));
    CHECK(idle.managerId == 0);
    const char* status = nullptr;
    bool empty = true;
    tt::groups::listMessage(idle, &status, &empty);
    CHECK(std::strcmp(status, tt::groups::kUnavailable) == 0);

    tt::groups::GroupsState live;
    CHECK(tt::groups::adoptGlobalManager(live, true));
    CHECK(live.managerId == tt::groups::kManagerDoId);
    CHECK(tt::groups::kManagerDoId == 4722);
    CHECK(!tt::groups::adoptGlobalManager(live, true));
    CHECK(live.managerId == 4722);
    tt::groups::listMessage(live, &status, &empty);
    CHECK(std::strcmp(status, tt::groups::kConnecting) == 0);

    live.managerId = 99;
    CHECK(!tt::groups::adoptGlobalManager(live, true));
    CHECK(live.managerId == 99);
}

} // namespace

int main() {
    checkWire();
    checkRoundTrip();
    checkCache();
    checkRules();
    checkAdopt();
    checkPlainName();
    std::printf("groups: sizeof(GroupsState)=%zu sizeof(GroupView)=%zu\n", sizeof(tt::groups::GroupsState),
                sizeof(tt::groups::GroupView));
    if (gFailures) {
        std::printf("%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("groups_test ok\n");
    return 0;
}
