#include "render/quality.h"
#include "world/zone.h"
#include <cassert>
#include <cstdarg>
#include <cstdint>
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
    // Host loads publish from completeLoads(), the same gap the device has between
    // queueing a chunk and the loader finishing it.
    auto finishLoads=[&](const tt::world::Vec3& p, tt::world::Zone::CellChanged cb, void* user) {
        for (int i=0;i<4000;++i) {
            assert(zone.update(p, cb, user));
            const int n=zone.completeLoads();
            if (!zone.loading() && !zone.warming() && n==0) return;
        }
        assert(false);
    };
    while (fscanf(points,"%f %f %f %u",&x,&y,&z,&expected)==4) {
        const tt::world::Vec3 p{x,y,z};
        assert(zone.zoneAt(p)==expected);
        bool ground=false;
        const float height=zone.collision().groundHeight(p.x,p.y,p.z,&ground);
        assert(ground && height<=p.z+tt::world::kFloorReach);
        finishLoads(p, changed, &events);
        assert(zone.currentZone()==expected);
        assert(zone.numVisible()>0);
        if (zone.version()>=2) assert(zone.triangles()<=15000 && zone.drawCalls()<=150);
        assert(zone.geometryBytes()<=zone.kGeometryBudget && zone.textureBytes()<=zone.kTextureBudget);
        uint16_t ids[257];
        assert(zone.visibleZones(ids,257)>0);
        const int count=events.count;
        assert(zone.update(p,changed,&events) && events.count==count);
        ++tested;
    }
    fclose(points);
    assert(tested>0 && events.count>0);
    // The gridded cellAt() must equal the full floor scan everywhere: random points over (and
    // beyond) the zone bounds at several heights and reaches, then jitter around the samples.
    {
        uint32_t rng=12345u;
        auto next=[&rng]() { rng=rng*1664525u+1013904223u; return static_cast<float>(rng>>8)/16777216.0f; };
        const float* lo=zone.boundsMin(); const float* hi=zone.boundsMax();
        const float reaches[3]={0.0f,tt::world::kFloorReach,100.0f};
        int hits=0,total=0;
        for (int i=0;i<30000;++i) {
            const tt::world::Vec3 p{lo[0]-20+next()*(hi[0]-lo[0]+40),lo[1]-20+next()*(hi[1]-lo[1]+40),lo[2]-10+next()*(hi[2]-lo[2]+30)};
            const float reach=reaches[i%3];
            const int a=zone.cellAt(p,reach),b=zone.cellAtReference(p,reach);
            assert(a==b);
            hits+=a>=0; ++total;
        }
        assert(hits>0);
        printf("cellAt grid == full scan on %d random queries (%d with a floor)\n",total,hits);
    }
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
    // Camera plans spend the draw cap on the view. ttc_mid must keep both Toon Hall
    // halves (they share the doorway) at the near mesh on Old and New 3DS.
    if (zone.version()>=2 && std::strstr(argv[1], "2000.tz")) {
        FILE* tz=std::fopen(argv[1], "rb");
        assert(tz);
        unsigned char head[64];
        assert(std::fread(head, 1, 64, tz)==64);
        auto u32=[&](int o) {
            return (uint32_t)head[o] | ((uint32_t)head[o+1]<<8) | ((uint32_t)head[o+2]<<16) | ((uint32_t)head[o+3]<<24);
        };
        const uint32_t nchunks=u32(12);
        const uint32_t chunkOff=u32(28);
        auto hallNear=[&]() {
            int hall=0, hallHigh=0;
            for (uint32_t i=0;i<nchunks;++i) {
                unsigned char row[36+24];
                assert(std::fseek(tz, static_cast<long>(chunkOff+i*104), SEEK_SET)==0);
                assert(std::fread(row, 1, sizeof(row), tz)==sizeof(row));
                float b[6];
                std::memcpy(b, row+36, sizeof(b));
                const float door[3]={113.0f, 0.0f, 10.0f};
                bool contains=true;
                for (int a=0;a<3;++a) if (door[a]<b[a] || door[a]>b[a+3]) contains=false;
                if (!contains) continue;
                ++hall;
                if (zone.chunkLod(static_cast<int>(i))==0) ++hallHigh;
            }
            assert(hall>=2 && hallHigh==hall);
            assert(zone.triangles()<=15000 && zone.geometryBytes()<=zone.kGeometryBudget &&
                   zone.textureBytes()<=zone.kTextureBudget);
        };
        for (int profile=0; profile<2; ++profile) {
            tt::render::setActiveQuality(profile==1);
            zone.setCamera({68.7f, 1.0f, 12.5f}, {113.7f, 1.0f, 10.0f});
            finishLoads({75.7f, 1.0f, 5.0f}, nullptr, nullptr);
            hallNear();
            std::printf("ttc_mid %s resident %u draws %u tris, both hall halves near\n",
                        profile ? "new" : "old", zone.drawCalls(), zone.triangles());
        }
        tt::render::setActiveQuality(false);
        std::fclose(tz);
    }
    if (zone.version()>=2 && (std::strstr(argv[1], "1000.tz") || std::strstr(argv[1], "2100.tz"))) {
        auto check=[&](const char* name, tt::world::Vec3 eye, tt::world::Vec3 at, tt::world::Vec3 feet) {
            for (int profile=0; profile<2; ++profile) {
                tt::render::setActiveQuality(profile==1);
                zone.setCamera(eye, at);
                finishLoads(feet, nullptr, nullptr);
                assert(zone.triangles()<=15000 && zone.drawCalls()<=150 &&
                       zone.geometryBytes()<=zone.kGeometryBudget &&
                       zone.textureBytes()<=zone.kTextureBudget);
                std::printf("%s %s resident %u draws %u tris\n", name, profile ? "new" : "old",
                            zone.drawCalls(), zone.triangles());
            }
        };
        if (std::strstr(argv[1], "1000.tz")) {
            check("dd_near", {-108.0f, -28.0f, 11.0f}, {-122.0f, -28.0f, 10.0f}, {-115.0f, -28.0f, 6.0f});
            check("dd_far", {-110.0f, -40.0f, 14.0f}, {20.0f, 110.0f, 5.0f}, {-112.0f, -33.0f, 6.0f});
        } else {
            check("street_near", {-123.0f, -115.0f, 5.0f}, {-137.0f, -115.0f, 6.0f}, {-130.0f, -115.0f, 0.5f});
        }
        tt::render::setActiveQuality(false);
    }
    zone.unload();
    assert(!zone.loaded() && !zone.update({0,0,0}));
    assert(!zone.load("/definitely/missing-zone.tz"));
    assert(!zone.loaded());
    assert(zone.load(argv[1]));
    assert(zone.update({1e6f,1e6f,1e6f}));
    assert(zone.currentCell()>=0);
    if (zone.version()>=2) assert(zone.triangles()<=15000 && zone.drawCalls()<=150);
    zone.unload();
    // Every trailing path is a deliberately corrupt fixture.
    for (int i=3;i<argc;++i) {
        assert(!zone.load(argv[i]));
        assert(!zone.loaded() && *zone.error());
    }
    printf("runtime: %d floor samples, callbacks, PVS transitions, budgets, triggers, malformed files passed\n",tested);
}
