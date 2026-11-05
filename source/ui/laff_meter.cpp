#include "ui/laff_meter.h"

#include <cstdio>

#include "core/log.h"
#include "render/ui_font.h"
#include "render/model.h"
#include "ui/inventory_art.h"
#include "ui/laff_meter_atlas.h"

namespace tt::ui {

namespace {

// The complete hp/max label fits across the eye whites. Keeping it as one centred group prevents
// adjacent two-digit values from reading as one number, and also keeps three-digit values bounded.
constexpr float kLabelMaxW = 0.46f;
constexpr float kLabelGap = 0.006f;
constexpr float kLabelMinScale = 0.34f, kLabelMaxScale = 0.7f;
// LaffMeter.fractions: tooth i shows while hp > maxHp * fraction (teeth listed 6 .. 1).
constexpr float kToothFractions[6] = {0.0f, 0.166666f, 0.333333f, 0.5f, 0.666666f, 0.833333f};
constexpr LaffPart kTeeth[6] = {kLaffTooth6, kLaffTooth5, kLaffTooth4, kLaffTooth3, kLaffTooth2, kLaffTooth1};
// LaffMeter.deathColor: the head at 0 laff.
constexpr float kDeathColor[3] = {0.58039216f, 0.80392157f, 0.34117647f};

constexpr float kPulseChange = 0.333f; // ToontownIntervals.PULSE_GUI_CHANGE
constexpr float kPulseSeconds = 0.2f;  // PULSE_GUI_DURATION, each half

render::Model gAtlas;
bool gTried = false;

} // namespace

// LaffMeter: the species head in the toon's head colour; at 0 laff the frown on deathColor, at
// full laff the smile with eyes, otherwise the open smile, eyes and adjustTeeth's teeth; the
// laff and max numbers sit in the eyes (hpLabel / maxLabel).
bool drawLaffMeter(const LaffFace& v, float x0, float y0, float size, const C2D_Text* hpText,
                   const C2D_Text* slashText, const C2D_Text* maxText, float z) {
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
    // Labels: upstream text_scale 0.4 is only ~6 px at HUD size (kLaffLabelHeight), so draw the
    // complete hp/max label as large as the eye whites allow, then shrink it as one unit to fit.
    if (!hpText || !slashText || !maxText) return true;
    float scale = kLabelMaxScale * size / 72.0f;
    if (scale < kLabelMinScale) scale = kLabelMinScale;
    float hpW = 0.0f, hpH = 0.0f, slashW = 0.0f, slashH = 0.0f, maxW = 0.0f, maxH = 0.0f;
    C2D_TextGetDimensions(hpText, scale, scale, &hpW, &hpH);
    C2D_TextGetDimensions(slashText, scale, scale, &slashW, &slashH);
    C2D_TextGetDimensions(maxText, scale, scale, &maxW, &maxH);
    const float gap = kLabelGap * size;
    float totalW = hpW + slashW + maxW + gap * 2.0f;
    const float fitW = kLabelMaxW * size;
    if (totalW > fitW && totalW > 0.0f) {
        scale *= fitW / totalW;
        C2D_TextGetDimensions(hpText, scale, scale, &hpW, &hpH);
        C2D_TextGetDimensions(slashText, scale, scale, &slashW, &slashH);
        C2D_TextGetDimensions(maxText, scale, scale, &maxW, &maxH);
        totalW = hpW + slashW + maxW + gap * 2.0f;
    }
    const u32 ink = C2D_Color32(0x10, 0x10, 0x10, 0xFF);
    const u32 rim = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
    const float labelCx = x0 + (kLaffHpLabel[0] + kLaffMaxLabel[0]) * 0.5f * size;
    const float labelCy = y0 + (kLaffHpLabel[1] + kLaffMaxLabel[1]) * 0.5f * size;
    float x = labelCx - totalW * 0.5f;
    drawOutlinedText(*hpText, x, labelCy - hpH * 0.5f, z, scale, ink, rim);
    x += hpW + gap;
    const float slashY = labelCy - slashH * 0.5f;
    drawOutlinedText(*slashText, x, slashY, z, scale, ink, rim);
    // ImpressBT's slash has a one-pixel diagonal at pocket size. Reinforce its fill inside the
    // existing glyph advance so three-digit labels read as hp/max rather than hp,max.
    C2D_DrawText(slashText, C2D_WithColor | C2D_AlignLeft, x + 0.55f, slashY, z + 0.002f,
                 scale, scale, ink);
    x += slashW + gap;
    drawOutlinedText(*maxText, x, labelCy - maxH * 0.5f, z, scale, ink, rim);
    return true;
}

float LaffPulse::update(int hp) {
    if (lastHp_ >= 0 && hp != lastHp_) {
        change_ = hp > lastHp_ ? 1.0f + kPulseChange : 1.0f - kPulseChange;
        startMs_ = osGetTime();
    }
    lastHp_ = hp;
    if (!startMs_) return 1.0f;
    // Scale to change_ (easeOut), then back to 1 (easeIn), 0.2 s each.
    const float t = (osGetTime() - startMs_) / 1000.0f;
    if (t >= 2 * kPulseSeconds) return 1.0f;
    if (t < kPulseSeconds) {
        const float u = t / kPulseSeconds;
        return 1.0f + (change_ - 1.0f) * (1.0f - (1.0f - u) * (1.0f - u));
    }
    const float u = (t - kPulseSeconds) / kPulseSeconds;
    return change_ + (1.0f - change_) * (u * u);
}

bool LaffDisplay::init() {
    buf_ = C2D_TextBufNew(16);
    return buf_ != nullptr;
}

void LaffDisplay::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    buf_ = nullptr;
}

void LaffDisplay::draw(const LaffFace& face, float cx, float cy, float size, float z) {
    if (!buf_) return;
    const int hp = face.hp > 0 ? face.hp : 0; // LaffMeter never shows a negative laff
    if (hp != hpShown_ || face.maxHp != maxShown_) {
        char n[12];
        C2D_TextBufClear(buf_);
        snprintf(n, sizeof n, "%d", hp);
        render::textParse(&hp_, buf_, n);
        C2D_TextOptimize(&hp_);
        render::textParse(&slash_, buf_, "/");
        C2D_TextOptimize(&slash_);
        snprintf(n, sizeof n, "%d", face.maxHp);
        render::textParse(&max_, buf_, n);
        C2D_TextOptimize(&max_);
        hpShown_ = hp;
        maxShown_ = face.maxHp;
    }
    const float s = size * pulse_.update(face.hp);
    drawLaffMeter(face, cx - s * 0.5f, cy - s * 0.5f, s, &hp_, &slash_, &max_, z);
}

} // namespace tt::ui
