#include "ui/tutorial_art.h"

#include <3ds.h>
#include <citro2d.h>

#include <cmath>

#include "core/log.h"
#include "render/model.h"

namespace tt::ui {

namespace {

render::Model gArt;
bool gTried = false;

C3D_Tex* artTexture() {
    if (!gTried) {
        gTried = true;
        if (!gArt.load("romfs:/baked/tutorial_gui.ttm")) TT_LOG("ui: no tutorial_gui.ttm");
    }
    return gArt.loaded() ? gArt.texture(0) : nullptr;
}

Tex3DS_SubTexture subTexture(const TutorialCardRect& r) {
    const float u0 = r.x / kTutorialGuiAtlasW, u1 = (r.x + r.w) / kTutorialGuiAtlasW;
    const float v0 = 1.0f - r.y / kTutorialGuiAtlasH, v1 = 1.0f - (r.y + r.h) / kTutorialGuiAtlasH; // top row = t 1
    return Tex3DS_SubTexture{uint16_t(r.w + 0.5f), uint16_t(r.h + 0.5f), u0, v0, u1, v1};
}

} // namespace

bool drawTutorialCard(TutorialCard card, float x, float y, float w, float h, float z) {
    C3D_Tex* tex = artTexture();
    if (!tex || int(card) < 0 || int(card) >= int(TutorialCard::Count)) return false;
    const TutorialCardRect& r = kTutorialCardRects[int(card)];
    const Tex3DS_SubTexture sub = subTexture(r);
    C2D_Image img{tex, &sub};
    float s = w / r.w;
    if (r.h * s > h) s = h / r.h;
    C2D_DrawImageAt(img, x + (w - r.w * s) * 0.5f, y + (h - r.h * s) * 0.5f, z, nullptr, s, s);
    return true;
}

bool drawTutorialArrow(float tipX, float tipY, float angleDeg, float size, float alpha, float z) {
    C3D_Tex* tex = artTexture();
    if (!tex) return false;
    const TutorialCardRect& r = kTutorialCardRects[int(TutorialCard::Arrow)];
    const Tex3DS_SubTexture sub = subTexture(r);
    C2D_Image img{tex, &sub};
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, C2D_Color32f(1.0f, 1.0f, 1.0f, alpha), 0.0f); // colourScale (1, 1, 1, a)
    // The baked arrow points right with its tip at the cell's right edge.
    const float a = angleDeg * 3.14159265f / 180.0f, s = size / r.w;
    C2D_DrawImageAtRotated(img, tipX - cosf(a) * size * 0.5f, tipY - sinf(a) * size * 0.5f, z, a, &tint, s, s);
    return true;
}

void releaseTutorialArt() {
    if (gArt.loaded()) TT_LOG("ui: tutorial_gui released");
    gArt.unload();
    gTried = false;
}

float tutorialArrowAlpha(float seconds) {
    constexpr float kOn = 0.75f, kOff = 0.25f; // BlinkingArrows onTime / offTime and offColor alpha
    const float t = fmodf(seconds > 0.0f ? seconds : 0.0f, 2.0f * kOn);
    return t < kOn ? kOff + (1.0f - kOff) * (t / kOn) : 1.0f - (1.0f - kOff) * ((t - kOn) / kOn);
}

} // namespace tt::ui
