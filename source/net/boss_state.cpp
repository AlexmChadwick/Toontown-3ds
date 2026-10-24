#include "net/boss_state.h"

#include <algorithm>
#include <cstring>

namespace tt::net {
namespace {

bool readIds(DatagramReader& reader, uint32_t* ids, int capacity, int* count) {
    const uint16_t bytes = reader.u16();
    if (bytes % 4 || bytes > capacity * 4) {
        reader.fail();
        return false;
    }
    *count = bytes / 4;
    for (int i = 0; i < *count; ++i) ids[i] = reader.u32();
    return reader.ok();
}

bool named(const char* name, const char* expected) {
    return std::strcmp(name, expected) == 0;
}

} // namespace

BossKind bossKind(uint16_t classId) {
    if (classId == dc::DistributedSellbotBoss::kClassId) return BossKind::Sellbot;
    if (classId == dc::DistributedCashbotBoss::kClassId) return BossKind::Cashbot;
    if (classId == dc::DistributedLawbotBoss::kClassId) return BossKind::Lawbot;
    if (classId == dc::DistributedBossbotBoss::kClassId) return BossKind::Bossbot;
    return BossKind::None;
}

bool applyBossField(BossState& state, uint16_t fieldId, DatagramReader& reader,
                    uint32_t nowMs, uint32_t ownAvatarId) {
    const dc::ClassDesc* cls = findClass(state.classId);
    if (!cls || bossKind(state.classId) == BossKind::None || !classHasField(*cls, fieldId)) {
        reader.fail();
        return false;
    }
    BossState next = state;
    const char* name = fieldName(fieldId);
    if (named(name, "setDNAString")) {
        Bytes dna = reader.blob();
        if (dna.len >= sizeof(next.dna)) {
            reader.fail();
        } else if (reader.ok()) {
            std::memcpy(next.dna, dna.data, dna.len);
            next.dna[dna.len] = 0;
        }
    } else if (named(name, "setState")) {
        char phase[sizeof(next.phase)] = {};
        reader.string(phase, sizeof(phase));
        if (std::strcmp(next.phase, phase)) next.phaseMs = nowMs;
        std::memcpy(next.phase, phase, sizeof(phase));
    } else if (named(name, "setToonIds")) {
        readIds(reader, next.toons, 8, &next.numToons);
        readIds(reader, next.sideA, 8, &next.numA);
        readIds(reader, next.sideB, 8, &next.numB);
    } else if (named(name, "setBattleIds")) {
        next.battleNumber = reader.u8();
        next.battleA = reader.u32();
        next.battleB = reader.u32();
    } else if (named(name, "setArenaSide")) {
        next.arenaSide = reader.u8();
    } else if (named(name, "setCagedToonNpcId")) {
        next.cagedNpc = reader.u32();
    } else if (named(name, "setDooberIds")) {
        readIds(reader, next.doobers, 8, &next.numDoobers);
    } else if (named(name, "setBossDamage")) {
        next.damage = reader.u16();
        next.damageMs = nowMs;
        if (next.kind != BossKind::Cashbot) {
            next.recoverRate = reader.u8();
            next.damageStamp = reader.i16();
        }
    } else if (named(name, "setAttackCode")) {
        next.attack = reader.u8();
        next.attackToon = reader.u32();
        next.attackMs = nowMs;
        ++next.attackRevision;
    } else if (named(name, "doStrafe")) {
        next.strafeSide = reader.u8();
        next.strafeDirection = reader.u8();
        next.strafeMs = nowMs;
        ++next.strafeRevision;
    } else if (named(name, "setBarrierData")) {
        readBossBarriers(reader, next.barriers, &next.numBarriers, ownAvatarId);
    } else if (named(name, "toonPromoted")) {
        next.promoted = reader.u8() != 0;
    } else if (named(name, "setRewardId")) {
        next.rewardId = reader.u16();
    } else if (named(name, "setBattleDifficulty")) {
        next.difficulty = reader.u8();
    } else if (named(name, "setX") || named(name, "setY") || named(name, "setZ") ||
               named(name, "setH") || named(name, "setP") || named(name, "setR")) {
        float* value = named(name, "setX") ? &next.x : named(name, "setY") ? &next.y :
                       named(name, "setZ") ? &next.z : named(name, "setH") ? &next.h :
                       named(name, "setP") ? &next.p : &next.r;
        *value = reader.i16() / 10.0f;
        next.hasPosition = true;
    } else if (named(name, "setPosHpr") || named(name, "setPos") || named(name, "setHpr")) {
        if (!named(name, "setHpr")) {
            next.x = reader.i16() / 10.0f;
            next.y = reader.i16() / 10.0f;
            next.z = reader.i16() / 10.0f;
        }
        if (!named(name, "setPos")) {
            next.h = reader.i16() / 10.0f;
            next.p = reader.i16() / 10.0f;
            next.r = reader.i16() / 10.0f;
        }
        next.hasPosition = true;
    } else if (!skipField(reader, fieldId)) {
        return false;
    }
    if (!reader.ok()) return false;
    ++next.revision;
    state = next;
    return true;
}

bool readBossBarriers(DatagramReader& reader, BossBarrier* output, int* count, uint32_t ownAvatarId) {
    Bytes bytes = reader.blob();
    DatagramReader barriers(bytes.data, bytes.len);
    *count = 0;
    while (barriers.remaining() && barriers.ok()) {
        BossBarrier barrier;
        barrier.context = barriers.u16();
        barriers.string(barrier.name, sizeof(barrier.name));
        uint32_t avatars[8];
        int avatarCount = 0;
        readIds(barriers, avatars, 8, &avatarCount);
        for (int index = 0; index < avatarCount; ++index) {
            barrier.waitingForUs |= avatars[index] == ownAvatarId;
        }
        if (*count == BossState::kMaxBarriers) {
            barriers.fail();
            break;
        }
        output[(*count)++] = barrier;
    }
    if (!barriers.ok()) reader.fail();
    return reader.ok();
}

float currentBossDamage(const BossState& state, uint32_t nowMs) {
    const int32_t milliseconds = static_cast<int32_t>(nowMs - state.damageMs);
    const float elapsed = std::max<int32_t>(0, milliseconds) / 1000.0f;
    return std::max(0.0f, state.damage - state.recoverRate * elapsed / 60.0f);
}

} // namespace tt::net
