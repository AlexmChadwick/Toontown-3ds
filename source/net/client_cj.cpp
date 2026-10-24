#include "net/client.h"
#include <cmath>
#include <cstring>
#include "net/msg_types.h"

namespace tt::net {
#define TT_NET_UPDATE(writer, doId, cls, field) \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " must be clsend"); \
    beginUpdate(writer, doId, dc::cls::k_##field)
static_assert(dc::sigEq(dc::DistributedLawbotCannon::sig_setCannonWillFire,
                       "u32,i16/10,i16/10,i16/10,i16"), "CJ cannon fire layout changed");

CjChild* Client::cjChild(uint32_t doId) {
    for (CjChild& child : cjChildren_) {
        if (child.active && child.doId == doId) return &child;
    }
    return nullptr;
}
const CjChild* Client::findCjChild(uint32_t doId) const {
    for (const CjChild& child : cjChildren_) {
        if (child.active && child.doId == doId) return &child;
    }
    return nullptr;
}

bool Client::handleCjEnter(uint32_t doId, uint16_t classId, DatagramReader& reader,
                          const uint16_t* required, uint16_t count, bool other) {
    const CjKind kind = cjKind(classId);
    if (kind == CjKind::None) return false;
    CjChild next;
    next.doId = doId;
    next.classId = classId;
    next.kind = kind;
    bool valid = true;
    for (uint16_t index = 0; index < count && valid; ++index) {
        valid = applyCjField(next, required[index], reader, now_);
    }
    if (other && valid) {
        const uint16_t fields = reader.u16();
        for (uint16_t index = 0; index < fields && valid && reader.ok(); ++index) {
            const uint16_t field = reader.u16();
            valid = applyCjField(next, field, reader, now_);
        }
    }
    if (!valid || !reader.ok()) return true;
    CjChild* destination = cjChild(doId);
    int used = 0;
    for (CjChild& child : cjChildren_) {
        if (child.active && child.kind == kind) ++used;
    }
    const int limit = kind == CjKind::Chair ? 12 : kind == CjKind::Lawyer ? 10 : 8;
    if (!destination && used >= limit) return true;
    for (CjChild& child : cjChildren_) {
        if (!destination && !child.active) destination = &child;
    }
    if (destination) {
        next.active = true;
        *destination = next;
    }
    return true;
}

bool Client::handleCjField(DatagramReader& reader, uint32_t doId, uint16_t fieldId) {
    CjChild* child = cjChild(doId);
    if (!child) return false;
    if (applyCjField(*child, fieldId, reader, now_) &&
        fieldId == dc::DistributedLawbotCannon::k_setCannonWillFire) {
        child->fireMs = timeSynced_ ? networkToLocal(child->fireStamp, now_) : now_;
    }
    return true;
}

bool Client::cjParticipant(const char* phase) const {
    return bossParticipant() && boss_.kind == BossKind::Lawbot && !std::strcmp(boss_.phase, phase);
}

bool Client::sendCjCannon(uint32_t cannonId, CjCannonAction action, float rotation, float elevation) {
    CjChild* cannon = cjChild(cannonId);
    if (!cjParticipant("BattleTwo") || !cannon || cannon->kind != CjKind::Cannon ||
        cannon->bossId != boss_.doId || !std::isfinite(rotation) || !std::isfinite(elevation)) return false;
    if (rotation < -55 || rotation > 50 || elevation < 10 || elevation > 85) return false;
    if (action == CjCannonAction::Enter) {
        if (cannon->owner && cannon->owner != avatarId_) return false;
        for (const CjChild& child : cjChildren_) {
            if (child.active && child.kind == CjKind::Cannon && child.owner == avatarId_) return false;
        }
    } else if (cannon->owner != avatarId_) {
        return false;
    }
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    switch (action) {
    case CjCannonAction::Enter:
        TT_NET_UPDATE(writer, cannonId, DistributedLawbotCannon, requestEnter);
        break;
    case CjCannonAction::Leave:
        if (cannon->inFlight || cannon->pendingFire) return false;
        TT_NET_UPDATE(writer, cannonId, DistributedLawbotCannon, requestLeave);
        break;
    case CjCannonAction::Aim:
    case CjCannonAction::Fire:
        if (cannon->movie != 1 || cannon->inFlight || cannon->pendingFire || !cannon->balls) return false;
        if (action == CjCannonAction::Aim) {
            TT_NET_UPDATE(writer, cannonId, DistributedLawbotCannon, setCannonPosition);
        } else {
            TT_NET_UPDATE(writer, cannonId, DistributedLawbotCannon, setCannonLit);
        }
        writer.i16(static_cast<int16_t>(std::round(rotation * 10)));
        writer.i16(static_cast<int16_t>(std::round(elevation * 10)));
        break;
    case CjCannonAction::Landed:
        if (!cannon->inFlight) return false;
        TT_NET_UPDATE(writer, cannonId, DistributedLawbotCannon, setLanded);
        break;
    }
    if (!finish(writer)) return false;
    if (action == CjCannonAction::Fire) cannon->pendingFire = true;
    if (action == CjCannonAction::Landed) {
        cannon->inFlight = false;
        cannon->pendingFire = true; // Wait for the next server LOAD/ammunition acknowledgement.
    }
    return true;
}

bool Client::sendCjChair(uint32_t cannonId, uint8_t chairIndex) {
    CjChild* cannon = cjChild(cannonId);
    if (!cjParticipant("BattleTwo") || !cannon || cannon->kind != CjKind::Cannon ||
        cannon->bossId != boss_.doId || cannon->owner != avatarId_ || !cannon->inFlight ||
        cannon->chairReported || chairIndex >= 12) return false;
    bool present = false;
    for (const CjChild& chair : cjChildren_) {
        present |= chair.active && chair.kind == CjKind::Chair && chair.bossId == boss_.doId && chair.index == chairIndex;
    }
    if (!present) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedLawbotBoss, hitChair);
    writer.u8(chairIndex);
    writer.u8(cannon->index); // Juror index is the firing cannon, never a toon doId.
    if (!finish(writer)) return false;
    cannon->chairReported = true;
    return true;
}

bool Client::sendCjEvidence(CjEvidence action, uint32_t target) {
    if (!cjParticipant("BattleThree")) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    switch (action) {
    case CjEvidence::Witness:
        TT_NET_UPDATE(writer, boss_.doId, DistributedLawbotBoss, touchWitnessStand);
        break;
    case CjEvidence::Defense:
        // DistributedLawbotBoss.__pieSplat sends hitBoss(1) for the defense pan.
        // hitDefensePan/hitProsecutionPan are declared in DC but unused by stock AI.
        TT_NET_UPDATE(writer, boss_.doId, DistributedLawbotBoss, hitBoss);
        writer.u8(1);
        break;
    case CjEvidence::Toon: {
        if (target == avatarId_) return false;
        bool member = false;
        for (int index = 0; index < boss_.numToons; ++index) member |= boss_.toons[index] == target;
        if (!member) return false;
        TT_NET_UPDATE(writer, boss_.doId, DistributedLawbotBoss, hitToon);
        writer.u32(target);
        break;
    }
    case CjEvidence::Lawyer: {
        const CjChild* lawyer = findCjChild(target);
        bool member = false;
        for (int index = 0; index < boss_.numLawyers; ++index) member |= boss_.lawyers[index] == target;
        if (!member || !lawyer || lawyer->kind != CjKind::Lawyer) return false;
        TT_NET_UPDATE(writer, target, DistributedLawbotBossSuit, hitByToon);
        break;
    }
    }
    return finish(writer);
}

bool Client::sendCjZap(float x, float y, float z, float heading, uint8_t attack) {
    if (!cjParticipant("BattleThree") || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(z) || !std::isfinite(heading) || x < -3276 || x > 3276 ||
        y < -3276 || y > 3276 || z < -3276 || z > 3276 || heading < -3276 || heading > 3276 ||
        (attack != 4 && attack != 13 && attack != 14 && attack != 15)) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedBossCog, zapToon);
    const float values[] = {x, y, z, heading, 0, 0};
    for (float value : values) writer.i16(static_cast<int16_t>(std::round(value * 10)));
    writer.i8(0);
    writer.i8(0);
    writer.u8(attack);
    writer.i16(networkTime(now_));
    return finish(writer);
}
} // namespace tt::net
