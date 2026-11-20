// Per-profile plan dump for the autotest "view:" steps (main.cpp kViews): which chunks each
// of Old / New 3DS keeps near (0) or far (1), and what Zone::draw submits.
//   c++ -std=c++17 -I source tools/zone_test/view_plan.cpp source/world/zone.cpp
//       source/world/collision.cpp -o /tmp/view_plan
//   /tmp/view_plan <data>/zones/2100.tz street_far
//   /tmp/view_plan <data>/zones/2100.tz street_near,street_far   (one zone, views in order,
//   as the autotest walks them: shows meshes a previous view left resident)
#include "render/quality.h"
#include "world/zone.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tt::log { void write(const char*, ...) {} }

namespace {
struct View { const char* name; unsigned zone; float eye[3], target[3], toon[3]; };
// Same values and lenses as main.cpp's kViews (the _near views use kViewFovYDeg).
constexpr float kViewFovYDeg = 55.0f;
const View kViews[] = {
    {"ttc_near", 2000, {-79.2f, -78.1f, 5.5f}, {-82.8f, -91.6f, 5.0f}, {-81.5f, -86.8f, 1.0f}},
    {"ttc_mid", 2000, {68.7f, 1.0f, 12.5f}, {113.7f, 1.0f, 10.0f}, {75.7f, 1.0f, 5.0f}},
    {"ttc_far", 2000, {112.0f, -135.0f, 30.0f}, {0.0f, 130.0f, 0.0f}, {101.0f, -117.0f, 5.0f}},
    {"ttc_tree", 2000, {-30.0f, 30.0f, 6.0f}, {-80.0f, 40.0f, 3.0f}, {-38.0f, 31.6f, 1.0f}},
    {"street_near", 2100, {-123.0f, -115.0f, 5.0f}, {-137.0f, -115.0f, 6.0f}, {-130.0f, -115.0f, 0.5f}},
    {"street_far", 2100, {-112.0f, -105.0f, 10.0f}, {-118.0f, -250.0f, 4.0f}, {-112.0f, -112.0f, 0.5f}},
    {"dd_near", 1000, {-108.0f, -28.0f, 11.0f}, {-122.0f, -28.0f, 10.0f}, {-115.0f, -28.0f, 6.0f}},
    {"dd_far", 1000, {-110.0f, -40.0f, 14.0f}, {20.0f, 110.0f, 5.0f}, {-112.0f, -33.0f, 6.0f}},
};

void show(tt::world::Zone& zone, const View& v, bool new3ds) {
    const tt::world::Vec3 eye{v.eye[0], v.eye[1], v.eye[2]}, at{v.target[0], v.target[1], v.target[2]};
    const tt::world::Vec3 feet{v.toon[0], v.toon[1], v.toon[2]};
    const bool r11Lens = !std::strcmp(v.name + std::strlen(v.name) - 5, "_near");
    zone.setCamera(eye, at, r11Lens ? kViewFovYDeg : tt::render::kTopFovYDeg);
    zone.update(feet);
    zone.warmAround(feet, tt::render::baseQuality().drawDistance * 1.35f);
    for (int i = 0; i < 4000; ++i) {
        zone.update(feet);
        const int n = zone.completeLoads();
        if (!zone.loading() && !zone.warming() && n == 0) break;
    }
    // Host draw() never touches the renderer.
    alignas(16) static unsigned char dummy[16];
    zone.draw(*reinterpret_cast<tt::render::Renderer*>(dummy), eye, tt::render::activeQuality().drawDistance);
    char plan[4096];
    zone.describePlan(plan, sizeof(plan));
    std::printf("%s %s: draws %u tris %u geom %u tex %u\n  plan %s\n  resident", v.name,
                new3ds ? "new" : "old", zone.drawCalls(), zone.triangles(), zone.geometryBytes(),
                zone.textureBytes(), plan);
    for (int i = 0; i < zone.numChunks(); ++i)
        if (zone.chunkLod(i) >= 0) std::printf(" %d:%d", i, zone.chunkLod(i));
    std::printf("\n");
}

bool open(tt::world::Zone& zone, const char* path, bool new3ds) {
    tt::render::setActiveQuality(new3ds);
    if (!zone.load(path)) { std::printf("load failed: %s\n", zone.error()); return false; }
    // Place::applyRetain.
    if (new3ds) zone.setRetainBudget(3u << 20, 7u << 20);
    else zone.setRetainBudget(2u << 20, 6u << 20);
    return true;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: view_plan <zone.tz> [view]\n"); return 2; }
    const char* slash = std::strrchr(argv[1], '/');
    const unsigned file = static_cast<unsigned>(std::atoi(slash ? slash + 1 : argv[1]));
    for (int profile = 0; profile < 2; ++profile) {
        tt::world::Zone zone;
        if (!open(zone, argv[1], profile == 1)) return 1;
        // argv[2]: comma-separated view names, walked in order on one zone. Otherwise each
        // view of this zone file starts from a freshly loaded zone.
        for (const View& v : kViews) {
            bool want = v.zone == file;
            if (argc > 2) {
                const size_t n = std::strlen(v.name);
                want = false;
                for (const char* at = argv[2]; (at = std::strstr(at, v.name)); at += n)
                    if ((at == argv[2] || at[-1] == ',') && (at[n] == 0 || at[n] == ',')) want = true;
            }
            if (!want) continue;
            show(zone, v, profile == 1);
            if (argc <= 2) { zone.unload(); if (!open(zone, argv[1], profile == 1)) return 1; }
        }
    }
    return 0;
}
