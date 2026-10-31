#include "ui/tutorial_art.h"

#include <3ds.h>
#include <citro2d.h>

#include "core/log.h"
#include "render/model.h"

namespace tt::ui {

namespace {

render::Model gArt;
bool gTried = false;

} // namespace

bool drawTutorialCard(TutorialCard card, float x, float y, float w, float h, float z) {
    if (!gTried) {
        gTried = true;
        if (!gArt.load("romfs:/baked/tutorial_gui.ttm")) TT_LOG("ui: no tutorial_gui.ttm");
    }
    C3D_Tex* tex = gArt.loaded() ? gArt.texture(0) : nullptr;
    if (!tex || int(card) < 0 || int(card) >= int(TutorialCard::Count)) return false;
    const TutorialCardRect& r = kTutorialCardRects[int(card)];
    const float u0 = r.x / kTutorialGuiAtlasW, u1 = (r.x + r.w) / kTutorialGuiAtlasW;
    const float v0 = 1.0f - r.y / kTutorialGuiAtlasH, v1 = 1.0f - (r.y + r.h) / kTutorialGuiAtlasH; // top row = t 1
    Tex3DS_SubTexture sub{uint16_t(r.w + 0.5f), uint16_t(r.h + 0.5f), u0, v0, u1, v1};
    C2D_Image img{tex, &sub};
    float s = w / r.w;
    if (r.h * s > h) s = h / r.h;
    C2D_DrawImageAt(img, x + (w - r.w * s) * 0.5f, y + (h - r.h * s) * 0.5f, z, nullptr, s, s);
    return true;
}

} // namespace tt::ui
