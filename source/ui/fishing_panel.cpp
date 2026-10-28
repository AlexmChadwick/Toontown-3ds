#include "render/ui_font.h"
#include "ui/fishing_panel.h"
#include "ui/theme.h"

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
        tt::render::textParse(&lines_[slot], buf_, s);
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
    lastPhase_ = v.phase;
    snprintf(lastStatus_, sizeof(lastStatus_), "%s", v.status);
}

void FishingPanel::draw(const FishingView& v, float contentHeight) {
    if (!buf_) return;
    if (v.revision != revision_ || v.tank != lastTank_ || v.tankMax != lastTankMax_ || v.beans != lastBeans_ ||
        v.rod != lastRod_ || v.practice != lastPractice_ || v.result != lastResult_ || v.phase != lastPhase_ || strcmp(v.status, lastStatus_)) {
        reparse(v);
    }
    auto text = [&](int slot, float x, float y, float scale, u32 color) {
        C2D_DrawText(&lines_[slot], C2D_WithColor, x, y, 0.5f, scale, scale, color);
    };
    theme::sky(320.0f, contentHeight);
    theme::paper(4, 2, 312, contentHeight - 4, 8.0f);
    text(kLTitle, 12, 6, 0.6f, theme::kBlueDark);
    if (v.practice) {
        theme::roundRect(196, 6, 112, 18, 6, theme::kYellow);
        text(kLPractice, 204, 7, 0.45f, kWhiteCol);
    }
    text(kLBeans, 12, 28, 0.45f, kDimCol);
    text(kLTank, 190, 28, 0.45f, kDimCol);
    text(kLStatus, 12, 48, 0.5f, kAccentCol);

    // Waiting for a bite: how much of the cast timeout is used (thin bar under the status).
    if (v.phase == FishingView::kWaiting) {
        C2D_DrawRectSolid(8, 70, 0.5f, 304, 3, kPanelFill);
        C2D_DrawRectSolid(8, 70, 0.55f, 304.0f * (1.0f - v.timeoutFrac), 3, kDimCol);
    }

    // Power bar (hold A) and aim gauge (circle pad), like the cast arrow of fishingGui.
    // DistributedFishingSpot hides the cast interface while reeling/entering/leaving.
    if (v.phase == FishingView::kReady || v.phase == FishingView::kCharging || v.phase == FishingView::kResult) {
    text(kLPowerLabel, 8, kPowerY - 1, 0.45f, kWhiteCol);
    theme::roundRect(kBarX, kPowerY, kBarW, kBarH, 4, kPanelFill);
    const float p = v.power < 0.0f ? 0.0f : (v.power > 1.0f ? 1.0f : v.power);
    const u32 pc = p < 0.4f ? kGreen : (p < 0.75f ? kYellow : kRed);
    if (p > 0.0f) theme::roundRect(kBarX, kPowerY, kBarW * p, kBarH, 4, pc, 0.55f);
    text(kLAimLabel, 8, kAimY - 1, 0.45f, kWhiteCol);
    theme::roundRect(kBarX, kAimY, kBarW, kBarH, 4, kPanelFill);
    C2D_DrawRectSolid(kBarX + kBarW * 0.5f - 1, kAimY, 0.55f, 2, kBarH, kDimCol);
    const float a = v.aimMax > 0.0f ? v.aim / v.aimMax : 0.0f; // +1 = full left
    const float mx = kBarX + kBarW * 0.5f - a * (kBarW * 0.5f - 4.0f);
    theme::roundRect(mx - 4, kAimY - 2, 8, kBarH + 4, 3, kAccentCol, 0.6f);

    }

    // Result card.
    if (v.result != FishingView::kNone) {
        const float y = 132.0f, h = 54.0f;
        theme::paper(10, y, 300, h, 6.0f, theme::kWhite, theme::kBlue);
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
    text(kLHint, 12, contentHeight - 22, 0.4f, kDimCol);
}

} // namespace tt::ui
