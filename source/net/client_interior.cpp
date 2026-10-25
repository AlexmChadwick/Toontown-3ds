#include "net/client.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "net/msg_types.h"

namespace tt::net {

#define TT_NET_UPDATE(w, doId, cls, field) \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " is not clsend"); \
    beginUpdate(w, doId, dc::cls::k_##field)

namespace {

// Internal pickle parser item
enum class PkType : uint8_t {
    Mark,
    Int,
    String,
    List,
    Tuple
};

struct PkItem {
    PkType type = PkType::Mark;
    uint32_t intVal = 0;
    char strVal[64] = {};
    int listId = -1; // index into list store (0 = avIds, 1 = names, 2 = scores)
};

// Simple list container for the 3 lists in the leaderboard tuple
struct PkList {
    int count = 0;
    PkItem items[16] = {};
};

} // namespace

// Stack-based Python Pickle Protocol 0/1/2 decoder for the Toon HQ leaderboard.
// Upstream DistributedHQInteriorAI sends:
//   pickle.dumps(self.air.trophyMgr.getLeaderInfo(), 1)
// which produces a 3-tuple: (avIds: list[int], names: list[str], scores: list[int]).
bool Client::decodeLeaderboardPickle(const uint8_t* data, size_t len, HQLeaderboardState& out) {
    if (!data || len == 0) return false;

    PkItem stack[64];
    int top = 0;

    PkList lists[8];
    int numLists = 0;

    size_t pos = 0;
    bool stopped = false;

    while (pos < len && !stopped) {
        const uint8_t op = data[pos++];
        switch (op) {
        case '(': { // MARK (0x28)
            if (top >= 64) return false;
            stack[top++].type = PkType::Mark;
            break;
        }
        case ']': { // EMPTY_LIST (0x5d)
            if (top >= 64 || numLists >= 8) return false;
            const int lid = numLists++;
            lists[lid].count = 0;
            stack[top].type = PkType::List;
            stack[top].listId = lid;
            top++;
            break;
        }
        case 'a': { // APPEND (0x61)
            if (top < 2) return false;
            const PkItem item = stack[--top];
            if (stack[top - 1].type != PkType::List) return false;
            const int lid = stack[top - 1].listId;
            if (lid >= 0 && lid < numLists && lists[lid].count < 16) {
                lists[lid].items[lists[lid].count++] = item;
            }
            break;
        }
        case 'e': { // APPENDS (0x65)
            // Pop items down to the mark and append them to the list below the mark
            int markPos = top - 1;
            while (markPos >= 0 && stack[markPos].type != PkType::Mark) {
                --markPos;
            }
            if (markPos < 1 || stack[markPos - 1].type != PkType::List) return false;
            const int lid = stack[markPos - 1].listId;
            if (lid >= 0 && lid < numLists) {
                for (int i = markPos + 1; i < top; ++i) {
                    if (lists[lid].count < 16) {
                        lists[lid].items[lists[lid].count++] = stack[i];
                    }
                }
            }
            top = markPos; // Pops items and mark, leaving the list on stack
            break;
        }
        case 'l': { // LIST (0x6c) - create list from mark
            int markPos = top - 1;
            while (markPos >= 0 && stack[markPos].type != PkType::Mark) {
                --markPos;
            }
            if (markPos < 0 || numLists >= 8) return false;
            const int lid = numLists++;
            lists[lid].count = 0;
            for (int i = markPos + 1; i < top; ++i) {
                if (lists[lid].count < 16) {
                    lists[lid].items[lists[lid].count++] = stack[i];
                }
            }
            top = markPos;
            stack[top].type = PkType::List;
            stack[top].listId = lid;
            top++;
            break;
        }
        case 't': { // TUPLE (0x74) - pop down to mark into tuple
            int markPos = top - 1;
            while (markPos >= 0 && stack[markPos].type != PkType::Mark) {
                --markPos;
            }
            if (markPos < 0) return false;
            top = markPos;
            stack[top++].type = PkType::Tuple;
            break;
        }
        case '\x85': { // TUPLE1
            if (top < 1) return false;
            stack[top - 1].type = PkType::Tuple;
            break;
        }
        case '\x86': { // TUPLE2
            if (top < 2) return false;
            --top;
            stack[top - 1].type = PkType::Tuple;
            break;
        }
        case '\x87': { // TUPLE3
            if (top < 3) return false;
            top -= 2;
            stack[top - 1].type = PkType::Tuple;
            break;
        }
        case 'K': { // BININT1 (0x4b) - 1 byte uint
            if (pos >= len || top >= 64) return false;
            stack[top].type = PkType::Int;
            stack[top].intVal = data[pos++];
            top++;
            break;
        }
        case 'M': { // BININT2 (0x4d) - 2 byte uint LE
            if (pos + 2 > len || top >= 64) return false;
            const uint16_t v = static_cast<uint16_t>(data[pos]) |
                              (static_cast<uint16_t>(data[pos + 1]) << 8);
            pos += 2;
            stack[top].type = PkType::Int;
            stack[top].intVal = v;
            top++;
            break;
        }
        case 'J': { // BININT (0x4a) - 4 byte int LE
            if (pos + 4 > len || top >= 64) return false;
            uint32_t v = static_cast<uint32_t>(data[pos]) |
                        (static_cast<uint32_t>(data[pos + 1]) << 8) |
                        (static_cast<uint32_t>(data[pos + 2]) << 16) |
                        (static_cast<uint32_t>(data[pos + 3]) << 24);
            pos += 4;
            stack[top].type = PkType::Int;
            stack[top].intVal = v;
            top++;
            break;
        }
        case 'I': { // INT (0x49) - ASCII integer ending in '\n'
            size_t end = pos;
            while (end < len && data[end] != '\n') ++end;
            if (end >= len || top >= 64) return false;
            char buf[32] = {};
            const size_t slen = (end - pos) < 31 ? (end - pos) : 31;
            memcpy(buf, data + pos, slen);
            pos = end + 1;
            stack[top].type = PkType::Int;
            stack[top].intVal = static_cast<uint32_t>(strtoul(buf, nullptr, 10));
            top++;
            break;
        }
        case 'U': { // SHORT_BINSTRING (0x55) - 1 byte len + string
            if (pos >= len || top >= 64) return false;
            const uint8_t slen = data[pos++];
            if (pos + slen > len) return false;
            stack[top].type = PkType::String;
            const size_t cplen = slen < 63 ? slen : 63;
            memcpy(stack[top].strVal, data + pos, cplen);
            stack[top].strVal[cplen] = '\0';
            pos += slen;
            top++;
            break;
        }
        case 'T':   // BINSTRING (0x54) - 4 byte len LE + string
        case 'X': { // BINUNICODE (0x58) - 4 byte len LE + utf8 string
            if (pos + 4 > len || top >= 64) return false;
            uint32_t slen = static_cast<uint32_t>(data[pos]) |
                           (static_cast<uint32_t>(data[pos + 1]) << 8) |
                           (static_cast<uint32_t>(data[pos + 2]) << 16) |
                           (static_cast<uint32_t>(data[pos + 3]) << 24);
            pos += 4;
            if (pos + slen > len) return false;
            stack[top].type = PkType::String;
            const size_t cplen = slen < 63 ? slen : 63;
            memcpy(stack[top].strVal, data + pos, cplen);
            stack[top].strVal[cplen] = '\0';
            pos += slen;
            top++;
            break;
        }
        case 'S': { // STRING (0x53) - ASCII string ending in '\n'
            size_t end = pos;
            while (end < len && data[end] != '\n') ++end;
            if (end >= len || top >= 64) return false;
            size_t start = pos;
            if (data[start] == '\'' || data[start] == '"') ++start;
            size_t stop = end;
            if (stop > start && (data[stop - 1] == '\'' || data[stop - 1] == '"')) --stop;
            const size_t slen = (stop - start) < 63 ? (stop - start) : 63;
            stack[top].type = PkType::String;
            memcpy(stack[top].strVal, data + start, slen);
            stack[top].strVal[slen] = '\0';
            pos = end + 1;
            top++;
            break;
        }
        case 'q': { // BINPUT (0x71) - 1 byte memo index
            if (pos >= len) return false;
            ++pos;
            break;
        }
        case 'r': { // LONG_BINPUT (0x72) - 4 byte memo index
            if (pos + 4 > len) return false;
            pos += 4;
            break;
        }
        case 'N': { // NONE (0x4e)
            if (top >= 64) return false;
            stack[top++].type = PkType::Mark;
            break;
        }
        case '0': { // POP (0x30)
            if (top > 0) --top;
            break;
        }
        case '2': { // DUP (0x32)
            if (top > 0 && top < 64) {
                stack[top] = stack[top - 1];
                top++;
            }
            break;
        }
        case '.': { // STOP (0x2e)
            stopped = true;
            break;
        }
        default:
            // Unknown or unsupported opcode
            return false;
        }
    }

    // After processing, numLists should have at least 3 lists (avIds = 0, names = 1, scores = 2)
    if (numLists < 3) return false;

    const PkList& avIds = lists[0];
    const PkList& names = lists[1];
    const PkList& scores = lists[2];

    const int n = avIds.count < names.count ? avIds.count : names.count;
    const int count = n < scores.count ? n : scores.count;
    out.numLeaders = count < 10 ? count : 10;

    for (int i = 0; i < out.numLeaders; ++i) {
        out.leaders[i].avId = avIds.items[i].intVal;
        strncpy(out.leaders[i].name, names.items[i].strVal, sizeof(out.leaders[i].name) - 1);
        out.leaders[i].name[sizeof(out.leaders[i].name) - 1] = '\0';
        out.leaders[i].score = scores.items[i].intVal;
    }
    for (int i = out.numLeaders; i < 10; ++i) {
        out.leaders[i] = Client::HQLeaderEntry{};
    }

    return true;
}

const Client::KnockKnockDoorState* Client::findKnockKnockDoor(uint32_t doId) const {
    for (size_t i = 0; i < kMaxKnockKnockDoors; ++i) {
        if (knockKnockDoors_[i].active && knockKnockDoors_[i].doId == doId) {
            return &knockKnockDoors_[i];
        }
    }
    return nullptr;
}

const Client::KnockKnockDoorState* Client::findKnockKnockDoorByPropId(uint16_t propId) const {
    for (size_t i = 0; i < kMaxKnockKnockDoors; ++i) {
        if (knockKnockDoors_[i].active && knockKnockDoors_[i].propId == propId) {
            return &knockKnockDoors_[i];
        }
    }
    return nullptr;
}

bool Client::handleInteriorEnter(uint32_t doId, uint16_t classId, DatagramReader& r,
                                 const uint16_t* req, uint16_t reqCount, bool other) {
    if (classId == dc::DistributedPetshopInterior::kClassId) {
        // Only setZoneIdAndBlock (required); DistributedPetshopInterior.setup adds the fish.
        for (uint16_t n = 0; n < reqCount && r.ok(); ++n) skipField(r, req[n]);
        if (other && r.ok()) {
            const uint16_t count = r.u16();
            for (uint16_t n = 0; n < count && r.ok(); ++n) skipField(r, r.u16());
        }
        petshopInteriorDoId_ = doId;
        TT_LOG("net: pet shop interior %u", unsigned(doId));
        return true;
    }
    if (classId == dc::DistributedHQInterior::kClassId) {
        HQLeaderboardState hq{};
        hq.doId = doId;
        for (uint16_t n = 0; n < reqCount && r.ok(); ++n) {
            const uint16_t fieldId = req[n];
            if (fieldId == dc::DistributedHQInterior::k_setZoneIdAndBlock) {
                hq.zoneId = r.u32();
                hq.block = r.u16();
            } else if (fieldId == dc::DistributedHQInterior::k_setLeaderBoard) {
                const Bytes b = r.blob();
                if (b.data && b.len > 0) {
                    decodeLeaderboardPickle(b.data, b.len, hq);
                }
            } else if (fieldId == dc::DistributedHQInterior::k_setTutorial) {
                hq.tutorial = r.u8();
            } else {
                skipField(r, fieldId);
            }
        }
        if (other && r.ok()) {
            const uint16_t count = r.u16();
            for (uint16_t n = 0; n < count && r.ok(); ++n) {
                const uint16_t fieldId = r.u16();
                if (fieldId == dc::DistributedHQInterior::k_setLeaderBoard) {
                    const Bytes b = r.blob();
                    if (b.data && b.len > 0) {
                        decodeLeaderboardPickle(b.data, b.len, hq);
                    }
                } else if (fieldId == dc::DistributedHQInterior::k_setTutorial) {
                    hq.tutorial = r.u8();
                } else {
                    skipField(r, fieldId);
                }
            }
        }
        hq.active = true;
        hq.revision = hqLeaderboard_.revision + 1;
        hqLeaderboard_ = hq;
        TT_LOG("net: HQ interior %u (zone %u block %u) generated with %d leaders, tutorial %u",
               unsigned(doId), unsigned(hq.zoneId), unsigned(hq.block), hq.numLeaders, unsigned(hq.tutorial));
        pushEvent(EventType::HQLeaderboard, doId);
        return true;
    }

    if (classId == dc::DistributedKnockKnockDoor::kClassId ||
        classId == dc::DistributedAnimatedProp::kClassId) {
        KnockKnockDoorState* dst = nullptr;
        for (size_t i = 0; i < kMaxKnockKnockDoors; ++i) {
            if (knockKnockDoors_[i].active && knockKnockDoors_[i].doId == doId) {
                dst = &knockKnockDoors_[i];
                break;
            }
        }
        if (!dst) {
            for (size_t i = 0; i < kMaxKnockKnockDoors; ++i) {
                if (!knockKnockDoors_[i].active) {
                    dst = &knockKnockDoors_[i];
                    break;
                }
            }
        }
        if (!dst) return false;

        KnockKnockDoorState kk{};
        kk.doId = doId;
        for (uint16_t n = 0; n < reqCount && r.ok(); ++n) {
            const uint16_t fieldId = req[n];
            if (fieldId == dc::DistributedAnimatedProp::k_setPropId) {
                kk.propId = r.u16();
            } else if (fieldId == dc::DistributedAnimatedProp::k_setAvatarInteract) {
                kk.avatarId = r.u32();
            } else if (fieldId == dc::DistributedAnimatedProp::k_setState) {
                r.string(kk.state, sizeof(kk.state));
                kk.timestamp = r.i16();
            } else {
                skipField(r, fieldId);
            }
        }
        if (other && r.ok()) {
            const uint16_t count = r.u16();
            for (uint16_t n = 0; n < count && r.ok(); ++n) {
                const uint16_t fieldId = r.u16();
                if (fieldId == dc::DistributedAnimatedProp::k_setAvatarInteract) {
                    kk.avatarId = r.u32();
                } else if (fieldId == dc::DistributedAnimatedProp::k_setState) {
                    r.string(kk.state, sizeof(kk.state));
                    kk.timestamp = r.i16();
                } else {
                    skipField(r, fieldId);
                }
            }
        }
        kk.active = true;
        kk.stateMs = timeSynced_ ? networkToLocal(kk.timestamp, now_) : now_;
        kk.serial = 1;
        *dst = kk;
        if (!strcmp(kk.state, "playing")) {
            TT_LOG("net: knock-knock door %u (propId %u) generated mid-joke for %u",
                   unsigned(doId), unsigned(kk.propId), unsigned(kk.avatarId));
        }
        pushEvent(EventType::KnockKnockState, doId);
        return true;
    }

    return false;
}

bool Client::handleInteriorField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    if (hqLeaderboard_.active && hqLeaderboard_.doId == doId) {
        if (fieldId == dc::DistributedHQInterior::k_setZoneIdAndBlock) {
            hqLeaderboard_.zoneId = r.u32();
            hqLeaderboard_.block = r.u16();
            return true;
        }
        if (fieldId == dc::DistributedHQInterior::k_setLeaderBoard) {
            const Bytes b = r.blob();
            if (b.data && b.len > 0) {
                decodeLeaderboardPickle(b.data, b.len, hqLeaderboard_);
                hqLeaderboard_.revision++;
                TT_LOG("net: HQ interior leaderboard updated (%d leaders)", hqLeaderboard_.numLeaders);
                pushEvent(EventType::HQLeaderboard, doId);
            }
            return true;
        }
        if (fieldId == dc::DistributedHQInterior::k_setTutorial) {
            hqLeaderboard_.tutorial = r.u8();
            hqLeaderboard_.revision++;
            TT_LOG("net: HQ interior tutorial flag set to %u", unsigned(hqLeaderboard_.tutorial));
            pushEvent(EventType::HQTutorial, doId);
            return true;
        }
    }

    for (size_t i = 0; i < kMaxKnockKnockDoors; ++i) {
        if (knockKnockDoors_[i].active && knockKnockDoors_[i].doId == doId) {
            if (fieldId == dc::DistributedAnimatedProp::k_setPropId) {
                knockKnockDoors_[i].propId = r.u16();
                return true;
            }
            if (fieldId == dc::DistributedAnimatedProp::k_setAvatarInteract) {
                knockKnockDoors_[i].avatarId = r.u32();
                TT_LOG("net: knock-knock %u avatarInteract %u", unsigned(doId), unsigned(knockKnockDoors_[i].avatarId));
                pushEvent(EventType::KnockKnockInteract, doId);
                return true;
            }
            if (fieldId == dc::DistributedAnimatedProp::k_setState) {
                r.string(knockKnockDoors_[i].state, sizeof(knockKnockDoors_[i].state));
                knockKnockDoors_[i].timestamp = r.i16();
                knockKnockDoors_[i].stateMs = timeSynced_ ? networkToLocal(knockKnockDoors_[i].timestamp, now_) : now_;
                knockKnockDoors_[i].serial++;
                TT_LOG("net: knock-knock %u state '%s'", unsigned(doId), knockKnockDoors_[i].state);
                pushEvent(EventType::KnockKnockState, doId);
                return true;
            }
        }
    }

    return false;
}

bool Client::handleInteriorLeaving(uint32_t doId) {
    if (hqLeaderboard_.active && hqLeaderboard_.doId == doId) {
        const uint32_t revision = hqLeaderboard_.revision;
        hqLeaderboard_ = HQLeaderboardState{};
        hqLeaderboard_.revision = revision + 1;
        return true;
    }
    if (petshopInteriorDoId_ == doId) {
        petshopInteriorDoId_ = 0;
        return true;
    }

    for (size_t i = 0; i < kMaxKnockKnockDoors; ++i) {
        if (knockKnockDoors_[i].active && knockKnockDoors_[i].doId == doId) {
            knockKnockDoors_[i] = KnockKnockDoorState{};
            pushEvent(EventType::KnockKnockGone, doId);
            return true;
        }
    }

    return false;
}

void Client::clearInteriorObjects() {
    const uint32_t revision = hqLeaderboard_.revision;
    hqLeaderboard_ = HQLeaderboardState{};
    hqLeaderboard_.revision = revision + 1;
    petshopInteriorDoId_ = 0;
    for (size_t i = 0; i < kMaxKnockKnockDoors; ++i) {
        knockKnockDoors_[i] = KnockKnockDoorState{};
    }
}

bool Client::sendKnockKnockInteract(uint32_t doId) {
    if (state_ != State::InZone || doId == 0) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedAnimatedProp, requestInteract);
    return finish(w);
}

bool Client::sendKnockKnockExit(uint32_t doId) {
    if (state_ != State::InZone || doId == 0) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedAnimatedProp, requestExit);
    return finish(w);
}

} // namespace tt::net
