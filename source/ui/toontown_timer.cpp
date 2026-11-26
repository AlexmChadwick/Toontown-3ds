#include "ui/toontown_timer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "render/model.h"
#include "render/ui_font.h"
#include "ui/timer_clock_atlas.h"

namespace tt::ui {

namespace {

render::Model gClock;
bool gTried = false;

} // namespace

bool ToontownTimer::init() {
    buf_ = C2D_TextBufNew(8);
    return buf_ != nullptr;
}

void ToontownTimer::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    buf_ = nullptr;
}

bool ToontownTimer::draw(int seconds, float cx, float cy, float size, float z) {
    if (!gTried) {
        gTried = true;
        if (!gClock.load("romfs:/baked/timer_clock.ttm")) TT_LOG("ui: no timer_clock.ttm");
    }
    C3D_Tex* tex = gClock.loaded() ? gClock.texture(0) : nullptr;
    if (!tex || !buf_) return false;
    Tex3DS_SubTexture sub{uint16_t(kTimerClockSize), uint16_t(kTimerClockSize), 0.0f, 1.0f, 1.0f, 0.0f};
    C2D_Image img{tex, &sub};
    const float x0 = cx - size * 0.5f, y0 = cy - size * 0.5f;
    C2D_DrawImageAt(img, x0, y0, z, nullptr, size / kTimerClockSize, size / kTimerClockSize);

    // OTPTimer.setTime: bound(time, 0, 999).
    if (seconds < 0) seconds = 0;
    if (seconds > 999) seconds = 999;
    if (seconds != shown_) {
        char s[8];
        snprintf(s, sizeof s, "%d", seconds);
        C2D_TextBufClear(buf_);
        render::textParse(&text_, buf_, s);
        C2D_TextOptimize(&text_);
        shown_ = seconds;
    }
    const int digits = seconds >= 100 ? 3 : seconds >= 10 ? 2 : 1;
    const float* label = kTimerLabel[digits - 1];
    // text_scale is the em height as a fraction of the image; the UI font's em is ~30 px at 1.0.
    const float scale = label[2] * size / 30.0f;
    float w = 0.0f, h = 0.0f;
    C2D_TextGetDimensions(&text_, scale, scale, &w, &h);
    const u32 ink = digits == 1 && seconds <= 5 ? C2D_Color32(0xFF, 0x00, 0x00, 0xFF) : C2D_Color32(0, 0, 0, 0xFF);
    C2D_DrawText(&text_, C2D_WithColor, x0 + label[0] * size - w * 0.5f, y0 + label[1] * size - h * 0.5f, z + 0.01f,
                 scale, scale, ink);
    return true;
}

int splitTimeLeft(const char* line, char* out, size_t outSize) {
    if (!outSize) return -1;
    snprintf(out, outSize, "%s", line ? line : "");
    const char* at = line ? strstr(line, "Time: ") : nullptr;
    if (!at) return -1;
    char* end = nullptr;
    const long secs = strtol(at + 6, &end, 10);
    if (end == at + 6) return -1; // "Time: " without a number
    if (end[0] == ' ' && end[1] == 's') end += 2;
    else if (end[0] == 's') end += 1;
    size_t start = size_t(at - line);
    if (start > 0 && line[start - 1] == '(' && end[0] == ')') { // "(Time: 65 s)"
        start--;
        end++;
    }
    while (start > 0 && line[start - 1] == ' ') start--; // the "  " before it
    const size_t n = start < outSize - 1 ? start : outSize - 1;
    snprintf(out, outSize, "%.*s%s", int(n), line, end);
    return int(secs);
}

} // namespace tt::ui
