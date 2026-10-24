#include "render/ui.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "core/log.h"

namespace tt::ui {

const float kToonColors[kNumToonColors][3] = {
    {1.0f, 1.0f, 1.0f},         {0.96875f, 0.691406f, 0.699219f}, {0.933594f, 0.265625f, 0.28125f},
    {0.863281f, 0.40625f, 0.417969f}, {0.710938f, 0.234375f, 0.4375f}, {0.570312f, 0.449219f, 0.164062f},
    {0.640625f, 0.355469f, 0.269531f}, {0.996094f, 0.695312f, 0.511719f}, {0.832031f, 0.5f, 0.296875f},
    {0.992188f, 0.480469f, 0.167969f}, {0.996094f, 0.898438f, 0.320312f}, {0.996094f, 0.957031f, 0.597656f},
    {0.855469f, 0.933594f, 0.492188f}, {0.550781f, 0.824219f, 0.324219f}, {0.242188f, 0.742188f, 0.515625f},
    {0.304688f, 0.96875f, 0.402344f},  {0.433594f, 0.90625f, 0.835938f}, {0.347656f, 0.820312f, 0.953125f},
    {0.191406f, 0.5625f, 0.773438f},   {0.558594f, 0.589844f, 0.875f},   {0.285156f, 0.328125f, 0.726562f},
    {0.460938f, 0.378906f, 0.824219f}, {0.546875f, 0.28125f, 0.75f},     {0.726562f, 0.472656f, 0.859375f},
    {0.898438f, 0.617188f, 0.90625f},  {0.7f, 0.7f, 0.8f},               {0.3f, 0.3f, 0.35f},
};

namespace {

constexpr float kW = 320.0f;
constexpr float kTabH = 32.0f;
constexpr float kTabY = 240.0f - kTabH;
constexpr float kTabW = kW / kTabCount;
const char* const kTabNames[kTabCount] = {"Map", "Chat", "Toon", "Setup", "Debug"};

const u32 kBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kPanel = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
const u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kDim = C2D_Color32(0xA8, 0xB4, 0xCC, 0xFF);

// Toon tab swatch grid
constexpr float kSwX = 8, kSwY = 44, kSwSize = 30, kSwGap = 4;
constexpr int kSwCols = 9;
// Settings buttons
constexpr float kBtnX = 12, kBtnW = 140, kBtnH = 36;
constexpr float kStereoY = 30, kStressY = 90;

bool inRect(const touchPosition& t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

u32 colorOf(int idx) {
    const float* c = kToonColors[idx];
    return C2D_Color32f(c[0], c[1], c[2], 1.0f);
}

} // namespace

bool Ui::init() {
    textBuf_ = C2D_TextBufNew(1024);
    staticBuf_ = C2D_TextBufNew(64);
    debugBuf_ = C2D_TextBufNew(1024);
    if (!textBuf_ || !staticBuf_ || !debugBuf_ || !chat_.init()) return false;
    for (int i = 0; i < kTabCount; i++) {
        C2D_TextParse(&tabText_[i], staticBuf_, kTabNames[i]);
        C2D_TextOptimize(&tabText_[i]);
    }
    return true;
}

void Ui::shutdown() {
    chat_.shutdown();
    C2D_TextBuf* bufs[] = {&textBuf_, &staticBuf_, &debugBuf_};
    for (C2D_TextBuf* b : bufs) {
        if (*b) C2D_TextBufDelete(*b);
        *b = nullptr;
    }
}

void Ui::debugLine(float x, float y, float scale, u32 color, const char* fmt, ...) {
    if (numDebugLines_ >= kMaxDebugLines) return;
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    CachedLine& l = debugLines_[numDebugLines_++];
    C2D_TextParse(&l.text, debugBuf_, buf);
    C2D_TextOptimize(&l.text);
    l.x = x;
    l.y = y;
    l.scale = scale;
    l.color = color;
}

void Ui::text(float x, float y, float scale, u32 color, const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    C2D_Text t;
    C2D_TextParse(&t, textBuf_, buf);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

u32 Ui::handleInput(u32 down, const touchPosition& t, State& s) {
    return handleInput(down, hidKeysHeld() | (down & KEY_TOUCH), t, s);
}

u32 Ui::handleInput(u32 down, u32 held, const touchPosition& t, State& s) {
    if (down & KEY_DLEFT) s.tab = (s.tab + kTabCount - 1) % kTabCount;
    if (down & KEY_DRIGHT) s.tab = (s.tab + 1) % kTabCount;
    if (down & KEY_X) s.tab = kTabChat;
    if (down & KEY_Y) s.tab = kTabMap;
    if ((down & KEY_TOUCH) && t.py >= kTabY) {
        s.tab = static_cast<int>(t.px / kTabW);
        if (s.tab >= kTabCount) s.tab = kTabCount - 1;
    }
    if (s.tab != lastTab_) {
        chat_.blur(); // the highlight (and A/B capture) never survives a tab change
        lastTab_ = s.tab;
        return 0;
    }
    if (s.tab == kTabChat) return chat_.handleInput(down, held, t, kTabY);
    if (!(down & KEY_TOUCH) || t.py >= kTabY) return 0;

    switch (s.tab) {
    case kTabToon:
        for (int i = 0; i < 3; i++) {
            if (inRect(t, 8 + i * 102, 8, 98, 28)) s.colorTarget = i;
        }
        for (int i = 0; i < kNumToonColors; i++) {
            const float x = kSwX + (i % kSwCols) * (kSwSize + kSwGap);
            const float y = kSwY + (i / kSwCols) * (kSwSize + kSwGap);
            if (inRect(t, x, y, kSwSize, kSwSize)) {
                (s.colorTarget == 0 ? s.bodyColor : s.colorTarget == 1 ? s.shirtColor : s.bottomColor) = i;
            }
        }
        break;
    case kTabSettings:
        if (inRect(t, kBtnX, kStereoY, kBtnW, kBtnH)) s.stereo = !s.stereo;
        if (inRect(t, kBtnX, kStressY, 60, kBtnH) && s.stressToons > 0) s.stressToons -= s.stressToons > 8 ? 8 : 1;
        if (inRect(t, kBtnX + 80, kStressY, 60, kBtnH) && s.stressToons < 64) s.stressToons += s.stressToons >= 8 ? 8 : 1;
        break;
    default:
        break;
    }
    return 0;
}

void Ui::draw(C3D_RenderTarget* target, const State& s, const Context& c) {
    frame_++;
    C2D_TextBufClear(textBuf_);
    C2D_TargetClear(target, kBg);
    C2D_SceneBegin(target);
    switch (s.tab) {
    case kTabMap: drawMap(c); break;
    case kTabChat: chat_.draw(kTabY); break;
    case kTabToon: drawToon(s); break;
    case kTabSettings: drawSettings(s); break;
    default: drawDebug(c); break;
    }
    drawTabs(s);
}

void Ui::drawTabs(const State& s) {
    C2D_DrawRectSolid(0, kTabY, 0.5f, kW, kTabH, kPanel);
    for (int i = 0; i < kTabCount; i++) {
        if (i == s.tab) C2D_DrawRectSolid(i * kTabW + 2, kTabY + 2, 0.5f, kTabW - 4, kTabH - 4, kAccent);
        C2D_DrawText(&tabText_[i], C2D_WithColor, i * kTabW + 10, kTabY + 8, 0.5f, 0.5f, 0.5f,
                     i == s.tab ? kBg : kWhite);
    }
}

void Ui::drawMap(const Context& c) {
    // Fit zone bounds into the content area, preserving aspect. Map +Y is up.
    const float areaX = 8, areaY = 8, areaW = kW - 16, areaH = kTabY - 16;
    const float zw = c.zoneMaxX - c.zoneMinX, zh = c.zoneMaxY - c.zoneMinY;
    const float scale = fminf(areaW / zw, areaH / zh);
    const float ox = areaX + (areaW - zw * scale) * 0.5f;
    const float oy = areaY + (areaH - zh * scale) * 0.5f;
    auto mx = [&](float x) { return ox + (x - c.zoneMinX) * scale; };
    auto my = [&](float y) { return oy + (c.zoneMaxY - y) * scale; };

    C2D_DrawRectSolid(ox, oy, 0.5f, zw * scale, zh * scale, C2D_Color32(0x4E, 0x9A, 0x3C, 0xFF));
    // Squares, not C2D_DrawCircleSolid: citro2d circles use the procedural-texture
    // unit and did not render after the 3D pass in Azahar (unverified on hardware).
    for (int i = 0; i < c.numMarkers; i++) {
        C2D_DrawRectSolid(mx(c.markers[i].x) - 3, my(c.markers[i].y) - 3, 0.55f, 6, 6, c.markers[i].color);
    }
    // Player arrow (heading 0 = +Y = up on the map; positive heading turns left).
    const float h = c.playerHeadingDeg * 3.14159265f / 180.0f;
    const float px = mx(c.playerX), py = my(c.playerY);
    const float fx = -sinf(h), fy = -cosf(h); // screen-space forward
    const float sx = -fy, sy = fx;
    // Long, narrow arrow so the tip is unambiguous at this size.
    C2D_DrawTriangle(px + fx * 11, py + fy * 11, kAccent, px - fx * 5 + sx * 4.5f, py - fy * 5 + sy * 4.5f, kAccent,
                     px - fx * 5 - sx * 4.5f, py - fy * 5 - sy * 4.5f, kAccent, 0.6f);
    text(8, 8, 0.45f, kWhite, "Toontown Central  (%.0f, %.0f)", c.playerX, c.playerY);
}

void Ui::drawToon(const State& s) {
    static const char* const kTargets[3] = {"Body", "Shirt", "Shorts"};
    for (int i = 0; i < 3; i++) {
        C2D_DrawRectSolid(8 + i * 102, 8, 0.5f, 98, 28, i == s.colorTarget ? kAccent : kPanel);
        text(16 + i * 102, 14, 0.5f, i == s.colorTarget ? kBg : kWhite, "%s", kTargets[i]);
    }
    const int sel = s.colorTarget == 0 ? s.bodyColor : s.colorTarget == 1 ? s.shirtColor : s.bottomColor;
    for (int i = 0; i < kNumToonColors; i++) {
        const float x = kSwX + (i % kSwCols) * (kSwSize + kSwGap);
        const float y = kSwY + (i / kSwCols) * (kSwSize + kSwGap);
        if (i == sel) C2D_DrawRectSolid(x - 2, y - 2, 0.5f, kSwSize + 4, kSwSize + 4, kWhite);
        C2D_DrawRectSolid(x, y, 0.5f, kSwSize, kSwSize, colorOf(i));
    }
    text(8, 154, 0.45f, kDim, "Tap a color. Head/arms/legs share the body color.");
    text(8, 172, 0.45f, kDim, "A: jump  B: walk  L/R: orbit camera");
}

void Ui::drawSettings(const State& s) {
    text(12, 8, 0.5f, kWhite, "Settings");
    C2D_DrawRectSolid(kBtnX, kStereoY, 0.5f, kBtnW, kBtnH, s.stereo ? kAccent : kPanel);
    text(kBtnX + 8, kStereoY + 10, 0.5f, s.stereo ? kBg : kWhite, "Stereo 3D: %s", s.stereo ? "On" : "Off");
    text(kBtnX + kBtnW + 12, kStereoY + 10, 0.45f, kDim, "uses the 3D slider");

    C2D_DrawRectSolid(kBtnX, kStressY, 0.5f, 60, kBtnH, kPanel);
    C2D_DrawRectSolid(kBtnX + 80, kStressY, 0.5f, 60, kBtnH, kPanel);
    text(kBtnX + 24, kStressY + 8, 0.7f, kWhite, "-");
    text(kBtnX + 102, kStressY + 8, 0.7f, kWhite, "+");
    text(kBtnX + kBtnW + 12, kStressY + 10, 0.5f, kWhite, "Stress toons: %d", s.stressToons);
    text(12, 140, 0.45f, kDim, "Stress toons are extra copies of the toon,");
    text(12, 156, 0.45f, kDim, "used to measure the Old 3DS draw budget.");
}

void Ui::drawDebug(const Context& c) {
    if (numDebugLines_ == 0 || frame_ % kDebugRefreshFrames == 0) {
        const u32 good = C2D_Color32(0x8C, 0xF0, 0x8C, 0xFF);
        const u32 bad = C2D_Color32(0xFF, 0x80, 0x80, 0xFF);
        C2D_TextBufClear(debugBuf_);
        numDebugLines_ = 0;
        debugLine(8, 4, 0.5f, kWhite, "Debug  (%s)", c.isNew3ds ? "New 3DS" : "Old 3DS");
        debugLine(8, 24, 0.5f, c.fps >= 29.5f ? good : bad, "%.1f fps   cpu %.1f ms  gpu %.1f ms", c.fps, c.cpuMs,
                  c.gpuMs);
        debugLine(8, 42, 0.5f, kWhite, "draws %u  tris %u  models %u (+%u culled)", c.drawCalls, c.triangles,
                  c.modelsDrawn, c.modelsCulled);
        debugLine(8, 60, 0.5f, c.heapUsedKB < c.heapBudgetKB ? kWhite : bad, "heap  %u / %u KB used", c.heapUsedKB,
                  c.heapBudgetKB);
        debugLine(8, 78, 0.5f, kWhite, "linear %u / %u KB free", c.linearFreeKB, c.linearBudgetKB);
        debugLine(8, 96, 0.5f, kWhite, "vram  %u KB free", c.vramFreeKB);
        for (int i = 0; i < 5; i++) debugLine(8, 118 + i * 16, 0.4f, kDim, "%s", log::recent(4 - i));
    }
    for (int i = 0; i < numDebugLines_; i++) {
        const CachedLine& l = debugLines_[i];
        C2D_DrawText(&l.text, C2D_WithColor, l.x, l.y, 0.5f, l.scale, l.scale, l.color);
    }
}

} // namespace tt::ui
