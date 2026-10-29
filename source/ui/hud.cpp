#include "ui/hud.h"

#include <cstdio>

#include "core/log.h"

#include "ui/laff_meter_atlas.h"

namespace tt::ui {

namespace {

// Each label may fill this fraction of the cell's width (the eye whites span ~0.30..0.70).
constexpr float kLaffLabelMaxW = 0.19f;
constexpr float kLaffLabelMinScale = 0.34f, kLaffLabelMaxScale = 0.46f;
constexpr float kPulseChange = 0.333f;      // ToontownIntervals.PULSE_GUI_CHANGE
constexpr float kPulseSeconds = 0.2f;       // PULSE_GUI_DURATION, each half
// LaffMeter.fractions: tooth i shows while hp > maxHp * fraction (teeth listed 6 .. 1).
constexpr float kToothFractions[6] = {0.0f, 0.166666f, 0.333333f, 0.5f, 0.666666f, 0.833333f};
constexpr LaffPart kTeeth[6] = {kLaffTooth6, kLaffTooth5, kLaffTooth4, kLaffTooth3, kLaffTooth2, kLaffTooth1};
// LaffMeter.deathColor: the head at 0 laff.
constexpr float kDeathColor[3] = {0.58039216f, 0.80392157f, 0.34117647f};

} // namespace

bool Hud::init() {
    buf_ = C2D_TextBufNew(64);
    raceBuf_ = C2D_TextBufNew(96);
    toastBuf_ = C2D_TextBufNew(160);
    return buf_ != nullptr && raceBuf_ != nullptr && toastBuf_ != nullptr;
}

void Hud::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    if (raceBuf_) C2D_TextBufDelete(raceBuf_);
    if (toastBuf_) C2D_TextBufDelete(toastBuf_);
    buf_ = raceBuf_ = toastBuf_ = nullptr;
}

namespace {

// Photo Fun viewfinder (DistributedPhotoGame's photo_game_viewfinder frame, drawn as flat
// bars): corner brackets around the sight, which stays at the centre of the top screen because
// the tripod camera always looks where the swivel points.
constexpr float kVfLeft = 120.0f, kVfRight = 280.0f, kVfTop = 70.0f, kVfBottom = 170.0f;
constexpr float kVfArm = 22.0f, kVfThick = 3.0f;
constexpr float kVfSightArm = 9.0f;
constexpr float kVfZ = 0.45f;

void drawViewfinder(C3D_RenderTarget* top) {
    C2D_SceneBegin(top);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    const u32 c = C2D_Color32(0xFF, 0xFF, 0xFF, 0xE0);
    const float cx = (kVfLeft + kVfRight) * 0.5f;
    const float cy = (kVfTop + kVfBottom) * 0.5f;
    // Top-left, top-right, bottom-left, bottom-right corners.
    C2D_DrawRectSolid(kVfLeft, kVfTop, kVfZ, kVfArm, kVfThick, c);
    C2D_DrawRectSolid(kVfLeft, kVfTop, kVfZ, kVfThick, kVfArm, c);
    C2D_DrawRectSolid(kVfRight - kVfArm, kVfTop, kVfZ, kVfArm, kVfThick, c);
    C2D_DrawRectSolid(kVfRight - kVfThick, kVfTop, kVfZ, kVfThick, kVfArm, c);
    C2D_DrawRectSolid(kVfLeft, kVfBottom - kVfThick, kVfZ, kVfArm, kVfThick, c);
    C2D_DrawRectSolid(kVfLeft, kVfBottom - kVfArm, kVfZ, kVfThick, kVfArm, c);
    C2D_DrawRectSolid(kVfRight - kVfArm, kVfBottom - kVfThick, kVfZ, kVfArm, kVfThick, c);
    C2D_DrawRectSolid(kVfRight - kVfThick, kVfBottom - kVfArm, kVfZ, kVfThick, kVfArm, c);
    // Sight.
    C2D_DrawRectSolid(cx - kVfSightArm, cy - 0.5f, kVfZ, 2 * kVfSightArm, 1.0f, c);
    C2D_DrawRectSolid(cx - 0.5f, cy - kVfSightArm, kVfZ, 1.0f, 2 * kVfSightArm, c);
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}

} // namespace

