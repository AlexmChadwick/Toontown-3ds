// Verify the proposed arrivals with the exact runtime collision implementation.
#include "world/zone.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace tt::log { void write(const char*, ...) {} }
int main(int argc,char** argv) {
    assert(argc==6 || argc==7);
    tt::world::Zone zone;
    assert(zone.load(argv[1]));
    const float x=std::atof(argv[2]), y=std::atof(argv[3]), z=std::atof(argv[4]);
    const float h=std::atof(argv[5])*3.14159265358979323846f/180;
    auto& world=zone.collision();
    bool found=false;
    const float floor=world.groundHeight(x,y,z,&found);
    const tt::world::Vec3 feet{x,y,floor+tt::world::kFloorOffset};
    const auto resolved=world.slideCapsule(feet,1.4f,6,{0,0,0});
    const float push=std::hypot(resolved.x-x,resolved.y-y);
    float fraction=1;
    const bool hit=world.cameraRaycast({x,y,floor+4},{x-20*std::sin(h),y+20*std::cos(h),floor+4},&fraction);
    std::printf("found=%d floor=%.6f drop_delta=%.6f push=%.6f forward_clear=%.6f cell=%d\n",found?1:0,floor,z-floor,push,hit?fraction*20:20,zone.cellAt(feet));
    std::fflush(stdout);
    if (argc==7) return 0;
    assert(found && std::fabs(z-floor)<.15f && push<.05f && zone.cellAt(feet)>=0);
    assert(!hit || fraction*20>8);
}
