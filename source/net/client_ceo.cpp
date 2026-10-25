#include "net/client.h"

#include <cmath>
#include <cstring>
#include "net/msg_types.h"

namespace tt::net {

#define TT_NET_UPDATE(writer, id, cls, field) \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " must be clsend"); \
    beginUpdate(writer, id, dc::cls::k_##field)

// Upstream ToontownGlobals / DistributedBossbotBoss limits:
// 13 tables, 4 spots, 2 belts.
constexpr int kCeoTableLimit = 13;
constexpr int kCeoGolfLimit = 4;
constexpr int kCeoBeltLimit = 2;

// Belt has 4 food/toonup slots:
constexpr int kCeoBeltSlots = 4;

// Water bottle damage in upstream is 1, 2, or 3 based on charge fraction:
constexpr uint8_t kMinWaterDamage = 1;
constexpr uint8_t kMaxWaterDamage = 3;

// Golf ball speed damage in upstream is 1 or 2:
constexpr uint8_t kMinBallDamage = 1;
constexpr uint8_t kMaxBallDamage = 2;

// Upstream ToontownGlobals.py BossCogMoveAttack = 16:
constexpr uint8_t kBossMoveAttack = 16;

CeoChild* Client::ceoChild(uint32_t id) {
    for (CeoChild& child : ceoChildren_) {
        if (child.active && child.doId == id) {
            return &child;
        }
    }
    return nullptr;
}

const CeoChild* Client::findCeoChild(uint32_t id) const {
    for (const CeoChild& child : ceoChildren_) {
        if (child.active && child.doId == id) {
            return &child;
        }
    }
    return nullptr;
}

bool Client::ceoParticipant(const char* phase) const {
    return bossParticipant() && boss_.kind == BossKind::Bossbot && !std::strcmp(boss_.phase, phase);
}

bool Client::handleCeoEnter(uint32_t id, uint16_t classId, DatagramReader& reader,
                            const uint16_t* required, uint16_t count, bool other) {
    const CeoKind kind = ceoKind(classId);
    if (kind == CeoKind::None) {
        return false;
    }

    CeoChild next;
    next.doId = id;
    next.classId = classId;
    next.kind = kind;

    bool valid = true;
    for (uint16_t index = 0; index < count && valid; ++index) {
        valid = applyCeoField(next, required[index], reader, now_);
    }

    if (other && valid) {
        const uint16_t fields = reader.u16();
        for (uint16_t index = 0; index < fields && valid && reader.ok(); ++index) {
            const uint16_t field = reader.u16();
            valid = applyCeoField(next, field, reader, now_);
        }
    }

    if (!valid || !reader.ok()) {
        return true;
    }

    CeoChild* destination = ceoChild(id);
    int used = 0;
    for (CeoChild& child : ceoChildren_) {
        if (child.active && child.kind == kind) {
            ++used;
        }
    }

    int limit = kCeoTableLimit;
    if (kind == CeoKind::Golf) {
        limit = kCeoGolfLimit;
    } else if (kind == CeoKind::Belt) {
        limit = kCeoBeltLimit;
    }

    if (!destination && used >= limit) {
        return true;
    }

    for (CeoChild& child : ceoChildren_) {
        if (!destination && !child.active) {
            destination = &child;
        }
    }

    if (destination) {
        next.active = true;
        *destination = next;
    }
    return true;
}

bool Client::handleCeoField(DatagramReader& reader, uint32_t id, uint16_t field) {
    CeoChild* child = ceoChild(id);
    if (!child) {
        return false;
    }

    // Locally published actions already started. Broadcast may echo to the owner.
    if (child->owner == avatarId_ &&
        (field == dc::DistributedBanquetTable::k_firingWater ||
         field == dc::DistributedGolfSpot::k_setSwingInfo)) {
        skipField(reader, field);
        return true;
    }

    applyCeoField(*child, field, reader, now_);
    return true;
}

bool Client::sendCeoControl(uint32_t id, bool enter, bool hit) {
    CeoChild* child = ceoChild(id);
    if (!ceoParticipant("BattleFour") || !child || child->bossId != boss_.doId || child->kind == CeoKind::Belt) {
        return false;
    }

    if (enter) {
        // Table Free state is 'R'; GolfSpot Free state is 'F'.
        const char freeState = (child->kind == CeoKind::Table) ? kTableStateFree : kGolfStateFree;
        if (child->owner != 0 || child->state != freeState) {
            return false;
        }
        for (const CeoChild& current : ceoChildren_) {
            if (current.active && current.owner == avatarId_) {
                return false;
            }
        }
    } else {
        if (child->owner != avatarId_ || child->state != kTableStateControlled) {
            return false;
        }
    }

    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (child->kind == CeoKind::Table) {
        if (enter) {
            TT_NET_UPDATE(writer, id, DistributedBanquetTable, requestControl);
        } else {
            TT_NET_UPDATE(writer, id, DistributedBanquetTable, requestFree);
        }
    } else {
        if (enter) {
            TT_NET_UPDATE(writer, id, DistributedGolfSpot, requestControl);
        } else {
            TT_NET_UPDATE(writer, id, DistributedGolfSpot, requestFree);
        }
    }

    // Upstream requestFree(int8 gotHitByBoss):
    if (!enter) {
        writer.i8(hit ? 1 : 0);
    }
    return finish(writer);
}

bool Client::sendCeoClear(uint32_t id) {
    CeoChild* child = ceoChild(id);
    if (!ceoParticipant("BattleFour") || !child || child->bossId != boss_.doId ||
        child->owner != avatarId_ || child->state != kTableStateControlled) {
        return false;
    }

    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (child->kind == CeoKind::Table) {
        TT_NET_UPDATE(writer, id, DistributedBanquetTable, clearSmoothing);
    } else if (child->kind == CeoKind::Golf) {
        TT_NET_UPDATE(writer, id, DistributedGolfSpot, clearSmoothing);
    } else {
        return false;
    }

    writer.i8(0);
    return finish(writer);
}

bool Client::sendCeoAim(uint32_t id, uint8_t sequence, float heading) {
    CeoChild* child = ceoChild(id);
    if (!ceoParticipant("BattleFour") || !child || child->bossId != boss_.doId ||
        child->owner != avatarId_ || child->state != kTableStateControlled ||
        !std::isfinite(heading) || std::fabs(heading) > 360.0f) {
        return false;
    }

    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (child->kind == CeoKind::Table) {
        TT_NET_UPDATE(writer, id, DistributedBanquetTable, setPitcherPos);
    } else if (child->kind == CeoKind::Golf) {
        TT_NET_UPDATE(writer, id, DistributedGolfSpot, setGolfSpotPos);
    } else {
        return false;
    }

    writer.u8(sequence);
    writer.u16(static_cast<uint16_t>(std::round(std::fmod(heading + 360.0f, 360.0f) * 100.0f)));
    writer.i16(networkTime(now_));

    if (!finish(writer)) {
        return false;
    }

    child->heading = heading;
    child->sequence = sequence;
    return true;
}

bool Client::sendCeoWater(uint32_t id, const float endpoints[6]) {
    CeoChild* child = ceoChild(id);
    if (!ceoParticipant("BattleFour") || !child || child->bossId != boss_.doId ||
        child->kind != CeoKind::Table || child->owner != avatarId_ ||
        child->state != kTableStateControlled ||
        (child->waterRevision && now_ - child->waterMs < 500)) {
        return false;
    }

    for (int index = 0; index < 6; ++index) {
        if (!std::isfinite(endpoints[index]) || std::fabs(endpoints[index]) > 10000.0f) {
            return false;
        }
    }

    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, id, DistributedBanquetTable, firingWater);
    for (int index = 0; index < 6; ++index) {
        writer.i32(static_cast<int32_t>(std::round(endpoints[index] * 100.0f)));
    }

    if (!finish(writer)) {
        return false;
    }

    std::memcpy(child->water, endpoints, sizeof(child->water));
    child->waterMs = now_;
    ++child->waterRevision;
    child->impactReported = false;
    return true;
}

bool Client::sendCeoWaterHit(uint32_t id, uint8_t damage) {
    CeoChild* child = ceoChild(id);
    if (!ceoParticipant("BattleFour") || !child || child->kind != CeoKind::Table ||
        child->bossId != boss_.doId || child->owner != avatarId_ ||
        child->state != kTableStateControlled || !child->waterRevision ||
        child->impactReported || damage < kMinWaterDamage || damage > kMaxWaterDamage) {
        return false;
    }

    // Upstream DistributedBanquetTable.waterHitBoss broadcast:
    DatagramWriter splash = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(splash, id, DistributedBanquetTable, waterHitBoss);
    splash.u8(child->index);
    if (!finish(splash)) {
        return false;
    }

    // Upstream DistributedBossbotBossAI.hitBoss(damage):
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedBossbotBoss, hitBoss);
    writer.u8(damage);
    if (!finish(writer)) {
        return false;
    }

