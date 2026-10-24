// Boss protocol snapshots and parsing, without platform or rendering dependencies.
#pragma once

#include <cstdint>
#include "net/datagram.h"

namespace tt::net {

enum class BossKind : uint8_t { None, Sellbot, Cashbot, Lawbot, Bossbot };

struct BossBarrier {
    uint16_t context = 0;
    char name[32] = {};
    bool waitingForUs = false;
};

struct BossState {
    static constexpr int kMaxToons = 8;
    static constexpr int kMaxBarriers = 8;
    uint32_t doId = 0;
    uint16_t classId = 0;
    BossKind kind = BossKind::None;
    bool active = false;
    char dna[8] = {};
    char phase[32] = "Off";
    uint32_t phaseMs = 0;
    uint32_t revision = 0;
    float x = 0, y = 0, z = 0, h = 0, p = 0, r = 0;
    bool hasPosition = false;
    uint32_t toons[kMaxToons] = {};
    uint32_t sideA[kMaxToons] = {};
    uint32_t sideB[kMaxToons] = {};
    int numToons = 0, numA = 0, numB = 0;
    uint32_t battleA = 0, battleB = 0;
    uint8_t battleNumber = 0, arenaSide = 0;
    uint32_t cagedNpc = 0;
    uint32_t doobers[8] = {};
    int numDoobers = 0;
    uint16_t damage = 0;
    uint8_t recoverRate = 0;
    uint32_t damageMs = 0;
    int16_t damageStamp = 0;
    uint8_t attack = 9; // ToontownGlobals.BossCogNoAttack
    uint32_t attackToon = 0;
    uint32_t attackMs = 0, attackRevision = 0;
    uint8_t strafeSide = 0, strafeDirection = 0;
    uint32_t strafeMs = 0, strafeRevision = 0;
    BossBarrier barriers[kMaxBarriers];
    int numBarriers = 0;
    bool promoted = false;
    uint16_t rewardId = 0;
    uint8_t difficulty = 0;
};

BossKind bossKind(uint16_t classId);
bool applyBossField(BossState& state, uint16_t fieldId, DatagramReader& reader,
                    uint32_t nowMs, uint32_t ownAvatarId);
float currentBossDamage(const BossState& state, uint32_t nowMs);

} // namespace tt::net
