// Facility level objects (docs/design/levels.md): the cogs of factories, mint/stage/country
// club rooms (DistributedFactorySuit and subclasses), their battle blockers, and the level
// visibility interest. The level itself (DistributedFactory / ...Room) stays in client.cpp.
#include "net/client.h"

#include <cmath>
#include <cstring>

#include "core/log.h"
#include "net/msg_types.h"

namespace tt::net {

#define TT_NET_UPDATE(w, doId, cls, field) \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " is not clsend"); \
    beginUpdate(w, doId, dc::cls::k_##field)

namespace {

constexpr uint16_t kZoneInterest = 3; // client.cpp: the one zone interest handle (re-set, not added)

int16_t packFixed(float v, float scale) {
    float s = std::floor(v * scale + 0.5f);
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    return int16_t(s);
}

bool isLevelSuitClass(uint16_t classId) {
    return classId == dc::DistributedFactorySuit::kClassId || classId == dc::DistributedMintSuit::kClassId ||
           classId == dc::DistributedStageSuit::kClassId;
}

int readIds(DatagramReader& r, uint32_t* out, int cap) {
    const uint16_t bytes = r.u16();
    int stored = 0;
    for (int i = 0; i < bytes / 4 && r.ok(); ++i) {
        const uint32_t v = r.u32();
        if (stored < cap) out[stored++] = v;
    }
    if (bytes % 4) r.skip(bytes % 4);
    return stored;
}

} // namespace

const LevelSuit* Client::findLevelSuit(uint32_t doId) const {
    for (const LevelSuit& s : levelSuits_)
        if (s.active && s.doId == doId) return &s;
    return nullptr;
}

bool Client::applyLevelSuitField(LevelSuit& s, DatagramReader& r, uint16_t fieldId) {
    namespace F = dc::DistributedFactorySuit;
    switch (fieldId) {
    case dc::DistributedSuit::k_setDNAString: { // DistributedSuitBase (same field ids)
        Bytes b = r.blob();
        if (!r.ok()) return false;
        s.hasDna = false;
        if (b.len >= 5 && b.data[0] == 's') {
            int n = 0;
            for (int i = 0; i < 3 && b.data[1 + i]; ++i) s.name[n++] = char(b.data[1 + i]);
            s.name[n] = 0;
            s.dept = char(b.data[4]);
            s.hasDna = n > 0;
        }
        break;
    }
    case dc::DistributedSuit::k_setLevelDist: s.levelDist = r.i16(); break;
    case dc::DistributedSuit::k_setSkelecog: s.skelecog = r.u8(); break;
    case dc::DistributedSuit::k_setHP: s.hp = r.i16(); break;
    case F::k_setLevelDoId: s.levelDoId = r.u32(); break;
    case F::k_setCogId: s.cogId = r.u32(); break;
    case F::k_setReserve: s.reserve = r.u8() != 0; break;
    case F::k_setConfrontToon:
        s.confrontToon = r.u32();
        ++s.chaseSerial;
        break;
    case F::k_setReturn:
        s.confrontToon = 0;
        ++s.returnSerial;
        break;
    default: // setSkeleRevives, setBrushOff, denyBattle, ...
        return skipField(r, fieldId);
    }
    return r.ok();
}

bool Client::applyLevelBlockerField(LevelBlocker& b, DatagramReader& r, uint16_t fieldId) {
    namespace B = dc::BattleBlocker;
    if (fieldId == dc::DistributedEntity::k_setLevelDoId) b.levelDoId = r.u32();
    else if (fieldId == dc::DistributedEntity::k_setEntId) b.entId = r.u32();
    else if (fieldId == B::k_setActive) b.blockerActive = r.u8() != 0;
    else if (fieldId == B::k_setSuits) b.numSuits = uint8_t(readIds(r, b.suits, 8));
    else if (fieldId == B::k_setBattle) b.battleId = r.u32();
    else if (fieldId == B::k_setBattleFinished) b.finished = true;
    else return skipField(r, fieldId);
    return r.ok();
}

bool Client::applyLevelEntityField(LevelEntityObj& e, DatagramReader& r, uint16_t fieldId) {
    namespace D = dc::DistributedDoorEntity;
    if (fieldId == dc::DistributedEntity::k_setLevelDoId) e.levelDoId = r.u32();
    else if (fieldId == dc::DistributedEntity::k_setEntId) e.entId = r.u32();
    else if (fieldId == D::k_setDoorState) {
        e.state = r.u8();
        const int32_t ts = r.i32(); // 32-bit ClockDelta time; its low 16 bits are the 16-bit time
        e.stateMs = timeSynced_ ? networkToLocal(int16_t(uint32_t(ts) & 0xFFFF), now_) : now_;
    } else if (fieldId == D::k_setLocksState) {
        const uint16_t bits = r.u16();
        for (int i = 0; i < 3; ++i)
            if (((bits >> (4 * i)) & 15) != ((e.locks >> (4 * i)) & 15)) e.locksMs[i] = now_;
        e.locks = bits;
    } else return skipField(r, fieldId);
    return r.ok();
}

bool Client::handleLevelEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                              uint16_t reqCount, bool other) {
    const dc::ClassDesc* cls = findClass(classId);
    const bool suit = isLevelSuitClass(classId);
    const bool blocker = !suit && cls && classHasField(*cls, dc::BattleBlocker::k_setBattle);
    if (classId == dc::DistributedDoorEntity::kClassId) {
        LevelEntityObj tmp{};
        tmp.doId = doId;
        tmp.classId = classId;
        bool ok = true;
        for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyLevelEntityField(tmp, r, req[i]);
        if (ok && other) {
            const uint16_t n = r.u16();
            for (uint16_t i = 0; i < n && ok; ++i) ok = applyLevelEntityField(tmp, r, r.u16());
        }
        if (!ok || !r.ok()) return true;
        for (uint32_t& t : tmp.locksMs) t = 0; // initial lock states are not transitions
        LevelEntityObj* slot = nullptr;
        for (LevelEntityObj& e : levelEntities_)
            if (e.active && e.doId == doId) slot = &e;
        for (LevelEntityObj& e : levelEntities_)
            if (!slot && !e.active) slot = &e;
        if (!slot) return true;
        tmp.active = true;
        *slot = tmp;
        return true;
    }
    if (!suit && !blocker) return false;
    if (suit) {
        LevelSuit tmp{};
        tmp.doId = doId;
        bool ok = true;
        for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyLevelSuitField(tmp, r, req[i]);
        if (ok && other) {
            const uint16_t n = r.u16();
            for (uint16_t i = 0; i < n && ok; ++i) ok = applyLevelSuitField(tmp, r, r.u16());
        }
        if (!ok || !r.ok()) return true;
        LevelSuit* slot = nullptr;
        for (LevelSuit& s : levelSuits_)
            if (s.active && s.doId == doId) slot = &s;
        for (LevelSuit& s : levelSuits_)
            if (!slot && !s.active) slot = &s;
        if (!slot) {
            TT_LOG("net: level cog %u dropped (table full)", unsigned(doId));
            return true;
        }
        tmp.active = true;
        *slot = tmp;
        TT_LOG("net: level cog %u \"%s\" cog %u level %u%s", unsigned(doId), tmp.name, unsigned(tmp.cogId),
               unsigned(tmp.levelDoId), tmp.reserve ? " (reserve)" : "");
        pushEvent(EventType::LevelSuitUp, doId);
        return true;
    }
    LevelBlocker tmp{};
    tmp.doId = doId;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyLevelBlockerField(tmp, r, req[i]);
    if (ok && other) {
        const uint16_t n = r.u16();
        for (uint16_t i = 0; i < n && ok; ++i) ok = applyLevelBlockerField(tmp, r, r.u16());
    }
    if (!ok || !r.ok()) return true;
    LevelBlocker* slot = nullptr;
    for (LevelBlocker& b : levelBlockers_)
        if (b.active && b.doId == doId) slot = &b;
    for (LevelBlocker& b : levelBlockers_)
        if (!slot && !b.active) slot = &b;
    if (!slot) return true;
    tmp.active = true;
    *slot = tmp;
    pushEvent(EventType::LevelBlockerState, doId);
    return true;
}

bool Client::handleLevelField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    for (LevelSuit& s : levelSuits_) {
        if (!s.active || s.doId != doId) continue;
        if (applyLevelSuitField(s, r, fieldId)) pushEvent(EventType::LevelSuitState, doId);
        return true;
    }
    for (LevelBlocker& b : levelBlockers_) {
        if (!b.active || b.doId != doId) continue;
        if (applyLevelBlockerField(b, r, fieldId)) pushEvent(EventType::LevelBlockerState, doId);
        return true;
    }
    for (LevelEntityObj& e : levelEntities_) {
        if (!e.active || e.doId != doId) continue;
        applyLevelEntityField(e, r, fieldId);
        return true;
    }
    return false;
}

