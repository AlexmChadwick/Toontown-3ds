#include "ui/purchase_art.h"

#include <3ds.h>

#include "core/log.h"
#include "render/model.h"
#include "ui/theme.h"

namespace tt::ui {

namespace {

render::Model gArt;
bool gTried = false;

const PurchaseArtRect& rectOf(PurchaseArt art) { return kPurchaseArtRects[int(art)]; }

} // namespace

bool drawPurchaseArt(PurchaseArt art, float x, float y, float w, float h, float z) {
    if (!gTried) {
        gTried = true;
        if (!gArt.load("romfs:/baked/purchase_gui.ttm")) TT_LOG("ui: no purchase_gui.ttm");
    }
    C3D_Tex* tex = gArt.loaded() ? gArt.texture(0) : nullptr;
    if (!tex || int(art) < 0 || int(art) >= int(PurchaseArt::Count)) return false;
    const PurchaseArtRect& r = rectOf(art);
    const float u0 = r.x / kPurchaseGuiAtlasW, u1 = (r.x + r.w) / kPurchaseGuiAtlasW;
    const float v0 = 1.0f - r.y / kPurchaseGuiAtlasH, v1 = 1.0f - (r.y + r.h) / kPurchaseGuiAtlasH; // top row = t 1
    Tex3DS_SubTexture sub{uint16_t(r.w + 0.5f), uint16_t(r.h + 0.5f), u0, v0, u1, v1};
    C2D_Image img{tex, &sub};
    C2D_DrawImageAt(img, x, y, z, nullptr, w / r.w, h / r.h);
    return true;
}

float purchaseArtAspect(PurchaseArt art) {
    const PurchaseArtRect& r = rectOf(art);
    return r.w / r.h;
}

void drawBeanJar(const C2D_Text& count, float x, float y, float h, float z) {
    constexpr u32 kBeanYellow = theme::rgb(0xF2, 0xF2, 0x00), kShadow = theme::rgb(0x00, 0x00, 0x00);
    const float w = h * purchaseArtAspect(PurchaseArt::Jar);
    const bool art = drawPurchaseArt(PurchaseArt::Jar, x, y, w, h, z);
    // text_pos (0, -0.1): the count sits low on the jar, over the beans.
    const float cx = x + w * 0.5f, ty = art ? y + h * 0.42f : y + h * 0.3f;
    const float scale = h * 0.0145f;
    theme::textCentered(count, cx + 1, ty + 1, scale, kShadow, w + 8, z + 0.01f);
    theme::textCentered(count, cx, ty, scale, art ? kBeanYellow : theme::kInk, w + 8, z + 0.02f);
}

float drawJarIcon(float x, float y, float h, float z) {
    const float w = h * purchaseArtAspect(PurchaseArt::Jar);
    if (drawPurchaseArt(PurchaseArt::Jar, x, y, w, h, z)) return w;
    static constexpr u32 kBeans[3] = {theme::rgb(0xE8, 0x3A, 0x6A), theme::rgb(0x6C, 0xC6, 0x3C),
                                      theme::rgb(0xF6, 0xB0, 0x22)};
    for (int i = 0; i < 3; i++) theme::roundRect(x + i * 7, y + h * 0.3f + (i & 1) * 3, 9, 6, 3, kBeans[i], z);
    return 23.0f;
}

void drawGagShopSign(const C2D_Text& title, float x, float y, float w, float h, float z) {
    constexpr u32 kSignInk = theme::rgb(0x99, 0x33, 0x00);
    if (!drawPurchaseArt(PurchaseArt::Sign, x, y, w, h, z)) theme::paper(x, y, w, h, 5.0f);
    theme::textCentered(title, x + w * 0.5f, y + h * 0.2f, h * 0.022f, kSignInk, w - h * 0.6f, z + 0.01f);
}

} // namespace tt::ui
