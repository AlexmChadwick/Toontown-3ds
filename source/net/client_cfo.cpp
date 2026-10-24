#include "net/client.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "net/msg_types.h"

namespace tt::net {

#define TT_NET_UPDATE(writer, doId, cls, field) \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " must be clsend"); \
    beginUpdate(writer, doId, dc::cls::k_##field)

namespace {
int16_t coordinate(float value) {
    return static_cast<int16_t>(std::round(std::clamp(value * 10, -32768.0f, 32767.0f)));
}
uint16_t angle(float value) {
    float wrapped = std::fmod(value, 360.0f);
    if (wrapped < 0) wrapped += 360;
    return static_cast<uint16_t>(std::round(wrapped * 100)) % 36000;
}
bool finitePose(float x, float y, float z, float h) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(h);
}
}

CfoChild* Client::cfoChild(uint32_t doId) {
    for (CfoChild& child : cfoChildren_) {
        if (child.active && child.doId == doId) return &child;
    }
    return nullptr;
}

const CfoChild* Client::findCfoChild(uint32_t doId) const {
    for (const CfoChild& child : cfoChildren_) {
        if (child.active && child.doId == doId) return &child;
    }
    return nullptr;
}

void Client::resetBoss() {
    boss_ = BossState{};
    for (CfoChild& child : cfoChildren_) child = CfoChild{};
    cfoRewardSent_ = false;
}

void Client::bossLeaving(uint32_t doId) {
    if (boss_.active && boss_.doId == doId) resetBoss();
    if (CfoChild* child = cfoChild(doId)) *child = CfoChild{};
}

bool Client::handleCfoEnter(uint32_t doId, uint16_t classId, DatagramReader& reader,
                           const uint16_t* required, uint16_t count, bool other) {
    const CfoKind kind = cfoKind(classId);
    if (kind == CfoKind::None) return false;
    CfoChild next;
    next.doId = doId;
    next.classId = classId;
    next.kind = kind;
    bool valid = true;
    for (uint16_t index = 0; index < count && valid; ++index) {
        valid = applyCfoField(next, required[index], reader, now_);
    }
    if (other && valid) {
        const uint16_t fields = reader.u16();
        for (uint16_t index = 0; index < fields && valid && reader.ok(); ++index) {
            const uint16_t field = reader.u16();
            valid = applyCfoField(next, field, reader, now_);
        }
    }
    if (!valid || !reader.ok()) return true;
    next.arrivalMs = timeSynced_ ? networkToLocal(next.arrivalStamp, now_) : now_;
    CfoChild* destination = cfoChild(doId);
    for (CfoChild& child : cfoChildren_) {
        if (!destination && !child.active) destination = &child;
    }
    if (destination) {
        next.active = true;
        *destination = next;
    }
    return true;
}

bool Client::handleCfoField(DatagramReader& reader, uint32_t doId, uint16_t fieldId) {
    CfoChild* child = cfoChild(doId);
    if (!child) return false;
    if (applyCfoField(*child, fieldId, reader, now_) &&
        fieldId == dc::DistributedCashbotBossGoon::k_setTarget) {
        child->arrivalMs = timeSynced_ ? networkToLocal(child->arrivalStamp, now_) : now_;
    }
    return true;
}

bool Client::cfoParticipant() const {
    return bossParticipant() && boss_.kind == BossKind::Cashbot && !std::strcmp(boss_.phase, "BattleThree");
}

bool Client::sendCfoControl(uint32_t craneId, bool control) {
    const CfoChild* crane = findCfoChild(craneId);
    if (!cfoParticipant() || !crane || crane->kind != CfoKind::Crane || crane->bossId != boss_.doId) return false;
    if (control) {
        if (crane->state != 'F' || crane->owner) return false;
        for (const CfoChild& child : cfoChildren_) {
            if (child.active && child.kind == CfoKind::Crane && child.owner == avatarId_) return false;
        }
    } else if (crane->owner != avatarId_) {
        return false;
    }
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (control) {
        TT_NET_UPDATE(writer, craneId, DistributedCashbotBossCrane, requestControl);
    } else {
        TT_NET_UPDATE(writer, craneId, DistributedCashbotBossCrane, requestFree);
    }
    return finish(writer);
}

bool Client::sendCfoCable(uint32_t craneId, uint8_t sequence, float y, float h, const float links[3][3]) {
    const CfoChild* crane = findCfoChild(craneId);
    if (!cfoParticipant() || !crane || crane->kind != CfoKind::Crane || crane->bossId != boss_.doId ||
        crane->state != 'C' || crane->owner != avatarId_ || !links || !std::isfinite(y) ||
        !std::isfinite(h) || y < 8 || y > 25 || h < -45 || h > 45) return false;
    for (int link = 0; link < 3; ++link) {
        for (float value : links[link]) {
            if (!std::isfinite(value) || value < -327.68f || value > 327.67f) return false;
        }
    }
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, craneId, DistributedCashbotBossCrane, setCablePos);
    writer.u8(sequence);
    writer.i16(static_cast<int16_t>(std::round(y * 100)));
    writer.u16(angle(h));
    for (int link = 0; link < 3; ++link) {
        for (float value : links[link]) writer.i16(static_cast<int16_t>(std::round(value * 100)));
    }
    writer.i16(networkTime(now_));
    return finish(writer);
}