void Hud::draw(C3D_RenderTarget* top, const HudValues& v) {
    if (v.toast) {
        if (v.toast != lastToast_) {
            C2D_TextBufClear(toastBuf_);
            C2D_TextParse(&toastText_, toastBuf_, v.toast);
            C2D_TextOptimize(&toastText_);
            lastToast_ = v.toast;
        }
        C2D_SceneBegin(top);
        float w, h;
        C2D_TextGetDimensions(&toastText_, 0.6f, 0.6f, &w, &h);
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        C2D_DrawRectSolid(200 - w * 0.5f - 10, 70, 0.5f, w + 20, h + 12, C2D_Color32(0x10, 0x18, 0x30, 0xD0));
        C2D_DrawText(&toastText_, C2D_WithColor, 200 - w * 0.5f, 76, 0.5f, 0.6f, 0.6f,
                     C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF));
        C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
    }
    if (v.photoViewfinder) drawViewfinder(top);
    if (!v.online) return; // stats come from the server
    // Re-parse text only when the numbers change (citro2d parsing is expensive).
    if (v.hp != lastHp_ && lastHp_ >= 0) {
        // ToontownIntervals.getPulseLargerIval / getPulseSmallerIval (PULSE_GUI_CHANGE 0.333).
        pulseChange_ = v.hp > lastHp_ ? 1.0f + kPulseChange : 1.0f - kPulseChange;
        pulseStartMs_ = osGetTime();
    }
    if (v.hp != lastHp_ || v.maxHp != lastMaxHp_) {
        C2D_TextBufClear(buf_); // hp, max and money live in buf_ (the race line in raceBuf_)
        char s[32];
        snprintf(s, sizeof(s), "%d", v.hp > 0 ? v.hp : 0); // LaffMeter never shows a negative laff
        C2D_TextParse(&hpText_, buf_, s);
        C2D_TextOptimize(&hpText_);
        snprintf(s, sizeof(s), "%d", v.maxHp);
        C2D_TextParse(&maxText_, buf_, s);
        C2D_TextOptimize(&maxText_);
        lastHp_ = v.hp;
        lastMaxHp_ = v.maxHp;
    }
    C2D_SceneBegin(top);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR); // overlay: never hidden by the world
    if (v.racing) {
        C2D_TextBufClear(raceBuf_);
        char race[96];
        snprintf(race, sizeof(race), "SPEED %d   LAP %d/%d   PLACE %d", v.raceSpeed, v.raceLap, v.raceLaps, v.racePlace);
        C2D_TextParse(&raceText_, raceBuf_, race);
        C2D_TextOptimize(&raceText_);
        C2D_DrawText(&raceText_, C2D_WithColor, 42, 16, 0.5f, 0.65f, 0.65f, C2D_Color32(0xFF, 0xF0, 0x70, 0xFF));
    }

    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL); // citro2d's default
}

void Hud::drawBottom(C3D_RenderTarget* bottom, const HudValues& v) {
    if (!v.online) return;
    C2D_SceneBegin(bottom);
    // Upstream anchors the Laff meter at the lower-left. On the touch screen it sits
    // just above the playground row; Map moves Home beside it to preserve both targets.
    drawLaffMeter(v, 4.0f, 104.0f, 72.0f);
}

