#include "world/zone.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
namespace tt::log { void write(const char*, ...) {} }
namespace {
struct Events { int count=0; uint16_t zone=0; };
void changed(void* user,uint16_t zone,const uint16_t* ids,int n) {
    auto& e=*static_cast<Events*>(user);
    ++e.count; e.zone=zone;
    bool found=false;
    for (int i=0;i<n;++i) if (ids[i]==zone) found=true;
    assert(found && n>0 && n<=257);
    for (int i=0;i<n;++i) for (int j=i+1;j<n;++j) assert(ids[i]!=ids[j]);
}
}
int main(int argc,char** argv) {
    assert(argc>=3);
    tt::world::Zone zone;
    assert(zone.load(argv[1]));
    assert(zone.numChunks()>0 && zone.currentCell()==-1);
    assert(zone.cellAt({INFINITY,0,0})==-1 && zone.zoneAt({1e9f,1e9f,0})==0);
    FILE* points=fopen(argv[2],"r");
    assert(points);
    Events events;
    int tested=0;
    float x,y,z;
    unsigned expected;
    while (fscanf(points,"%f %f %f %u",&x,&y,&z,&expected)==4) {
        const tt::world::Vec3 p{x,y,z};
        assert(zone.zoneAt(p)==expected);
        bool ground=false;
        const float height=zone.collision().groundHeight(p.x,p.y,p.z,&ground);
        assert(ground && height<=p.z+tt::world::kFloorReach);
        assert(zone.update(p,changed,&events));
        assert(zone.currentZone()==expected);
        assert(zone.numVisible()>0);
        if (zone.version()==2) assert(zone.triangles()<=15000 && zone.drawCalls()<=150);
        assert(zone.geometryBytes()<=zone.kGeometryBudget && zone.textureBytes()<=zone.kTextureBudget);
        uint16_t ids[257];
        assert(zone.visibleZones(ids,257)>0);
        const int count=events.count;
        assert(zone.update(p,changed,&events) && events.count==count);
        ++tested;
    }
    fclose(points);
    assert(tested>0 && events.count>0);
    const int current=zone.currentCell();
    assert(zone.update({1e9f,1e9f,0}) && zone.currentCell()==current);
    for (int i=0;i<zone.numTriggers();++i) {
        const auto* t=zone.trigger(i);
        assert(t);
        tt::world::Vec3 centre{(t->min[0]+t->max[0])*.5f,(t->min[1]+t->max[1])*.5f,(t->min[2]+t->max[2])*.5f};
        int ids[4096];
        const int n=zone.triggersAt(centre,0,ids,4096,t->kind==2);
        bool found=false;
        for (int j=0;j<n;++j) if (ids[j]==i) found=true;
        assert(found);
    }
    assert(!zone.trigger(-1) && !zone.trigger(zone.numTriggers()));
    assert(!zone.triggersAt({NAN,0,0},1,nullptr,0));
    zone.unload();
    assert(!zone.loaded() && !zone.update({0,0,0}));
    assert(!zone.load("/definitely/missing-zone.tz"));
    assert(!zone.loaded());
    assert(zone.load(argv[1]));
    assert(zone.update({1e6f,1e6f,1e6f}));
    assert(zone.currentCell()>=0);
    if (zone.version()==2) assert(zone.triangles()<=15000 && zone.drawCalls()<=150);
    zone.unload();
    // Every trailing path is a deliberately corrupt fixture.
    for (int i=3;i<argc;++i) {
        assert(!zone.load(argv[i]));
        assert(!zone.loaded() && *zone.error());
    }
    printf("runtime: %d floor samples, callbacks, PVS transitions, budgets, triggers, malformed files passed\n",tested);
}
