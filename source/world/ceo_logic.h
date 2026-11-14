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

// DistributedBossbotBoss.makeIntroductionMovie at t seconds: camera, Good ol' Gil Giggles (the
// resistance Toon, BossbotRTIntroStartPosHpr) and his line.
struct CeoIntroShot {
    Vec3 eye, target;
    Vec3 resistance;
    float resistanceHeading;
    bool resistanceVisible, resistanceWalking;
    const char* chat; // nullptr when silent
    float chatAge;
};
constexpr float kCeoIntroSeconds = 18.5f;
// newToonRandom(335933, 'm', 1) with head 'sls' (__makeResistanceToon), as makeNetString bytes.
constexpr uint8_t kCeoResistanceDna[15] = {0x74, 0x1e, 0x01, 0x02, 0x01, 0x00, 0x03, 0x00,
                                           0x03, 0x01, 0x0b, 0x0f, 0x00, 0x0f, 0x0f};
void ceoIntro(float t, CeoIntroShot* out);

// DistributedBossbotBoss.enterElevator: the camera at BB_Inside_Elevator (0, 21, 7) h 180 with two
// ride bobs, then the doors open over ELEVATOR_BB openTime 4 (closed while WaitForToons). Our Toon
// stands at BigElevatorPoints[0] (h 180 in the elevator, facing the doors) until BattleOne.
struct CeoElevatorShot {
    Vec3 eye, target;
    float doorOpen; // 0 closed .. 1 open
};
constexpr Vec3 kCeoElevatorOrigin{-0.001f, -80.487f, -0.017f}; // BanquetInterior_1 elevator_origin, h 180
constexpr Vec3 kCeoElevatorToon{2.499f, -89.487f, 0.083f};    // BigElevatorPoints[0], facing +y
constexpr float kCeoElevatorDoorWidth = 6.3f;
void ceoElevator(float t, bool riding, CeoElevatorShot* out);

const char* ceoClip(const net::BossState& state, uint32_t nowMs);

} // namespace tt::world