// LaffMeter: the species head in the toon's head colour; at 0 laff the frown on deathColor, at
// full laff the smile with eyes, otherwise the open smile, eyes and adjustTeeth's teeth; the
// laff and max numbers sit in the eyes (hpLabel / maxLabel).
void Hud::drawLaffMeter(const HudValues& v, float x, float y, float baseSize) {
    if (!laffTried_) {
        laffTried_ = true;
        if (!laffAtlas_.load("romfs:/baked/laff_meter.ttm")) TT_LOG("hud: no laff_meter.ttm");
    }
    C3D_Tex* tex = laffAtlas_.loaded() ? laffAtlas_.texture(0) : nullptr;
    if (!tex) return;
    // The pulse: scale to pulseChange_ (easeOut), then back to 1 (easeIn), 0.2 s each.
    float pulse = 1.0f;
    const float t = (osGetTime() - pulseStartMs_) / 1000.0f;
    if (pulseStartMs_ && t < 2 * kPulseSeconds) {
        if (t < kPulseSeconds) {
            const float u = t / kPulseSeconds;
            pulse = 1.0f + (pulseChange_ - 1.0f) * (1.0f - (1.0f - u) * (1.0f - u));
        } else {
            const float u = (t - kPulseSeconds) / kPulseSeconds;
            pulse = pulseChange_ + (1.0f - pulseChange_) * (u * u);
        }
    }
    const float size = baseSize * pulse;
    // Pulse around the meter's centre, without changing its nominal anchor.
    const float x0 = x - (size - baseSize) * 0.5f;
    const float y0 = y - (size - baseSize) * 0.5f;
    auto part = [&](int index, const C2D_ImageTint* tint) {
        Tex3DS_SubTexture sub;
        sub.width = kLaffCell;
        sub.height = kLaffCell;
        sub.left = kLaffCellXY[index][0] / float(kLaffAtlasW);
        sub.right = (kLaffCellXY[index][0] + kLaffCell) / float(kLaffAtlasW);
        sub.top = 1.0f - kLaffCellXY[index][1] / float(kLaffAtlasH);
        sub.bottom = 1.0f - (kLaffCellXY[index][1] + kLaffCell) / float(kLaffAtlasH);
        C2D_Image img{tex, &sub};
        C2D_DrawImageAt(img, x0, y0, 0.5f, tint, size / kLaffCell, size / kLaffCell);
    };
    const bool dead = v.hp < 1;
    const bool full = v.hp >= v.maxHp;
    const float* rgb = dead ? kDeathColor : v.headRgb;
    C2D_ImageTint headTint;
    C2D_PlainImageTint(&headTint, C2D_Color32f(rgb[0], rgb[1], rgb[2], 1.0f), 1.0f);
    C2D_SetTintMode(C2D_TintMult);
    const int species = v.species >= 0 && v.species < 9 ? v.species : 0;
    part(kLaffDoghead + species, &headTint);
    C2D_SetTintMode(C2D_TintSolid);
    if (dead) {
        part(kLaffFrown, nullptr);
        return;
    }
    part(full ? kLaffSmile : kLaffOpenSmile, nullptr);
    if (!full) {
        for (int i = 0; i < 6; i++) {
            if (v.hp > v.maxHp * kToothFractions[i]) part(kTeeth[i], nullptr);
        }
    }
    part(kLaffEyes, nullptr);
    // Labels: upstream text_scale 0.4 is only ~8 px here, so draw them as large as the eye whites
    // allow. Both labels share one scale, shrunk until the wider one fits its eye.
    float scale = kLaffLabelHeight * size / 30.0f;
    if (scale < kLaffLabelMinScale) scale = kLaffLabelMinScale;
    if (scale > kLaffLabelMaxScale) scale = kLaffLabelMaxScale;
    float w = 0.0f, h = 0.0f, wMax = 0.0f;
    C2D_TextGetDimensions(&hpText_, scale, scale, &w, &h);
    wMax = w;
    C2D_TextGetDimensions(&maxText_, scale, scale, &w, &h);
    if (w > wMax) wMax = w;
    const float fitW = kLaffLabelMaxW * size;
    if (wMax > fitW && wMax > 0.0f) scale *= fitW / wMax;
    const u32 ink = C2D_Color32(0x10, 0x10, 0x10, 0xFF);
    C2D_TextGetDimensions(&hpText_, scale, scale, &w, &h);
    C2D_DrawText(&hpText_, C2D_WithColor, x0 + kLaffHpLabel[0] * size - w * 0.5f,
                 y0 + kLaffHpLabel[1] * size - h * 0.5f, 0.5f, scale, scale, ink);
    C2D_TextGetDimensions(&maxText_, scale, scale, &w, &h);
    C2D_DrawText(&maxText_, C2D_WithColor, x0 + kLaffMaxLabel[0] * size - w * 0.5f,
                 y0 + kLaffMaxLabel[1] * size - h * 0.5f, 0.5f, scale, scale, ink);
}

} // namespace tt::ui