bool Client::sendCfoObject(uint32_t objectId, CfoAction action, float value,
                           float x, float y, float z, float heading) {
    const CfoChild* object = findCfoChild(objectId);
    if (!cfoParticipant() || !object || !std::isfinite(value) || !finitePose(x, y, z, heading)) return false;
    if (object->kind == CfoKind::Treasure) {
        if (action != CfoAction::Treasure || object->grabbed) return false;
        DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
        TT_NET_UPDATE(writer, objectId, DistributedTreasure, requestGrab);
        return finish(writer);
    }
    if (object->bossId != boss_.doId || object->kind == CfoKind::Crane) return false;
    const bool owns = object->owner == avatarId_;
    const CfoChild* crane = nullptr;
    for (const CfoChild& candidate : cfoChildren_) {
        if (candidate.active && candidate.kind == CfoKind::Crane && candidate.bossId == boss_.doId &&
            candidate.owner == avatarId_ && candidate.state == 'C') crane = &candidate;
    }
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    switch (action) {
    case CfoAction::Grab:
        if (!crane || object->state == 'G' || object->state == 'O' ||
            (object->kind == CfoKind::Goon && object->state != 'S')) return false;
        for (const CfoChild& candidate : cfoChildren_) {
            if (candidate.active && candidate.state == 'G' && candidate.owner == avatarId_ &&
                candidate.bossId == boss_.doId) return false;
        }
        TT_NET_UPDATE(writer, objectId, DistributedCashbotBossObject, requestGrab);
        break;
    case CfoAction::Drop:
        if (!owns || !crane || object->state != 'G' || object->crane != crane->doId) return false;
        TT_NET_UPDATE(writer, objectId, DistributedCashbotBossObject, requestDrop);
        break;
    case CfoAction::Floor:
        if (!owns || object->state != 'D') return false;
        TT_NET_UPDATE(writer, objectId, DistributedCashbotBossObject, hitFloor);
        break;
    case CfoAction::Free:
        if (!owns) return false;
        TT_NET_UPDATE(writer, objectId, DistributedCashbotBossObject, requestFree);
        writer.i16(coordinate(x));
        writer.i16(coordinate(y));
        writer.i16(coordinate(z));
        writer.u16(angle(heading));
        break;
    case CfoAction::Hit:
        if (!owns || object->state != 'D' || value < 0 || value > 1 || object->crane == boss_.doId) return false;
        TT_NET_UPDATE(writer, objectId, DistributedCashbotBossObject, hitBoss);
        writer.u16(static_cast<uint16_t>(std::round(value * 255)));
        break;
    case CfoAction::Initial:
        if (!owns || object->kind != CfoKind::Safe) return false;
        TT_NET_UPDATE(writer, objectId, DistributedCashbotBossSafe, requestInitial);
        break;
    case CfoAction::Stun:
    case CfoAction::Battle:
        if (object->kind != CfoKind::Goon || value < 0 || value > 3276.7f ||
            (object->state != 'W' && object->state != 'a' && object->state != 'b' && object->state != 'B')) return false;
        if (action == CfoAction::Stun) {
            TT_NET_UPDATE(writer, objectId, DistributedCashbotBossGoon, requestStunned);
        } else {
            TT_NET_UPDATE(writer, objectId, DistributedCashbotBossGoon, requestBattle);
        }
        writer.i16(coordinate(value));
        break;
    case CfoAction::Treasure:
        return false;
    }
    return finish(writer);
}

bool Client::sendCfoPose(uint32_t objectId, float x, float y, float z, float heading) {
    const CfoChild* object = findCfoChild(objectId);
    if (!cfoParticipant() || !object || object->bossId != boss_.doId || object->owner != avatarId_ ||
        (object->kind != CfoKind::Safe && object->kind != CfoKind::Goon) ||
        (object->state != 'G' && object->state != 'D' && object->state != 's') || !finitePose(x, y, z, heading)) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, objectId, DistributedCashbotBossObject, setSmXYZH);
    writer.i16(coordinate(x));
    writer.i16(coordinate(y));
    writer.i16(coordinate(z));
    writer.i16(coordinate(heading));
    writer.i16(networkTime(now_));
    return finish(writer);
}

bool Client::sendCfoReward() {
    if (!bossParticipant() || boss_.kind != BossKind::Cashbot || cfoRewardSent_ ||
        std::strcmp(boss_.phase, "Epilogue")) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedCashbotBoss, applyReward);
    if (!finish(writer)) return false;
    cfoRewardSent_ = true;
    return true;
}

} // namespace tt::net
