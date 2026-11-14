#include "ui/book_tab_icons.h"

#include <3ds.h>
#include <citro2d.h>

#include "core/log.h"
#include "render/model.h"
#include "ui/book_tabs_atlas.h"

namespace tt::ui {

namespace {

render::Model gIcons;
bool gTried = false;

} // namespace

bool drawBookTabIcon(int page, float cx, float cy, float size, float z) {
    if (!gTried) {
        gTried = true;
        if (!gIcons.load("romfs:/baked/book_tabs.ttm")) TT_LOG("ui: no book_tabs.ttm");
    }
    C3D_Tex* tex = gIcons.loaded() ? gIcons.texture(0) : nullptr;
    if (!tex || page < 0 || page >= kBookTabIcons) return false;
    const BookTabRect& r = kBookTabRects[page];
    const float u0 = r.x / kBookTabAtlasW, u1 = (r.x + r.w) / kBookTabAtlasW;
    const float v0 = 1.0f - r.y / kBookTabAtlasH, v1 = 1.0f - (r.y + r.h) / kBookTabAtlasH; // top row = t 1
    Tex3DS_SubTexture sub{uint16_t(r.w + 0.5f), uint16_t(r.h + 0.5f), u0, v0, u1, v1};
    C2D_Image img{tex, &sub};
    const float s = size / (r.w > r.h ? r.w : r.h);
    C2D_DrawImageAt(img, cx - r.w * s * 0.5f, cy - r.h * s * 0.5f, z, nullptr, s, s);
    return true;
}

} // namespace tt::ui