bool Client::handleLevelLeaving(uint32_t doId) {
    for (LevelSuit& s : levelSuits_) {
        if (!s.active || s.doId != doId) continue;
        s = LevelSuit{};
        pushEvent(EventType::LevelSuitGone, doId);
        return true;
    }
    for (LevelBlocker& b : levelBlockers_) {
        if (!b.active || b.doId != doId) continue;
        b = LevelBlocker{};
        pushEvent(EventType::LevelBlockerState, doId);
        return true;
    }
    for (LevelEntityObj& e : levelEntities_) {
        if (!e.active || e.doId != doId) continue;
        e = LevelEntityObj{};
        return true;
    }
    return false;
}

void Client::clearLevelObjects() {
    for (LevelSuit& s : levelSuits_) {
        if (s.active) pushEvent(EventType::LevelSuitGone, s.doId);
        s = LevelSuit{};
    }
    for (LevelBlocker& b : levelBlockers_) b = LevelBlocker{};
    for (LevelEntityObj& e : levelEntities_) e = LevelEntityObj{};
}

bool Client::setLevelVisibility(uint32_t levelZone, const uint32_t* zones, int count) {
    if (state_ != State::InZone) return false;
    uint32_t ids[kMaxVisZones + 1];
    int n = 0;
    ids[n++] = levelZone;
    for (int i = 0; i < count && n <= kMaxVisZones; ++i) {
        bool dup = false;
        for (int j = 0; j < n; ++j) dup = dup || ids[j] == zones[i];
        if (!dup) ids[n++] = zones[i];
    }
    cfg_.zoneId = levelZone;
    sendSetLocation(avatarId_, shardId_, levelZone);
    zoneCtx_ = nextContext_;
    sendAddInterestMultiple(kZoneInterest, shardId_, ids, n);
    TT_LOG("net: level %u visibility: %d zones", unsigned(levelZone), n);
    return true;
}

bool Client::sendLevelSuitBattle(uint32_t doId, float x, float y, float z, float h) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedFactorySuit, requestBattle);
    w.i16(packFixed(x, 10.0f));
    w.i16(packFixed(y, 10.0f));
    w.i16(packFixed(z, 10.0f));
    w.i16(packFixed(h, 10.0f));
    w.i16(0);
    w.i16(0);
    TT_LOG("net: requestBattle on level cog %u", unsigned(doId));
    return finish(w);
}

bool Client::sendLevelDoorOpen(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedDoorEntity, requestOpen);
    return finish(w);
}

bool Client::sendLevelSuitAlert(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedFactorySuit, setAlert);
    w.u32(avatarId_);
    return finish(w);
}

bool Client::sendLevelSuitStrayed(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedFactorySuit, setStrayed);
    return finish(w);
}

} // namespace tt::net
