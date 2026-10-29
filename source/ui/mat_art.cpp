#include "ui/mat_art.h"

#include <3ds.h>
#include <citro2d.h>

#include "core/log.h"
#include "render/model.h"

namespace tt::ui {

namespace {

render::Model gArt;
bool gTried = false;

} // namespace

bool drawMatArt(MatArt art, float x, float y, float w, float h, float z, bool flipX) {
    if (!gTried) {
        gTried = true;
        if (!gArt.load("romfs:/baked/mat_gui.ttm")) TT_LOG("ui: no mat_gui.ttm");
    }
    C3D_Tex* tex = gArt.loaded() ? gArt.texture(0) : nullptr;
    if (!tex || int(art) < 0 || int(art) >= int(MatArt::Count)) return false;
    const MatArtRect& r = kMatArtRects[int(art)];
    const float u0 = r.x / kMatGuiAtlasW, u1 = (r.x + r.w) / kMatGuiAtlasW;
    const float v0 = 1.0f - r.y / kMatGuiAtlasH, v1 = 1.0f - (r.y + r.h) / kMatGuiAtlasH; // top row = t 1
    Tex3DS_SubTexture sub{uint16_t(r.w + 0.5f), uint16_t(r.h + 0.5f), flipX ? u1 : u0, v0, flipX ? u0 : u1, v1};
    C2D_DrawImageAt(C2D_Image{tex, &sub}, x, y, z, nullptr, w / r.w, h / r.h);
    return true;
}

float matArtAspect(MatArt art) {
    const MatArtRect& r = kMatArtRects[int(art)];
    return r.w / r.h;
}

void releaseMatArt() {
    gArt.unload();
    gTried = false;
}

} // namespace tt::ui
