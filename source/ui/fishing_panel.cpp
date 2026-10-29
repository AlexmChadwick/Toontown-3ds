#include "render/ui_font.h"
#include "ui/fishing_panel.h"
#include "ui/theme.h"
#include "ui/fishing_gui_atlas.h"
#include "core/log.h"
#include "render/model.h"
#include <cmath>

#include <cstdio>
#include <cstring>

namespace tt::ui {

namespace {

// Paper on the sky (ui/theme.h): ink text, a red-brown status line, grey bar troughs.
const u32 kPanelFill = theme::kGrey;
const u32 kAccentCol = theme::kRedDark;
const u32 kWhiteCol = theme::kInk;
const u32 kDimCol = theme::kInkSoft;
const u32 kGreen = C2D_Color32(0x5C, 0xD0, 0x5C, 0xFF);
const u32 kYellow = C2D_Color32(0xF0, 0xE0, 0x30, 0xFF);
const u32 kRed = C2D_Color32(0xE8, 0x50, 0x40, 0xFF);

// Line slots in lines_.
enum Line : int {
    kLTitle, kLPractice, kLBeans, kLTank, kLStatus, kLPowerLabel, kLAimLabel, kLCaught, kLItem, kLWeight, kLValue,
    kLHint, kLCast, kLExit, kLCount
};
static_assert(kLCount <= 14, "FishingPanel::kMaxLines");


} // namespace

bool FishingPanel::init() {
    buf_ = C2D_TextBufNew(768);
    return buf_ != nullptr;
}

void FishingPanel::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    buf_ = nullptr;
}

void FishingPanel::reparse(const FishingView& v) {
    C2D_TextBufClear(buf_);
    char tmp[80];
    auto put = [&](int slot, const char* s) {
        tt::render::textParse(&lines_[slot], buf_, s);
        C2D_TextOptimize(&lines_[slot]);
    };
    put(kLTitle, "Fishing");
    put(kLPractice, v.practice ? "Practice mode" : "");
    if (v.beans >= 0) snprintf(tmp, sizeof tmp, "%d", v.beans);
    else snprintf(tmp, sizeof tmp, "-");
    put(kLBeans, tmp);
    snprintf(tmp, sizeof tmp, "%d/%d", v.tank, v.tankMax);
    put(kLTank, tmp);
    put(kLStatus, v.status);
    put(kLPowerLabel, "Power");
    put(kLAimLabel, "Aim");
    put(kLCaught, v.result == FishingView::kFish ? (v.newEntry ? "You caught: (new!)" : "You caught:") : "");
    put(kLItem, v.itemName);
    put(kLWeight, v.itemWeight);
    put(kLValue, v.itemValue);
    put(kLHint, "Drag down from Cast, or hold A and aim with the pad.");
    put(kLCast, "Cast"); // TTLocalizer.FishingCast
    put(kLExit, "Exit"); // TTLocalizer.FishingExit
    numLines_ = kLCount;
    revision_ = v.revision;
    lastTank_ = v.tank;
    lastTankMax_ = v.tankMax;
    lastBeans_ = v.beans;
    lastRod_ = v.rod;
    lastPractice_ = v.practice;
    lastResult_ = v.result;
    lastPhase_ = v.phase;
    snprintf(lastStatus_, sizeof(lastStatus_), "%s", v.status);
}

namespace {

render::Model gGui;
bool gGuiTried = false;

// Layout: fishingGui at 80 px per aspect2d unit across the 320 px screen (its 4-unit boards fill
// the width), boards on the bottom edge. The Cast button is drawn larger for a fingertip, and the
// arrow at 1.4x the gui scale so a full-power tail ends near the boards.
constexpr float kUnit = 80.0f, kBoardsBottomZ = -1.5368f;
constexpr float kBtnCx = 160.0f, kBtnCy = 118.0f, kBtnSize = 56.0f;
constexpr float kArrowUnit = kUnit * 1.4f;
// FishingSpot.localAdjustingCastTask: a drag of 0.5 units (a quarter of the screen height) is full power.
constexpr float kFullDrag = 60.0f;
constexpr float kAngleMax = 50.0f; // FishGlobals.FishingAngleMax (fish_logic.h kFishingAngleMax)

float gx(float x) { return 160.0f + x * kUnit; }
float gy(float z) { return 240.0f - (z - kBoardsBottomZ) * kUnit; }

bool inCircle(const touchPosition& t, float cx, float cy, float r) {
    const float dx = t.px - cx, dy = t.py - cy;
    return dx * dx + dy * dy <= r * r;
}

constexpr float kExitCx = 296.0f, kExitCy = 182.0f, kExitSize = 34.0f;

} // namespace

