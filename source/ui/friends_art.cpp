#include "ui/friends_art.h"

#include "core/log.h"
#include "render/model.h"

namespace tt::ui {

namespace {

render::Model gArt;
bool gTried = false;

} // namespace

bool drawFriendsArt(FriendsArt art, float x, float y, float w, float h, float z) {
    if (!gTried) {
        gTried = true;
        if (!gArt.load("romfs:/baked/friends_gui.ttm")) TT_LOG("ui: no friends_gui.ttm");
    }
    C3D_Tex* tex = gArt.loaded() ? gArt.texture(0) : nullptr;
    if (!tex || int(art) < 0 || int(art) >= int(FriendsArt::Count)) return false;
    const FriendsArtRect& r = kFriendsArtRects[int(art)];
    const float u0 = r.x / kFriendsGuiAtlasW, u1 = (r.x + r.w) / kFriendsGuiAtlasW;
    const float v0 = 1.0f - r.y / kFriendsGuiAtlasH;
    const float v1 = 1.0f - (r.y + r.h) / kFriendsGuiAtlasH;
    Tex3DS_SubTexture sub{uint16_t(r.w + 0.5f), uint16_t(r.h + 0.5f), u0, v0, u1, v1};
    C2D_Image img{tex, &sub};
    C2D_DrawImageAt(img, x, y, z, nullptr, w / r.w, h / r.h);
    return true;
}

} // namespace tt::ui