    child->impactReported = true;
    return true;
}

bool Client::sendCeoGolf(uint32_t id, uint8_t power, float angle, uint8_t sequence) {
    CeoChild* child = ceoChild(id);
    if (!ceoParticipant("BattleFour") || !child || child->kind != CeoKind::Golf ||
        child->bossId != boss_.doId || child->owner != avatarId_ ||
        child->state != kGolfStateControlled || power > 100 ||
        !std::isfinite(angle) || std::fabs(angle) > 360.0f ||
        (child->golfRevision && now_ - child->golfMs < 3000)) {
        return false;
    }

    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, id, DistributedGolfSpot, setSwingInfo);
    writer.u8(power);
    writer.i16(static_cast<int16_t>(std::round(angle * 10.0f)));
    writer.u8(sequence);

    if (!finish(writer)) {
        return false;
    }

    child->golfPower = power;
    child->golfAngle = angle;
    child->golfSequence = sequence;
    child->golfMs = now_;
    ++child->golfRevision;
    child->impactReported = false;
    return true;
}

bool Client::sendCeoBallHit(uint32_t id, uint8_t damage) {
    CeoChild* child = ceoChild(id);
    if (!ceoParticipant("BattleFour") || !child || child->kind != CeoKind::Golf ||
        child->bossId != boss_.doId || child->owner != avatarId_ ||
        !child->golfRevision || child->impactReported ||
        damage < kMinBallDamage || damage > kMaxBallDamage) {
        return false;
    }

    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedBossbotBoss, ballHitBoss);
    writer.u8(damage);
    if (!finish(writer)) {
        return false;
    }

    child->impactReported = true;
    return true;
}

