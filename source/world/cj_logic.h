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

// DistributedLawbotBoss arrival: enterElevator's shot from LB_Elevator (0, 30, 8) h 180 (two ride
// bobs, then the doors over openTime 4), which the introduction keeps until loseCogSuits at
// (-2.798, -70, 10) h 180 (24 s); the C.J.'s LawbotBossTempIntro lines and "Attack!!" (27 s).
struct CjArrivalShot {
    Vec3 eye, target;
    float doorOpen;          // 0 closed .. 1 open
    const char* chat;        // nullptr when silent
    float chatAge;
};
constexpr float kCjIntroSeconds = 27.5f;
constexpr Vec3 kCjElevatorOrigin{-2.864f, -117.204f, 0.094f}; // LawbotCourtroom3 elevator_origin, h 180
constexpr Vec3 kCjElevatorToon{-0.364f, -126.204f, 0.194f};   // BigElevatorPoints[0], facing +y
void cjArrival(const char* phase, float t, CjArrivalShot* out);
} // namespace tt::world
