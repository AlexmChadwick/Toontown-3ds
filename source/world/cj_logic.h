#pragma once
#include "world/boss_logic.h"
#include "world/cj_layout.h"

namespace tt::world {
// ToontownGlobals.LawbotBoss* and DistributedLawbotCannon constants.
constexpr float kCjMaxDamage = 2700;
constexpr float kCjInitialDamage = 1350;
constexpr float kCjJurySeconds = 70;
constexpr int kCjMaxFlights = 12;
constexpr float kCjCannonSpeed = 80;
constexpr float kCjGravity = 32;
constexpr Vec3 kCjScalePosition{-3, 12, 0};
constexpr Vec3 kCjWitnessPosition{54, 100, 0};
struct CjFlight {
    Vec3 origin;
    Vec3 velocity;
};
struct CjGavelPose {
    float heading;
    float pitch;
};
float cjScaleTilt(float damage);
Vec3 cjJuryOffset(float seconds);
Vec3 cjChairPosition(int index, float seconds);
CjFlight cjCannonFlight(Vec3 position, float heading, float rotation, float elevation, Vec3 barrelPivot);
Vec3 cjFlightPosition(CjFlight flight, float seconds);
bool cjAimCannon(Vec3 position, float heading, Vec3 barrelPivot, Vec3 target, float* rotation, float* elevation);
CjGavelPose cjGavelPose(int index, float seconds);
Vec3 cjRotate(Vec3 point, float heading, float pitch = 0);
Vec3 cjScalePan(Vec3 beamPivot, Vec3 pan, float damage);
float cjBarrierDelay(const char* phase);
const char* cjClip(const net::BossState& state, uint32_t nowMs);
} // namespace tt::world
