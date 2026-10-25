#pragma once

#include <cstdint>
#include "net/datagram.h"

namespace tt::net {

// Upstream ToontownGlobals.py and DistributedBossbotBossAI.py constants.
// Up to 13 banquet tables, 4 golf spots, and 2 food belts (19 child objects total).
constexpr int kCeoMaxTables = 13;
constexpr int kCeoMaxGolfSpots = 4;
constexpr int kCeoMaxFoodBelts = 2;
constexpr int kMaxCeoChildren = kCeoMaxTables + kCeoMaxGolfSpots + kCeoMaxFoodBelts;
constexpr int kCeoMaxDinersPerTable = 8;

// Upstream DistributedBanquetTableBase.py diner status codes.
constexpr uint8_t kDinerStatusInactive = 0;
constexpr uint8_t kDinerStatusHungry = 1;
constexpr uint8_t kDinerStatusEating = 2;
constexpr uint8_t kDinerStatusAngry = 3;
constexpr uint8_t kDinerStatusDead = 4;
constexpr uint8_t kDinerStatusHidden = 5;

// Upstream DistributedBanquetTableAI.py state chars.
constexpr char kTableStateOn = 'N';
constexpr char kTableStateOff = 'F';
constexpr char kTableStateInactive = 'I';
constexpr char kTableStateFree = 'R';
constexpr char kTableStateControlled = 'C';
constexpr char kTableStateFlat = 'L';

// Upstream DistributedGolfSpotAI.py state chars.
constexpr char kGolfStateOff = 'O';
constexpr char kGolfStateFree = 'F';
constexpr char kGolfStateControlled = 'C';

// Upstream DistributedFoodBeltAI.py state chars.
constexpr char kBeltStateOn = 'N';
constexpr char kBeltStateOff = 'F';
constexpr char kBeltStateInactive = 'I';
constexpr char kBeltStateToonup = 'T';

enum class CeoKind : uint8_t {
    None,
    Table,
    Golf,
    Belt
};

struct CeoChild {
    uint32_t doId = 0;
    uint32_t bossId = 0;
    uint16_t classId = 0;
    CeoKind kind = CeoKind::None;
    bool active = false;
    bool impactReported = false;

    // Table, golf spot, or belt index within its kind.
    uint8_t index = 0;

    // Banquet table diners info:
    uint8_t numDiners = 0;
    uint8_t hungry[kCeoMaxDinersPerTable] = {};
    uint8_t eating[kCeoMaxDinersPerTable] = {};
    uint8_t levels[kCeoMaxDinersPerTable] = {};
    uint8_t diners[kCeoMaxDinersPerTable] = {};

    // State machine:
    char state = 'F';
    uint32_t owner = 0;
    int8_t extra = 0;
    uint32_t stateMs = 0;
    uint32_t revision = 0;

    // Smoothing & aiming:
    uint8_t sequence = 0;
    float heading = 0;
    int16_t stamp = 0;

    // Seltzer bottle water stream: 6 signed values (origin XYZ, end XYZ in feet):
    float water[6] = {};
    uint32_t waterMs = 0;
    uint32_t waterRevision = 0;

    // Golf swing parameters:
    uint8_t golfPower = 0;
    float golfAngle = 0;
    uint8_t golfSequence = 0;
    uint32_t golfMs = 0;
    uint32_t golfRevision = 0;
};

CeoKind ceoKind(uint16_t classId);

bool applyCeoField(CeoChild& state, uint16_t fieldId, DatagramReader& reader, uint32_t nowMs);

} // namespace tt::net
