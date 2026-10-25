#pragma once
#include "world/collision.h"
namespace tt::world {
struct CogdoPlacement {
    Vec3 position;
    float heading;
};
extern const CogdoPlacement kCogdoCranes[4];
extern const CogdoPlacement kCogdoBags[8];
// DistCogdoCrane derives the CFO crane's three-link cable and gravity.
constexpr float kCogdoGravity = 32;
Vec3 cogdoToWorld(Vec3 local, CogdoPlacement placement);
struct CogdoCable {
    float y = 20;
    float heading = 0;
    Vec3 links[3] = {};
    Vec3 previous[3] = {};
    void reset();
    void update(float slide, float turn, float seconds, bool safe = false);
    Vec3 magnet(int index) const;
};
} // namespace tt::world
