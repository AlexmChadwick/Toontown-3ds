// Groups wire (docs/design/groups.md). Field numbers are otp+toon+platform+groups.
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "net/client.h"
#include "net/msg_types.h"
#include "quest/quest_text.h"

namespace tt::net {

namespace {

#define TT_GROUPS_SEND(field)                                                                                          \
    static_assert((groups::kw##field & groups::kKwClsend) != 0, "groups " #field " must be clsend");                   \
    static_assert((groups::kw##field & dc::kKwClsend) != 0, "groups keyword bit")

bool canSend(const Client& c, const groups::GroupsState& g) {
    return g.managerId != 0 && c.inZone();
}

// Same gate the Map page uses: a publishable hood, and the teleport-access bit
// once setTeleportAccess has arrived. Offline (no field yet) does not gate.
bool hoodAllowed(const OwnStats& stats, uint32_t hood) {
    if (!groups::publishHood(hood)) return false;
    const unsigned bit = hood / 1000u;
    if (bit >= 32) return false;
    if (stats.hasTeleportAccess && (stats.teleportAccess & (1u << bit)) == 0) return false;
    return true;
}

int taskHoodsOf(const OwnQuests& quests, uint32_t* out, int cap) {
    int32_t zones[kMaxQuests];
    int n = 0;
    const int count = quests.count < kMaxQuests ? quests.count : kMaxQuests;
    for (int i = 0; i < count; i++) {
        const uint32_t to = quests.quests[i].toNpc;
        if (to == 0 || to > 0xFFFF) continue;
        const tt::quest::NpcDef* npc = tt::quest::findNpc(static_cast<uint16_t>(to));
        if (!npc || npc->zone <= 0) continue;
        zones[n++] = npc->zone;
    }
    return groups::taskHoodsFromNpcZones(zones, n, out, cap);
}

void sayGo(groups::GroupsState& g, const groups::GroupView& view) {
    std::snprintf(g.status, sizeof(g.status), "Go to %s to meet your group.", groups::placeName(view));
}

} // namespace

void Client::groupsReset() {
    const bool want = groups_.watchWanted;
    const uint32_t self = avatarId_;
    groups_ = groups::GroupsState{};
    groups_.watchWanted = want;
    groups_.selfId = self;
}

void Client::groupsName(groups::GroupView& g) const {
    const char* name = avatarName(g.leader);
    if (!name || !name[0] || g.leader == avatarId_) {
        g.leaderName[0] = 0;
        return;
    }
    std::snprintf(g.leaderName, sizeof(g.leaderName), "%s", name);
}

void Client::groupsSendContext() {
    if (!canSend(*this, groups_)) return;
    const uint32_t zone = zoneId();
    const uint32_t hood = groups::hoodOfZone(zone);
    const int activity = groups::activityFor(zone);
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_GROUPS_SEND(SetContext);
    beginUpdate(w, groups_.managerId, groups::kSetContext);
    w.u32(hood);
    w.u32(shardId_);
    w.u8(uint8_t(activity));
    uint32_t taskHoods[groups::kMaxTaskHoods];
    const int n = taskHoodsOf(ownQuests_, taskHoods, groups::kMaxTaskHoods);
    groups::writeTaskHoods(w, taskHoods, n);
    finish(w);
}

void Client::groupsSendBoardRequest() {
    if (!canSend(*this, groups_)) return;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_GROUPS_SEND(RequestBoard);
    beginUpdate(w, groups_.managerId, groups::kRequestBoard);
    finish(w);
    groups_.subscribed = true;
}

void Client::groupsNoteGenerate(uint32_t doId) {
    if (doId != groups::kManagerDoId) return;
    groups_.managerId = doId;
    groups_.selfId = avatarId_;
    groups_.touch();
    TT_LOG("groups: manager %u", unsigned(doId));
    if (groups_.watchWanted && !groups_.subscribed) {
        groupsSendContext();
        groupsSendBoardRequest();
    }
}

void Client::groupsWatch(bool on) {
    groups_.watchWanted = on;
    groups_.selfId = avatarId_;
    if (!on) {
        if (groups_.subscribed && canSend(*this, groups_)) {
            DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
            TT_GROUPS_SEND(StopBoard);
            beginUpdate(w, groups_.managerId, groups::kStopBoard);
            finish(w);
        }
        groups_.subscribed = false;
        return;
    }
    if (groups_.managerId == 0 || groups_.subscribed) return;
    groupsSendContext();
    groupsSendBoardRequest();
}

void Client::groupsCommand(uint8_t cmd, uint8_t kind, uint8_t target, uint32_t id) {
    if (!canSend(*this, groups_)) return;
    groups_.selfId = avatarId_;
    if (cmd == groups::kCmdCreate) {
        if (kind > 2) return;
        const uint8_t size = uint8_t(groups::kDefaultMaxSize[kind]);
        groupsSendContext();
        DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
        TT_GROUPS_SEND(CreateGroup);
        beginUpdate(w, groups_.managerId, groups::kCreateGroup);
        w.u8(kind);
        w.u8(target);
        w.u8(size);
        finish(w);
        TT_LOG("groups: create kind %u target %u", unsigned(kind), unsigned(target));
        return;
    }
    if (cmd == groups::kCmdJoin) {
        DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
        TT_GROUPS_SEND(JoinGroup);
        beginUpdate(w, groups_.managerId, groups::kJoinGroup);
        w.u32(id);
        finish(w);
        TT_LOG("groups: join %u", unsigned(id));
        return;
    }
    if (cmd == groups::kCmdLeave) {
        DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
        TT_GROUPS_SEND(LeaveGroup);
        beginUpdate(w, groups_.managerId, groups::kLeaveGroup);
        finish(w);
        TT_LOG("groups: leave");
    }
}

void Client::groupsResult(uint8_t op, uint8_t code, uint32_t groupId) {
    groups_.status[0] = 0;
    if (code != groups::kOk) {
        const char* err = code < 7 ? groups::kErrors[code] : groups::kErrors[groups::kErrBadArgs];
        std::snprintf(groups_.status, sizeof(groups_.status), "%s", err);
    } else if (op == groups::kOpCreate) {
        std::snprintf(groups_.status, sizeof(groups_.status), "%s", groups::kCreated);
    } else if (op == groups::kOpLeave) {
        std::snprintf(groups_.status, sizeof(groups_.status), "%s", groups::kLeft);
    } else if (op == groups::kOpJoin) {
        groups_.goZone = 0;
        groups_.goFromMap = false;
        groups_.awaitFriend = 0;
        groups_.hoodFallback = 0;
        const groups::GroupView* g = nullptr;
        for (int i = 0; i < groups_.count; i++)
            if (groups_.groups[i].id == groupId) g = &groups_.groups[i];
        if (!g) {
            std::snprintf(groups_.status, sizeof(groups_.status), "%s", groups::kJoined);
        } else {
            const uint32_t hood = groups::hoodOfZone(zoneId());
            const bool allowed = hoodAllowed(ownStats_, g->hood);
            const int plan = groups::teleportPlan(*g, shardId_, hood, isFriend(g->leader), allowed);
            if (plan == groups::kGoNone) {
                std::snprintf(groups_.status, sizeof(groups_.status), "%s", groups::kAlreadyHere);
            } else if (plan == groups::kGoFriend) {
                // Android calls teleportQuery. This client can send it only to a
                // toon already in interest. Otherwise the Map teleport to the
                // group's hood is the path that actually moves us.
                std::snprintf(groups_.status, sizeof(groups_.status), "%s", groups::kJoined);
                if (sendTeleportQuery(g->leader)) {
                    groups_.awaitFriend = g->leader;
                    if (allowed) groups_.hoodFallback = g->hood;
                } else if (allowed) {
                    groups_.goZone = g->hood;
                    groups_.goFromMap = true;
                } else {
                    sayGo(groups_, *g);
                }
            } else if (plan == groups::kGoHood) {
                std::snprintf(groups_.status, sizeof(groups_.status), "%s", groups::kJoined);
                groups_.goZone = g->hood;
                groups_.goFromMap = true;
            } else {
                // kGoHoodAfterShard needs a district change this client does not
                // have (the district list is display only). kGoTell is Android's
                // "go to this place" line. Both stay a status line.
                sayGo(groups_, *g);
            }
        }
    }
    groups_.touch();
    TT_LOG("groups: result op %u code %u group %u", unsigned(op), unsigned(code), unsigned(groupId));
}

bool Client::groupsTakeTravel(uint32_t& zone, bool& fromMap) {
    if (!groups_.goZone) return false;
    zone = groups_.goZone;
    fromMap = groups_.goFromMap;
    groups_.goZone = 0;
    groups_.goFromMap = false;
    return true;
}

void Client::groupsOnTeleportResult() {
    if (!groups_.awaitFriend) return;
    const TeleportResult& t = friends_.teleport;
    if (!t.valid || t.avId != groups_.awaitFriend) return;
    const uint32_t fallback = groups_.hoodFallback;
    groups_.awaitFriend = 0;
    groups_.hoodFallback = 0;
    if (t.available == 1 && t.shardId == shardId_ && t.zoneId != 0) {
        groups_.goZone = t.zoneId;
        groups_.goFromMap = false;
        return;
    }
    if (fallback != 0 && t.shardId == shardId_) {
        groups_.goZone = fallback;
        groups_.goFromMap = true;
        return;
    }
    for (int i = 0; i < groups_.count; i++) {
        if (groups_.groups[i].leader == t.avId || groups_.groups[i].has(groups_.selfId)) {
            sayGo(groups_, groups_.groups[i]);
            groups_.touch();
            return;
        }
    }
}

bool Client::groupsHandleSetField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    if (doId != groups::kManagerDoId && doId != groups_.managerId) return false;
    if (groups_.managerId == 0) groupsNoteGenerate(groups::kManagerDoId);
    groups_.selfId = avatarId_;
    if (fieldId == groups::kSetBoard) {
        const uint32_t ver = r.u32();
        groups::GroupView tmp[groups::kMaxGroups];
        const int n = groups::readGroupArray(r, tmp, groups::kMaxGroups);
        if (n < 0 || !r.ok()) return true;
        for (int i = 0; i < n; i++) groupsName(tmp[i]);
        groups_.applyBoard(ver, tmp, n);
        TT_LOG("groups: board v%u count %d", unsigned(ver), n);
        return true;
    }
    if (fieldId == groups::kGroupChanged) {
        const uint32_t ver = r.u32();
        groups::GroupView g;
        if (!groups::readGroup(r, g) || !r.ok()) return true;
        groupsName(g);
        if (!groups_.applyChanged(ver, g) && groups_.watchWanted) groupsSendBoardRequest();
        return true;
    }
    if (fieldId == groups::kGroupGone) {
        const uint32_t ver = r.u32();
        const uint32_t id = r.u32();
        if (!r.ok()) return true;
        if (!groups_.applyGone(ver, id) && groups_.watchWanted) groupsSendBoardRequest();
        return true;
    }
    if (fieldId == groups::kResult) {
        const uint8_t op = r.u8();
        const uint8_t code = r.u8();
        const uint32_t id = r.u32();
        if (r.ok()) groupsResult(op, code, id);
        return true;
    }
    // The manager's other fields are not for the client. Swallow the dispatch so a
    // different object handler cannot read this doId's bytes.
    return true;
}

#undef TT_GROUPS_SEND

} // namespace tt::net
