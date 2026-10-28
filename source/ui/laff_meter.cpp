#include "ui/laff_meter.h"

#include "core/log.h"
#include "render/model.h"
#include "ui/laff_meter_atlas.h"

namespace tt::ui {

namespace {

// Each label may fill this fraction of the cell's width (the eye whites span ~0.30..0.70).
constexpr float kLabelMaxW = 0.19f;
constexpr float kLabelMinScale = 0.34f, kLabelMaxScale = 0.62f;
// LaffMeter.fractions: tooth i shows while hp > maxHp * fraction (teeth listed 6 .. 1).
constexpr float kToothFractions[6] = {0.0f, 0.166666f, 0.333333f, 0.5f, 0.666666f, 0.833333f};
constexpr LaffPart kTeeth[6] = {kLaffTooth6, kLaffTooth5, kLaffTooth4, kLaffTooth3, kLaffTooth2, kLaffTooth1};
// LaffMeter.deathColor: the head at 0 laff.
constexpr float kDeathColor[3] = {0.58039216f, 0.80392157f, 0.34117647f};

render::Model gAtlas;
bool gTried = false;

} // namespace

// LaffMeter: the species head in the toon's head colour; at 0 laff the frown on deathColor, at
// full laff the smile with eyes, otherwise the open smile, eyes and adjustTeeth's teeth; the
// laff and max numbers sit in the eyes (hpLabel / maxLabel).
bool drawLaffMeter(const LaffFace& v, float x0, float y0, float size, const C2D_Text* hpText,
                   const C2D_Text* maxText, float z) {
    if (!gTried) {
        gTried = true;
        if (!gAtlas.load("romfs:/baked/laff_meter.ttm")) TT_LOG("hud: no laff_meter.ttm");
    }
    C3D_Tex* tex = gAtlas.loaded() ? gAtlas.texture(0) : nullptr;
    if (!tex) return false;
    auto part = [&](int index, const C2D_ImageTint* tint) {
        Tex3DS_SubTexture sub;
        sub.width = kLaffCell;
        sub.height = kLaffCell;
        sub.left = kLaffCellXY[index][0] / float(kLaffAtlasW);
        sub.right = (kLaffCellXY[index][0] + kLaffCell) / float(kLaffAtlasW);
        sub.top = 1.0f - kLaffCellXY[index][1] / float(kLaffAtlasH);
        sub.bottom = 1.0f - (kLaffCellXY[index][1] + kLaffCell) / float(kLaffAtlasH);
        C2D_Image img{tex, &sub};
        C2D_DrawImageAt(img, x0, y0, z, tint, size / kLaffCell, size / kLaffCell);
    };
    const bool dead = v.hp < 1;
    const bool full = v.hp >= v.maxHp;
    const float* rgb = dead ? kDeathColor : v.rgb;
    C2D_ImageTint headTint;
    C2D_PlainImageTint(&headTint, C2D_Color32f(rgb[0], rgb[1], rgb[2], 1.0f), 1.0f);
    C2D_SetTintMode(C2D_TintMult);
    const int species = v.species >= 0 && v.species < 9 ? v.species : 0;
    part(kLaffDoghead + species, &headTint);
    C2D_SetTintMode(C2D_TintSolid);
    if (dead) {
        part(kLaffFrown, nullptr);
        return true;
    }
    part(full ? kLaffSmile : kLaffOpenSmile, nullptr);
    if (!full) {
        for (int i = 0; i < 6; i++) {
            if (v.hp > v.maxHp * kToothFractions[i]) part(kTeeth[i], nullptr);
        }
    }
    part(kLaffEyes, nullptr);
    // Labels: upstream text_scale 0.4 is only ~8 px at HUD size, so draw them as large as the eye
    // whites allow. Both labels share one scale, shrunk until the wider one fits its eye.
    float scale = kLaffLabelHeight * size / 30.0f;
    if (scale < kLabelMinScale) scale = kLabelMinScale;
    if (scale > kLabelMaxScale) scale = kLabelMaxScale;
    float w = 0.0f, h = 0.0f, wMax = 0.0f;
    if (hpText) {
        C2D_TextGetDimensions(hpText, scale, scale, &w, &h);
        wMax = w;
    }
    if (maxText) {
        C2D_TextGetDimensions(maxText, scale, scale, &w, &h);
        if (w > wMax) wMax = w;
    }
    const float fitW = kLabelMaxW * size;
    if (wMax > fitW && wMax > 0.0f) scale *= fitW / wMax;
    const u32 ink = C2D_Color32(0x10, 0x10, 0x10, 0xFF);
    if (hpText) {
        C2D_TextGetDimensions(hpText, scale, scale, &w, &h);
        C2D_DrawText(hpText, C2D_WithColor, x0 + kLaffHpLabel[0] * size - w * 0.5f,
                     y0 + kLaffHpLabel[1] * size - h * 0.5f, z, scale, scale, ink);
    }
    if (maxText) {
        C2D_TextGetDimensions(maxText, scale, scale, &w, &h);
        C2D_DrawText(maxText, C2D_WithColor, x0 + kLaffMaxLabel[0] * size - w * 0.5f,
                     y0 + kLaffMaxLabel[1] * size - h * 0.5f, z, scale, scale, ink);
    }
    return true;
}

} // namespace tt::ui