bool Client::sendCeoFood(int8_t belt, int8_t slot, uint32_t sequence, bool toonup) {
    const char* expectedPhase = toonup ? "BattleFour" : "BattleTwo";
    if (!ceoParticipant(expectedPhase) ||
        belt < 0 || belt >= kCeoBeltLimit ||
        slot < 0 || slot >= kCeoBeltSlots ||
        (!toonup && boss_.heldFood)) {
        return false;
    }

    const char requiredState = toonup ? kBeltStateToonup : kBeltStateOn;
    bool present = false;
    for (const CeoChild& child : ceoChildren_) {
        if (child.active && child.kind == CeoKind::Belt &&
            child.bossId == boss_.doId && child.index == belt &&
            child.state == requiredState) {
            present = true;
            break;
        }
    }
    if (!present) {
        return false;
    }

    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (toonup) {
        TT_NET_UPDATE(writer, boss_.doId, DistributedBossbotBoss, requestGetToonup);
    } else {
        TT_NET_UPDATE(writer, boss_.doId, DistributedBossbotBoss, requestGetFood);
    }
    writer.i8(belt);
    writer.i8(slot);
    writer.u32(sequence);
    return finish(writer);
}

bool Client::sendCeoServe(uint32_t id, uint8_t seat) {
    CeoChild* child = ceoChild(id);
    if (!ceoParticipant("BattleTwo") || !boss_.heldFood || !child ||
        child->kind != CeoKind::Table || child->bossId != boss_.doId ||
        seat >= child->numDiners) {
        return false;
    }

    // Upstream DistributedBanquetTable: diner must be HUNGRY (1) or ANGRY (3) to be served.
    if (child->diners[seat] != kDinerStatusHungry && child->diners[seat] != kDinerStatusAngry) {
        return false;
    }

    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedBossbotBoss, requestServeFood);
    writer.i8(child->index);
    writer.i8(seat);
    return finish(writer);
}

bool Client::sendCeoTable(uint8_t index, CeoTableAction action) {
    if (!ceoParticipant("BattleFour") || boss_.attack != kBossMoveAttack ||
        boss_.attackToon != index || index >= kCeoTableLimit) {
        return false;
    }

    bool present = false;
    for (const CeoChild& child : ceoChildren_) {
        if (child.active && child.kind == CeoKind::Table &&
            child.index == index && child.bossId == boss_.doId) {
            present = true;
            break;
        }
    }
    if (!present) {
        return false;
    }

    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (action == CeoTableAction::Reached) {
        TT_NET_UPDATE(writer, boss_.doId, DistributedBossbotBoss, reachedTable);
    } else if (action == CeoTableAction::Hit) {
        TT_NET_UPDATE(writer, boss_.doId, DistributedBossbotBoss, hitTable);
    } else {
        TT_NET_UPDATE(writer, boss_.doId, DistributedBossbotBoss, awayFromTable);
    }
    writer.u8(index);
    return finish(writer);
}

bool Client::sendCeoZap(float x, float y, float z, float heading, uint8_t attack) {
    if (!ceoParticipant("BattleFour")) {
        return false;
    }

    // Whitelist valid attack codes from ToontownGlobals:
    // ElectricFence (1), SwatLeft (2), SwatRight (3), AreaAttack (4), FrontAttack (5),
    // DirectedAttack (7), SlowDirectedAttack (11), MoveAttack (16), GolfAttack (17),
    // GolfAreaAttack (18), GearDirectedAttack (19), OvertimeAttack (20).
    const bool validAttack =
        (attack == 1 || attack == 2 || attack == 3 || attack == 4 || attack == 5 ||
         attack == 7 || attack == 11 || attack == 16 || attack == 17 || attack == 18 ||
         attack == 19 || attack == 20);
    if (!validAttack) {
        return false;
    }

    const float values[] = {x, y, z, heading};
    for (float value : values) {
        if (!std::isfinite(value) || std::fabs(value) > 3276.0f) {
            return false;
        }
    }

    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedBossCog, zapToon);
    for (float value : values) {
        writer.i16(static_cast<int16_t>(std::round(value * 10.0f)));
    }
    writer.i16(0); // directionX
    writer.i16(0); // directionY
    writer.i8(0);
    writer.i8(0);
    writer.u8(attack);
    writer.i16(networkTime(now_));
    return finish(writer);
}

} // namespace tt::net
