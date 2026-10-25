// Boss protocol snapshots and parsing, without platform or rendering dependencies.
#pragma once

#include <cstdint>
#include "net/datagram.h"

namespace tt::net {

enum class BossKind : uint8_t { None, Sellbot, Cashbot, Lawbot, Bossbot };

// ToontownGlobals.py BossCog attack codes (wire values, not animation indices).
enum BossAttack : uint8_t {
    kBossDizzy = 0,
    kBossElectricFence = 1,
    kBossSwatLeft = 2,
    kBossSwatRight = 3,
    kBossAreaAttack = 4,
    kBossFrontAttack = 5,
    kBossRecoverDizzy = 6,
    kBossDirectedAttack = 7,
    kBossStrafeAttack = 8,
    kBossNoAttack = 9,
    kBossSlowDirectedAttack = 11,
    kBossDizzyNow = 12,
};

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
    uint8_t attack = kBossNoAttack;
    uint32_t attackToon = 0;
    uint32_t attackMs = 0, attackRevision = 0;
    uint8_t strafeSide = 0, strafeDirection = 0;
    uint32_t strafeMs = 0, strafeRevision = 0;
    BossBarrier barriers[kMaxBarriers];
    int numBarriers = 0;
    bool promoted = false;
    uint16_t rewardId = 0;
    uint8_t difficulty = 0;
    uint16_t speedDamage = 0;
    uint8_t speedRecover = 0;
    int16_t speedStamp = 0;
    uint32_t speedMs = 0;
    bool heldFood = false;
    int8_t foodBelt = -1;
    int8_t foodSlot = -1;
    uint32_t foodSequence = 0;
    uint32_t foodRevision = 0;
    uint32_t lawyers[10] = {};
    int numLawyers = 0;
    uint32_t bonusMs = 0;
    uint32_t bonusRevision = 0;
    uint32_t healedToon = 0;
    int8_t tauntTarget = 0;
    int8_t tauntIndex = 0;
};

// CFO children are independent distributed objects, not DistributedLevel entities.
// 4 cranes + 9 safes + up to 16 goons + bounded treasure snapshots.
constexpr int kMaxCfoChildren = 61;
enum class CfoKind : uint8_t { None, Crane, Safe, Goon, Treasure };
struct CfoChild {
    uint32_t doId = 0;
    uint32_t bossId = 0;
    uint16_t classId = 0;
    CfoKind kind = CfoKind::None;
    bool active = false;
    uint8_t index = 0;
    char state = 'O';
    uint32_t owner = 0;
    uint32_t crane = 0;
    uint32_t revision = 0;
    uint32_t stateMs = 0;
    float x = 0;
    float y = 0;
    float z = 0;
    float h = 0;
    float targetX = 0;
    float targetY = 0;
    float targetH = 0;
    int16_t arrivalStamp = 0;
    uint32_t targetMs = 0;
    uint32_t arrivalMs = 0;
    float velocity = 4;
    float scale = 1;
    uint8_t fov = 70;
    uint8_t radius = 15;
    uint8_t strength = 1;
    uint8_t sequence = 0;
    float armY = 20;
    float armH = 0;
    float links[3][3] = {};
    int16_t cableStamp = 0;
    uint32_t cableMs = 0;
    bool rejected = false;
    bool grabbed = false;
    uint16_t style = 0;
};
CfoKind cfoKind(uint16_t classId);
bool applyCfoField(CfoChild& state, uint16_t fieldId, DatagramReader& reader, uint32_t nowMs);

BossKind bossKind(uint16_t classId);
bool applyBossField(BossState& state, uint16_t fieldId, DatagramReader& reader,
                    uint32_t nowMs, uint32_t ownAvatarId);
bool readBossBarriers(DatagramReader& reader, BossBarrier* barriers, int* count, uint32_t ownAvatarId);
float currentBossDamage(const BossState& state, uint32_t nowMs);

} // namespace tt::net
