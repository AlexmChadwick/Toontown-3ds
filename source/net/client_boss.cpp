#include "net/client.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/log.h"
#include "net/msg_types.h"

namespace tt::net {

#define TT_NET_UPDATE(writer, doId, cls, field) \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " must be clsend"); \
    beginUpdate(writer, doId, dc::cls::k_##field)

#define BOSS_SIGNATURE(cls, field, signature) \
    static_assert(dc::sigEq(dc::cls::sig_##field, signature), #cls "." #field " wire layout changed")
BOSS_SIGNATURE(DistributedObject, setBarrierReady, "u16");
BOSS_SIGNATURE(DistributedSellbotBoss, hitBoss, "u8");
BOSS_SIGNATURE(DistributedSellbotBoss, hitBossInsides, "");
BOSS_SIGNATURE(DistributedSellbotBoss, touchCage, "");
BOSS_SIGNATURE(DistributedSellbotBoss, finalPieSplat, "");
BOSS_SIGNATURE(DistributedBossCog, zapToon, "i16/10,i16/10,i16/10,i16/10,i16/10,i16/10,i8/100,i8/100,u8,i16");

bool Client::handleBossEnter(uint32_t doId, uint16_t classId, DatagramReader& reader,
                             const uint16_t* required, uint16_t count, bool other) {
    const BossKind kind = bossKind(classId);
    if (kind == BossKind::None) return false;
    BossState next;
    next.doId = doId;
    next.classId = classId;
    next.kind = kind;
    bool ok = true;
    for (uint16_t i = 0; i < count && ok; ++i) {
        ok = applyBossField(next, required[i], reader, now_, avatarId_);
    }
    if (other) {
        const uint16_t fields = reader.u16();
        for (uint16_t i = 0; i < fields && ok && reader.ok(); ++i) {
            const uint16_t field = reader.u16();
            ok = applyBossField(next, field, reader, now_, avatarId_);
            if (field == dc::DistributedSellbotBoss::k_setBossDamage ||
                field == dc::DistributedLawbotBoss::k_setBossDamage ||
                field == dc::DistributedBossbotBoss::k_setBossDamage) {
                next.damageMs = timeSynced_ ? networkToLocal(next.damageStamp, now_) : now_;
            }
        }
    }
    if (ok && reader.ok()) {
        next.active = true;
        boss_ = next;
        TT_LOG("boss: generated %lu, phase %s", static_cast<unsigned long>(doId), next.phase);
    }
    return true;
}

bool Client::handleBossField(DatagramReader& reader, uint32_t doId, uint16_t fieldId) {
    if (!boss_.active || boss_.doId != doId) return false;
    if (!applyBossField(boss_, fieldId, reader, now_, avatarId_)) {
        TT_LOG("boss: malformed field %u", unsigned(fieldId));
        return true;
    }
    if (!std::strcmp(fieldName(fieldId), "setBossDamage") && boss_.kind != BossKind::Cashbot) {
        boss_.damageMs = timeSynced_ ? networkToLocal(boss_.damageStamp, now_) : now_;
    }
    return true;
}

bool Client::bossOwnField(DatagramReader& reader, uint16_t fieldId) {
    if (fieldId == dc::DistributedToon::k_setNumPies) bossPies_ = reader.u16();
    else if (fieldId == dc::DistributedToon::k_setPieType) bossPieType_ = reader.u8();
    else return false;
    return true;
}

bool Client::bossParticipant() const {
    if (state_ != State::InZone || !boss_.active) return false;
    for (int i = 0; i < boss_.numToons; ++i) {
        if (boss_.toons[i] == avatarId_) return true;
    }
    return false;
}

bool Client::spendBossPie() {
    if (!bossParticipant() || !bossPies_) return false;
    // FullPies (65535) is upstream's unlimited stock after touching the cage.
    if (bossPies_ != 65535) --bossPies_;
    return true;
}

bool Client::sendBossEnter() {
    if (state_ != State::InZone || !boss_.active) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedBossCog, avatarEnter);
    return finish(writer);
}

bool Client::sendBossExit() {
    if (state_ != State::InZone || !boss_.active) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedBossCog, avatarExit);
    return finish(writer);
}

bool Client::sendBossNear(bool near) {
    if (!bossParticipant()) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (near) {
        TT_NET_UPDATE(writer, boss_.doId, DistributedBossCog, avatarNearEnter);
    } else {
        TT_NET_UPDATE(writer, boss_.doId, DistributedBossCog, avatarNearExit);
    }
    return finish(writer);
}

bool Client::sendBossBarrier(uint16_t context) {
    if (!bossParticipant()) return false;
    bool waiting = false;
    for (int i = 0; i < boss_.numBarriers; ++i) {
        waiting |= boss_.barriers[i].context == context && boss_.barriers[i].waitingForUs;
    }
    if (!waiting) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedObject, setBarrierReady);
    writer.u16(context);
    return finish(writer);
}

bool Client::sendVpTouchCage() {
    if (!bossParticipant() || boss_.kind != BossKind::Sellbot ||
        (std::strcmp(boss_.phase, "BattleThree") && std::strcmp(boss_.phase, "NearVictory"))) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedSellbotBoss, touchCage);
    return finish(writer);
}

bool Client::sendVpHitInsides() {
    if (!bossParticipant() || boss_.kind != BossKind::Sellbot || std::strcmp(boss_.phase, "BattleThree")) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedSellbotBoss, hitBossInsides);
    return finish(writer);
}

bool Client::sendVpHit() {
    if (!bossParticipant() || boss_.kind != BossKind::Sellbot ||
        std::strcmp(boss_.phase, "BattleThree") || boss_.attack != 12) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedSellbotBoss, hitBoss);
    writer.u8(1); // DistributedSellbotBossAI.hitBoss accepts exactly one damage per pie.
    return finish(writer);
}

bool Client::sendVpFinalPie() {
    if (!bossParticipant() || boss_.kind != BossKind::Sellbot || std::strcmp(boss_.phase, "NearVictory")) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedSellbotBoss, finalPieSplat);
    return finish(writer);
}

bool Client::sendBossZap(float x, float y, float z, float heading, float directionX,
                         float directionY, uint8_t attack) {
    if (!bossParticipant() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(heading) || !std::isfinite(directionX) || !std::isfinite(directionY)) return false;
    if (attack != 1 && attack != 2 && attack != 3 && attack != 4 && attack != 5 &&
        attack != 6 && attack != 7 && attack != 8) return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, boss_.doId, DistributedBossCog, zapToon);
    const float values[] = {x, y, z, heading, 0, 0};
    for (float value : values) {
        const float packed = std::max(-32768.0f, std::min(32767.0f, std::round(value * 10)));
        writer.i16(static_cast<int16_t>(packed));
    }
    writer.i8(static_cast<int8_t>(std::round(std::max(-1.0f, std::min(1.0f, directionX)) * 100)));
    writer.i8(static_cast<int8_t>(std::round(std::max(-1.0f, std::min(1.0f, directionY)) * 100)));
    writer.u8(attack);
    writer.i16(networkTime(now_));
    return finish(writer);
}

} // namespace tt::net
