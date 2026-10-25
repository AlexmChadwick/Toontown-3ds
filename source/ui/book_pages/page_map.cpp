#include "ui/book_pages/page_map.h"
#include "ui/pad_tables.h"
#include "render/ui.h"
#include <cstdio>
#include <cstdarg>
#include <cmath>

namespace tt::ui {

namespace {

constexpr float kW = 320.0f;
// Shared with the focus table (ui/pad_tables.h), like the Map tab.
constexpr float kHoodRowY = pad::hoods::kRowY, kHoodRowH = pad::hoods::kRowH, kHoodW = pad::hoods::kHoodW;
constexpr float kHomeY = pad::hoods::kHomeY, kHomeH = pad::hoods::kHomeH;
static_assert(pad::hoods::kCount == kNumTeleportHoods, "one focus item per teleport hood");
const char* const kHoodShort[kNumTeleportHoods] = {"TTC", "DD", "DG", "MM", "BR", "DL", "AA", "GS"};

constexpr u32 kBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
constexpr u32 kPanel = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
constexpr u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
constexpr u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
constexpr u32 kDim = C2D_Color32(0x78, 0x70, 0x68, 0xFF);

} // namespace

void PageMap::init() {
    textBuf_ = C2D_TextBufNew(512);
    pad::buildHoodRow(focus_); // padnav
}

void PageMap::shutdown() {
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
}

u32 PageMap::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    if (!bdc.state) return 0;
    // ---- padnav: Home and the hood row (pad::buildHoodRow): the old touch areas, now also
    // reached with the D-Pad and pressed with A. Unavailable hoods stay pressable, as by touch;
    // main.cpp refuses a teleport to a hood that is not installed.
    int activated = -1;
    const u32 used = focus_.handleInput(keysDown, touch, &activated);
    if (activated == pad::hoods::kIdHome) {
        bdc.state->homeRequest = true;
    } else if (activated >= 0 && activated < kNumTeleportHoods) {
        bdc.state->teleportRequest = kTeleportHoods[activated];
    }
    // The page keeps the whole D-Pad (L / R turn pages), even where a row has one control.
    return used | (keysDown & kDpadKeys);
    // ---- end padnav ----
}

void PageMap::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)x; (void)y; (void)w; (void)h;
    if (!textBuf_) init();
    C2D_TextBufClear(textBuf_);

    auto drawText = [&](float tx, float ty, float scale, u32 col, const char* fmt, ...) {
        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        C2D_Text c2d;
        C2D_TextParse(&c2d, textBuf_, buf);
        C2D_DrawText(&c2d, C2D_WithColor, tx, ty, 0.6f, scale, scale, col);
    };

    if (!bdc.ctx) return;
    const Context& c = *bdc.ctx;

    // Fit map into area above Home button
    const float areaX = 4, areaY = 24, areaW = kW - 8, areaH = kHomeY - 26;
    const float minX = bdc.hasMap ? bdc.mapMinX : c.zoneMinX;
    const float maxX = bdc.hasMap ? bdc.mapMaxX : c.zoneMaxX;
    const float minY = bdc.hasMap ? bdc.mapMinY : c.zoneMinY;
    const float maxY = bdc.hasMap ? bdc.mapMaxY : c.zoneMaxY;
    const float zw = (maxX > minX) ? (maxX - minX) : 1.0f;
    const float zh = (maxY > minY) ? (maxY - minY) : 1.0f;
    const float scale = bdc.hasMap ? fminf(areaW / bdc.mapW, areaH / bdc.mapH) * bdc.mapW / zw
                                   : fminf(areaW / zw, areaH / zh);
    const float ox = areaX + (areaW - zw * scale) * 0.5f;
    const float oy = areaY + (areaH - zh * scale) * 0.5f;
    auto mx = [&](float px) { return ox + (px - minX) * scale; };
    auto my = [&](float py) { return oy + (maxY - py) * scale; };

    if (bdc.hasMap && bdc.mapImage) {
        C2D_DrawImageAt(*bdc.mapImage, ox, oy, 0.45f, nullptr, zw * scale / bdc.mapW, zh * scale / bdc.mapH);
    } else {
        C2D_DrawRectSolid(ox, oy, 0.45f, zw * scale, zh * scale, C2D_Color32(0x4E, 0x9A, 0x3C, 0xFF));
    }

    // Markers
    for (int i = 0; i < c.numMarkers; i++) {
        C2D_DrawRectSolid(mx(c.markers[i].x) - 3, my(c.markers[i].y) - 3, 0.55f, 6, 6, c.markers[i].color);
    }

    // Player arrow
    const float ang = c.playerHeadingDeg * 3.14159265f / 180.0f;
    const float px = mx(c.playerX), py = my(c.playerY);
    const float fx = -sinf(ang), fy = -cosf(ang);
    const float sx = -fy, sy = fx;
    C2D_DrawTriangle(px + fx * 11, py + fy * 11, kAccent,
                     px - fx * 5 + sx * 4.5f, py - fy * 5 + sy * 4.5f, kAccent,
                     px - fx * 5 - sx * 4.5f, py - fy * 5 - sy * 4.5f, kAccent, 0.6f);

    drawText(8, 26, 0.42f, kWhite, "Zone %u  (%.0f, %.0f)", c.currentZone, c.playerX, c.playerY);

    // Home button
    C2D_DrawRectSolid(4, kHomeY, 0.5f, 76, kHomeH, kAccent);
    drawText(18, kHomeY + 5, 0.42f, kBg, "Home");

    // Teleport hoods row
    for (int i = 0; i < kNumTeleportHoods; i++) {
        const bool have = c.hoodsAvailable & (1u << i);
        const bool here = kTeleportHoods[i] == c.currentHood;
        const float tx = i * kHoodW + 1;
        C2D_DrawRectSolid(tx, kHoodRowY, 0.5f, kHoodW - 2, kHoodRowH, here ? kAccent : have ? kPanel : kBg);
        drawText(tx + 6, kHoodRowY + 6, 0.42f, here ? kBg : have ? kWhite : kDim, "%s", kHoodShort[i]);
    }
    focus_.drawHighlight(); // padnav
}

} // namespace tt::ui
