#pragma once
#include "world/boss_logic.h"
namespace tt::world {
// ToontownGlobals BossbotBossDifficultySettings/ServingDuration/MaxDamage.
constexpr int kCeoTables = 13;
constexpr float kCeoMaxDamage = 500;
constexpr float kCeoMaxSpeedDamage = 90;
constexpr float kCeoServingSeconds = 300;
struct CeoPose { Vec3 position; float heading; };
struct CeoBelt { Vec3 start; Vec3 end; };
struct CeoLayout {
    CeoPose tables[kCeoTables];
    CeoPose chairs[8];
    CeoBelt belts[2];
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