void FishingPanel::drawPart(int part, float x, float y, float w, float h, const C2D_ImageTint* tint, float z) {
    C3D_Tex* tex = gGui.loaded() ? gGui.texture(0) : nullptr;
    if (!tex) return;
    const FishGuiRect& r = kFishGuiRects[part];
    Tex3DS_SubTexture sub{uint16_t(r.w), uint16_t(r.h), r.x / kFishGuiAtlas, 1.0f - r.y / kFishGuiAtlas,
                          (r.x + r.w) / kFishGuiAtlas, 1.0f - (r.y + r.h) / kFishGuiAtlas};
    C2D_Image img{tex, &sub};
    C2D_DrawImageAt(img, x, y, z, tint, w / r.w, h / r.h);
}

void FishingPanel::drawPartRotated(int part, float cx, float cy, float w, float h, float rad, const C2D_ImageTint* tint) {
    C3D_Tex* tex = gGui.loaded() ? gGui.texture(0) : nullptr;
    if (!tex) return;
    const FishGuiRect& r = kFishGuiRects[part];
    Tex3DS_SubTexture sub{uint16_t(r.w), uint16_t(r.h), r.x / kFishGuiAtlas, 1.0f - r.y / kFishGuiAtlas,
                          (r.x + r.w) / kFishGuiAtlas, 1.0f - (r.y + r.h) / kFishGuiAtlas};
    C2D_Image img{tex, &sub};
    C2D_DrawImageAtRotated(img, cx, cy, 0.56f, rad, tint, w / r.w, h / r.h);
}

u32 FishingPanel::handleInput(u32 down, u32 held, const touchPosition& t) {
    if ((down & KEY_TOUCH) && inCircle(t, kBtnCx, kBtnCy, kBtnSize * 0.6f)) {
        touch_.started = touch_.dragging = true;
        touch_.power = touch_.angle = 0.0f;
        touchX0_ = t.px, touchY0_ = t.py;
        TT_LOG("fishing: cast button pressed");
    } else if ((down & KEY_TOUCH) && inCircle(t, kExitCx, kExitCy, kExitSize * 0.6f)) {
        exitDown_ = true;
    }
    if (touch_.dragging && (held & KEY_TOUCH)) {
        // localAdjustingCastTask with screen y down: only a drag downward has power.
        const float dx = t.px - touchX0_, dy = t.py - touchY0_;
        if (dy <= 0.0f) {
            touch_.power = 0.0f;
            touch_.angle = 0.0f;
        } else {
            const float dist = sqrtf(dx * dx + dy * dy);
            touch_.power = dist / kFullDrag > 1.0f ? 1.0f : dist / kFullDrag;
            float angle = atanf(dx / -dy) * 57.29578f; // rad2Deg(atan(deltaX / deltaY)), deltaY up
            if (touch_.power < 0.25f) angle *= powf(touch_.power * 4.0f, 3.0f);
            touch_.angle = angle < -kAngleMax ? -kAngleMax : (angle > kAngleMax ? kAngleMax : angle);
        }
    } else if (touch_.dragging) {
        touch_.dragging = false;
        touch_.released = true;
        TT_LOG("fishing: cast released power %.2f angle %.0f", double(touch_.power), double(touch_.angle));
    }
    if (exitDown_ && !(held & KEY_TOUCH)) {
        exitDown_ = false;
        touch_.exit = true; // released: the press started on the button (no drag-off on 3DS)
        TT_LOG("fishing: exit button");
    }
    return down & KEY_TOUCH;
}

