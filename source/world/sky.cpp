#include "world/sky.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/gpu_sync.h"
#include "world/place.h"

namespace tt::world {
namespace {

struct HoodSky {
    u32 hood; // or the exact zone for Cog HQ places
    const char* name;
    u8 horizon[3]; // the dome at eye level (Panda render of the sky model, eye height 6)
};

// Hood.skyFile per Toon hood (TTHood, DDHood, BRHood, MMHood, DGHood, DLHood, OZHood, GSHood, GZHood).
constexpr HoodSky kSkies[] = {
    {1000, "br", {176, 183, 199}}, {2000, "tt", {162, 204, 244}}, {3000, "br", {176, 183, 199}},
    {4000, "mm", {251, 216, 126}}, {5000, "tt", {162, 204, 244}}, {6000, "tt", {162, 204, 244}},
    {8000, "tt", {162, 204, 244}}, {9000, "dl", {74, 81, 130}},   {17000, "tt", {162, 204, 244}},
};
// SellbotHQExterior and FactoryExterior startSky (cog_sky); the lobby and the other Cog HQs do not.
constexpr HoodSky kCogSkies[] = {{11000, "cog", {88, 88, 88}}, {11200, "cog", {88, 88, 88}}};

constexpr float kDegToRad = 3.14159265f / 180.0f;

} // namespace

void Sky::setZone(u32 zoneId, bool indoors) {
    const HoodSky* want = nullptr;
    if (!indoors) {
        const u32 hood = hoodOf(zoneId);
        for (const HoodSky& s : kSkies)
            if (s.hood == hood) want = &s;
        for (const HoodSky& s : kCogSkies)
            if (s.hood == zoneId) want = &s;
    }
    if (want ? dome_.loaded() && !strcmp(name_, want->name) : !dome_.loaded()) return;
    ::tt::gpu::frameSync();
    shutdown();
    if (!want) return;
    snprintf(name_, sizeof(name_), "%s", want->name);
    for (int i = 0; i < 3; i++) horizon_[i] = want->horizon[i] / 255.0f;
    char path[64];
    snprintf(path, sizeof(path), "romfs:/baked/sky/%s.ttm", name_);
    dome_.load(path);
    snprintf(path, sizeof(path), "romfs:/baked/sky/%s_cloud1.ttm", name_);
    FILE* f = fopen(path, "rb");
    if (f) {
        fclose(f);
        cloud1_.load(path);
        snprintf(path, sizeof(path), "romfs:/baked/sky/%s_cloud2.ttm", name_);
        cloud2_.load(path);
    }
}

void Sky::draw(render::Renderer& renderer, const C3D_FVec& eye, bool fogged) {
    if (!dome_.loaded()) return;
    C3D_Mtx m;
    Mtx_Identity(&m);
    Mtx_Translate(&m, eye.x, eye.y, 0.0f, true);
    // Camera-centred and drawn without depth, so a uniform scale only changes how far away the
    // dome is, not how it looks: keep every corner inside the far plane (cog_sky reaches 610
    // units, past Old 3DS's 520), or the PICA clips the top of the dome to the clear colour.
    const float* lo = dome_.boundsMin();
    const float* hi = dome_.boundsMax();
    float reach = 0.0f;
    for (int i = 0; i < 8; i++) {
        const float x = (i & 1) ? hi[0] : lo[0], y = (i & 2) ? hi[1] : lo[1], z = ((i & 4) ? hi[2] : lo[2]) - eye.z;
        reach = fmaxf(reach, sqrtf(x * x + y * y + z * z));
    }
    const float fit = 0.9f * renderer.farPlane() / reach;
    if (fit < 1.0f) Mtx_Scale(&m, fit, fit, fit);
    renderer.drawSky(dome_, m, fogged);
    const float h = fmodf(cloudH_, 360.0f) * kDegToRad;
    if (cloud1_.loaded()) {
        C3D_Mtx c = m;
        Mtx_RotateZ(&c, h, true);
        renderer.drawSky(cloud1_, c, fogged);
    }
    if (cloud2_.loaded()) {
        C3D_Mtx c = m;
        Mtx_RotateZ(&c, -0.8f * h, true);
        renderer.drawSky(cloud2_, c, fogged);
    }
}

void Sky::shutdown() {
    dome_.unload();
    cloud1_.unload();
    cloud2_.unload();
    name_[0] = 0;
    horizon_[0] = 0x6A / 255.0f, horizon_[1] = 0xB0 / 255.0f, horizon_[2] = 0xD9 / 255.0f; // no sky: the old clear
}

} // namespace tt::world
