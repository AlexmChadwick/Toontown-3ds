// CFO crane coordinates and bounded physics, independent of libctru.
#pragma once

#include "world/boss_logic.h"

namespace tt::world {
struct CfoPlacement {
    Vec3 position;
    float heading;
};
extern const CfoPlacement kCfoCranes[4];
extern const CfoPlacement kCfoSafes[9];
constexpr Vec3 kCfoBoss{120, -315, 0}; // ToontownGlobals.CashbotBossBattleThreePosHpr
constexpr float kCfoGravity = 32; // DistributedCashbotBoss.__init__ LinearVectorForce
constexpr float kCfoMaxDamage = 500;

Vec3 cfoToWorld(Vec3 local, CfoPlacement placement);
float cfoImpact(Vec3 velocity, float craneHeading);
bool cfoGoonDetect(const net::CfoChild& goon, Vec3 position, Vec3 toon);
bool cfoGoonStun(const net::CfoChild& goon, Vec3 position, Vec3 toon);

struct CfoCable {
    float y = 20;
    float heading = 0;
    Vec3 links[3] = {};
    Vec3 previous[3] = {};
    void reset();
    void update(float slide, float turn, float seconds, bool safe);
    Vec3 magnet(int index) const;
};
const char* cfoClip(const net::BossState& state, uint32_t nowMs);
} // namespace tt::world
