#include "ui/fishing_panel.h"

#include <cstdio>

namespace tt::ui {

namespace {

const u32 kPanelBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kPanelFill = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
const u32 kAccentCol = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhiteCol = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kDimCol = C2D_Color32(0xA8, 0xB4, 0xCC, 0xFF);
const u32 kGreen = C2D_Color32(0x5C, 0xD0, 0x5C, 0xFF);
const u32 kYellow = C2D_Color32(0xF0, 0xE0, 0x30, 0xFF);
const u32 kRed = C2D_Color32(0xE8, 0x50, 0x40, 0xFF);

// Line slots in lines_.
enum Line : int {
    kLTitle, kLPractice, kLBeans, kLTank, kLStatus, kLPowerLabel, kLAimLabel, kLCaught, kLItem, kLWeight, kLValue,
    kLHint, kLCount
};
static_assert(kLCount <= 12, "FishingPanel::kMaxLines");

constexpr float kBarX = 56.0f, kBarW = 248.0f, kBarH = 14.0f;
constexpr float kPowerY = 82.0f, kAimY = 106.0f;

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
        C2D_TextParse(&lines_[slot], buf_, s);
        C2D_TextOptimize(&lines_[slot]);
    };
    put(kLTitle, "Fishing");
    put(kLPractice, v.practice ? "Practice mode" : "");
    if (v.beans >= 0) snprintf(tmp, sizeof tmp, "Jellybeans: %d", v.beans);
    else snprintf(tmp, sizeof tmp, "Jellybeans: -");
    put(kLBeans, tmp);
    snprintf(tmp, sizeof tmp, "Bucket: %d/%d", v.tank, v.tankMax);
    put(kLTank, tmp);
    put(kLStatus, v.status);
    put(kLPowerLabel, "Power");
    put(kLAimLabel, "Aim");
    put(kLCaught, v.result == FishingView::kFish ? (v.newEntry ? "You caught: (new!)" : "You caught:") : "");
    put(kLItem, v.itemName);
    put(kLWeight, v.itemWeight);
    put(kLValue, v.itemValue);
    put(kLHint, "Hold A: charge   Release: cast   Pad: aim   B: leave");
    numLines_ = kLCount;
    revision_ = v.revision;
    lastTank_ = v.tank;
    lastTankMax_ = v.tankMax;
    lastBeans_ = v.beans;
    lastRod_ = v.rod;
    lastPractice_ = v.practice;
    lastResult_ = v.result;
}

void FishingPanel::draw(const FishingView& v, float contentHeight) {
    if (!buf_) return;
    if (v.revision != revision_ || v.tank != lastTank_ || v.tankMax != lastTankMax_ || v.beans != lastBeans_ ||
        v.rod != lastRod_ || v.practice != lastPractice_ || v.result != lastResult_) {
        reparse(v);
    }
    auto text = [&](int slot, float x, float y, float scale, u32 color) {
        C2D_DrawText(&lines_[slot], C2D_WithColor, x, y, 0.5f, scale, scale, color);
    };
    text(kLTitle, 8, 4, 0.6f, kWhiteCol);
    if (v.practice) text(kLPractice, 200, 8, 0.45f, kAccentCol);
    text(kLBeans, 8, 28, 0.45f, kDimCol);
    text(kLTank, 190, 28, 0.45f, kDimCol);
    text(kLStatus, 8, 48, 0.5f, kAccentCol);

    // Waiting for a bite: how much of the cast timeout is used (thin bar under the status).
    if (v.phase == FishingView::kWaiting) {
        C2D_DrawRectSolid(8, 70, 0.5f, 304, 3, kPanelFill);
        C2D_DrawRectSolid(8, 70, 0.55f, 304.0f * (1.0f - v.timeoutFrac), 3, kDimCol);
    }

    // Power bar (hold A) and aim gauge (circle pad), like the cast arrow of fishingGui.
    text(kLPowerLabel, 8, kPowerY - 1, 0.45f, kWhiteCol);
    C2D_DrawRectSolid(kBarX, kPowerY, 0.5f, kBarW, kBarH, kPanelFill);
    const float p = v.power < 0.0f ? 0.0f : (v.power > 1.0f ? 1.0f : v.power);
    const u32 pc = p < 0.4f ? kGreen : (p < 0.75f ? kYellow : kRed);
    C2D_DrawRectSolid(kBarX, kPowerY, 0.55f, kBarW * p, kBarH, pc);
    text(kLAimLabel, 8, kAimY - 1, 0.45f, kWhiteCol);
    C2D_DrawRectSolid(kBarX, kAimY, 0.5f, kBarW, kBarH, kPanelFill);
    C2D_DrawRectSolid(kBarX + kBarW * 0.5f - 1, kAimY, 0.55f, 2, kBarH, kDimCol);
    const float a = v.aimMax > 0.0f ? v.aim / v.aimMax : 0.0f; // +1 = full left
    const float mx = kBarX + kBarW * 0.5f - a * (kBarW * 0.5f - 4.0f);
    C2D_DrawRectSolid(mx - 4, kAimY - 2, 0.6f, 8, kBarH + 4, kAccentCol);

    // Result card.
    if (v.result != FishingView::kNone) {
        const float y = 132.0f, h = 54.0f;
        C2D_DrawRectSolid(8, y, 0.5f, 304, h, kAccentCol);
        C2D_DrawRectSolid(10, y + 2, 0.52f, 300, h - 4, kPanelFill);
        if (v.result == FishingView::kFish) {
            text(kLCaught, 16, y + 4, 0.42f, kDimCol);
            text(kLItem, 16, y + 18, 0.55f, kWhiteCol);
            text(kLWeight, 16, y + 36, 0.42f, kDimCol);
            text(kLValue, 160, y + 36, 0.42f, kAccentCol);
        } else {
            text(kLItem, 16, y + 18, 0.55f, kWhiteCol);
            text(kLValue, 16, y + 36, 0.42f, kAccentCol);
        }
    }
    text(kLHint, 8, contentHeight - 18, 0.38f, kDimCol);
}

} // namespace tt::ui
