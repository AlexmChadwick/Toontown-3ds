#pragma once

#include "world/boss_logic.h"

namespace tt::world {

// Upstream ToontownGlobals.py Bossbot Boss constants:
// Max boss damage is 500 (hitBoss deals 2x damage up to 500).
constexpr float kCeoMaxDamage = 500.0f;

// Max speed damage is 90; speed recovery rate is 20 per minute.
constexpr float kCeoMaxSpeedDamage = 90.0f;
constexpr float kCeoSpeedRecoverRate = 20.0f;

// Roll speeds in feet per second (ToontownGlobals.BossbotRollSpeedMax/Min):
constexpr float kCeoRollSpeedMax = 22.0f;
constexpr float kCeoRollSpeedMin = 7.5f;

// Serving duration in seconds (ToontownGlobals.BossbotServingDuration):
constexpr float kCeoServingSeconds = 300.0f;

// Layout counts:
constexpr int kCeoTables = 13;
constexpr int kCeoChairsPerTable = 8;
constexpr int kCeoFoodBelts = 2;
constexpr int kCeoGolfSpots = 4;

struct CeoPose {
    Vec3 position;
    float heading;
};

struct CeoBelt {
    Vec3 start;
    Vec3 end;
};

struct CeoLayout {
    CeoPose tables[kCeoTables];
    CeoPose chairs[kCeoChairsPerTable];
    CeoBelt belts[kCeoFoodBelts];
    Vec3 pitcher;
    Vec3 nozzle;
};

static_assert(sizeof(CeoLayout) == 408, "BES1 layout must match bake_ceo.py");

Vec3 ceoRotate(Vec3 point, float heading);

Vec3 ceoSeat(const CeoLayout& layout, int table, int chair);

Vec3 ceoGolfPosition(int index);

Vec3 ceoBallPosition(Vec3 origin, uint8_t power, float heading, float seconds);

float ceoSpeedDamage(const net::BossState& state, uint32_t nowMs);

float ceoRollSpeed(const net::BossState& state, uint32_t nowMs);

float ceoBarrierDelay(const char* phase);

const char* ceoClip(const net::BossState& state, uint32_t nowMs);

} // namespace tt::world
