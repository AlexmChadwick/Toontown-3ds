#include "world/sky.h"
#include "world/sky_motion.h"
#include "core/log.h"
#include <cmath>
#include <initializer_list>

namespace tt::world {
bool Sky::load() {
    const bool ok = dome_.load("sdmc:/3ds/toontown3ds/data/sky/tt.ttm") &&
        cloud1_.load("sdmc:/3ds/toontown3ds/data/sky/tt_cloud1.ttm") &&
        cloud2_.load("sdmc:/3ds/toontown3ds/data/sky/tt_cloud2.ttm") &&
        dock_.load("sdmc:/3ds/toontown3ds/data/sky/br.ttm");
    if (ok) {
        u32 moved=0;
        for (render::Model* model : {&dome_, &cloud1_, &cloud2_, &dock_})
            moved+=model->moveTexturesToVram(512u*1024u-moved);
        TT_LOG("sky: moved %lu KB to VRAM; linear free %lu KB, VRAM free %lu KB",
               static_cast<unsigned long>(moved/1024), static_cast<unsigned long>(linearSpaceFree()/1024),
               static_cast<unsigned long>(vramSpaceFree()/1024));
    }
    if (!ok) TT_LOG("sky: missing zone skies; run tools/pipeline/bake_sky.py");
    return ok;
}
void Sky::draw(render::Renderer& renderer, const C3D_FVec& eye, bool dock, float seconds) {
    auto part = [&](render::Model& model, float degrees) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        Mtx_Translate(&world, eye.x, eye.y, eye.z, true);
        Mtx_RotateZ(&world, C3D_AngleFromDegrees(degrees), true);
        renderer.drawSky(model, world);
    };
    if (dock) part(dock_, 0);
    else {
        part(dome_, 0);
        // SkyUtil.cloudSkyTrack: +0.25 degrees/s, second ring at -0.8*h.
        const SkyHeading h = skyHeading(seconds);
        part(cloud1_, h.first);
        part(cloud2_, h.second);
    }
}
}