void FishingPanel::draw(const FishingView& v, float contentHeight) {
    if (!buf_) return;
    if (!gGuiTried) {
        gGuiTried = true;
        if (!gGui.load("romfs:/baked/fishing_gui.ttm")) TT_LOG("fishing: no fishing_gui.ttm");
    }
    if (v.revision != revision_ || v.tank != lastTank_ || v.tankMax != lastTankMax_ || v.beans != lastBeans_ ||
        v.rod != lastRod_ || v.practice != lastPractice_ || v.result != lastResult_ || v.phase != lastPhase_ || strcmp(v.status, lastStatus_)) {
        reparse(v);
    }
    auto text = [&](int slot, float x, float y, float scale, u32 color) {
        C2D_DrawText(&lines_[slot], C2D_WithColor, x, y, 0.6f, scale, scale, color);
    };
    auto centered = [&](int slot, float cx, float y, float scale, u32 color) {
        float w = 0, h = 0;
        C2D_TextGetDimensions(&lines_[slot], scale, scale, &w, &h);
        C2D_DrawText(&lines_[slot], C2D_WithColor, cx - w * 0.5f, y, 0.6f, scale, scale, color);
    };
    // The pond behind the dock: sky over water.
    theme::sky(320.0f, contentHeight);
    C2D_DrawRectSolid(0, 150, 0.4f, 320, contentHeight - 150, C2D_Color32(0x3C, 0x8C, 0xC8, 0xFF));
    theme::paper(4, 2, 312, 40, 8.0f);
    text(kLTitle, 12, 4, 0.55f, theme::kBlueDark);
    if (v.practice) {
        theme::roundRect(196, 5, 112, 17, 6, theme::kYellow);
        text(kLPractice, 204, 5, 0.42f, kWhiteCol);
    }
    text(kLStatus, 12, 22, 0.46f, kAccentCol);
    // Waiting for a bite: how much of the cast timeout is used (thin bar under the status).
    if (v.phase == FishingView::kWaiting) {
        C2D_DrawRectSolid(10, 38, 0.5f, 300, 2, kPanelFill);
        C2D_DrawRectSolid(10, 38, 0.55f, 300.0f * (1.0f - v.timeoutFrac), 2, kDimCol);
    }

    // FishingItemFound: "You caught:" with the item, under the title.
    if (v.result != FishingView::kNone) {
        const float y = 46.0f, h = 46.0f;
        theme::paper(10, y, 300, h, 6.0f, theme::kWhite, theme::kBlue);
        if (v.result == FishingView::kFish) {
            text(kLCaught, 16, y + 2, 0.40f, kDimCol);
            text(kLItem, 16, y + 14, 0.52f, kWhiteCol);
            text(kLWeight, 16, y + 30, 0.40f, kDimCol);
            text(kLValue, 160, y + 30, 0.40f, kAccentCol);
        } else {
            text(kLItem, 16, y + 8, 0.52f, kWhiteCol);
            text(kLValue, 16, y + 26, 0.40f, kAccentCol);
        }
    }

    if (!gGui.loaded()) { // the art is missing: the old text hint at least
        text(kLHint, 12, contentHeight - 22, 0.4f, kDimCol);
        return;
    }
    // The dock: boards, the jellybean jar (left) and the fish bucket (right) with their counts.
    const FishGuiRect& b = kFishGuiRects[kFishGuiBoards];
    drawPart(kFishGuiBoards, gx(b.minX), gy(b.maxZ), (b.maxX - b.minX) * kUnit, (b.maxZ - b.minZ) * kUnit);
    auto onDock = [&](int part, int display, int label, float k) {
        const FishGuiRect& r = kFishGuiRects[part];
        const float w = (r.maxX - r.minX) * kUnit * k, h = (r.maxZ - r.minZ) * kUnit * k;
        const float cx = gx((r.minX + r.maxX) * 0.5f), bottom = gy(r.minZ);
        drawPart(part, cx - w * 0.5f, bottom - h, w, h);
        // The label sits on the part's display (its centre, scaled about the part's bottom).
        const FishGuiRect& d = kFishGuiRects[display];
        const float dcx = cx + (gx((d.minX + d.maxX) * 0.5f) - cx) * k;
        const float dcy = bottom + (gy((d.minZ + d.maxZ) * 0.5f) - bottom) * k;
        centered(label, dcx, dcy - 8.0f, 0.5f, theme::kInk);
    };
    onDock(kFishGuiJar, kFishGuiDisplayJar, kLBeans, 1.25f);
    onDock(kFishGuiBucket, kFishGuiDisplayBucket, kLTank, 1.25f);
    drawPart(exitDown_ ? kFishGuiExitDown : kFishGuiExitUp, kExitCx - kExitSize * 0.5f, kExitCy - kExitSize * 0.5f,
             kExitSize, kExitSize * 0.91f, nullptr, 0.58f);
    text(kLExit, kExitCx - 9, kExitCy + 15, 0.36f, C2D_Color32(0, 0, 0, 0xC0));
    text(kLExit, kExitCx - 10, kExitCy + 14, 0.36f, theme::kWhite);

    // The Cast button: red, green while the cast is being adjusted, pink while it can't be used.
    const bool canCast = v.phase == FishingView::kReady || v.phase == FishingView::kResult ||
                         v.phase == FishingView::kWaiting;
    const bool adjusting = v.phase == FishingView::kCharging;
    C2D_ImageTint tint;
    const u32 btn = adjusting ? C2D_Color32(0x00, 0xFF, 0x00, 0xFF)
                    : canCast ? C2D_Color32(0xFF, 0x00, 0x00, 0xFF)
                              : C2D_Color32(0xCC, 0x80, 0x80, 0xFF);
    C2D_PlainImageTint(&tint, btn, 1.0f);
    C2D_SetTintMode(C2D_TintMult);
    drawPart(kFishGuiCastButton, kBtnCx - kBtnSize * 0.5f, kBtnCy - kBtnSize * 0.5f, kBtnSize, kBtnSize, &tint, 0.55f);

    // The cast arrow (arrowTip under the button, arrowTail stretched by power toward the drag),
    // rolled by the angle; yellow fading to red with power, as setColorScale(1, 1 - p^3, 0.1, 0.7).
    if (adjusting && v.power > 0.0f) {
        const float p = v.power > 1.0f ? 1.0f : v.power;
        const float th = -v.aim * 0.0174533f; // the arrow's roll: the opposite of the cast heading
        const float dx = -sinf(th), dy = cosf(th); // "down" rolled clockwise by th
        const float ox = kBtnCx, oy = kBtnCy + 3.0f;
        const bool clamped = fabsf(v.aim) >= v.aimMax - 0.01f;
        C2D_ImageTint at;
        C2D_PlainImageTint(&at, clamped ? C2D_Color32(0xFF, 0, 0, 0xFF)
                                        : C2D_Color32f(1.0f, 1.0f - p * p * p, 0.1f, 0.7f), 1.0f);
        const FishGuiRect& tip = kFishGuiRects[kFishGuiArrowTip];
        const FishGuiRect& tail = kFishGuiRects[kFishGuiArrowTail];
        const float tipW = (tip.maxX - tip.minX) * kArrowUnit, tipH = (tip.maxZ - tip.minZ) * kArrowUnit;
        const float tailW = (tail.maxX - tail.minX) * kArrowUnit * (0.075f / 0.0799f);
        const float tailTop = 0.111f * kArrowUnit, tailLen = 0.6f * p * kArrowUnit;
        drawPartRotated(kFishGuiArrowTail, ox + dx * (tailTop + tailLen * 0.5f), oy + dy * (tailTop + tailLen * 0.5f),
                        tailW, tailLen, th, &at);
        drawPartRotated(kFishGuiArrowTip, ox + dx * tipH * 0.5f, oy + dy * tipH * 0.5f, tipW, tipH, th, &at);
    }
    C2D_SetTintMode(C2D_TintSolid);
    // White labels with upstream's black text_shadow.
    const u32 shadow = C2D_Color32(0, 0, 0, 0xC0);
    centered(kLCast, kBtnCx + 1, kBtnCy + kBtnSize * 0.5f - 1, 0.5f, shadow);
    centered(kLCast, kBtnCx, kBtnCy + kBtnSize * 0.5f - 2, 0.5f, theme::kWhite);
    centered(kLHint, 161, 161, 0.36f, shadow);
    centered(kLHint, 160, 160, 0.36f, theme::kWhite);
}

} // namespace tt::ui
