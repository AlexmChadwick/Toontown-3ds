#include "ui/map_view.h"

#include <cmath>

#include "quest/quest_text.h"
#include "render/text_cache.h"
#include "render/ui.h"
#include "render/ui_font.h"
#include "ui/pad_tables.h"
#include "ui/panel_geometry.h"
#include "ui/theme.h"
#include "world/place.h"

namespace tt::ui {

namespace {

const char* const kHoodShort[kNumTeleportHoods] = {"TTC", "DD", "DG", "MM", "BR", "DL", "AA", "GS"};
static_assert(pad::hoods::kCount == kNumTeleportHoods, "one focus item per teleport hood");

C2D_TextBuf gStatic = nullptr, gDynamic = nullptr;
C2D_Text gHome, gHoods[kNumTeleportHoods];
render::TextCache gCache;

void initText() {
    if (gStatic) return;
    gStatic = C2D_TextBufNew(64);
    gDynamic = C2D_TextBufNew(256);
    render::textParse(&gHome, gStatic, "Go Home");
    C2D_TextOptimize(&gHome);
    for (int i = 0; i < kNumTeleportHoods; i++) {
        render::textParse(&gHoods[i], gStatic, kHoodShort[i]);
        C2D_TextOptimize(&gHoods[i]);
    }
}

} // namespace

void drawMapFrame(const Context& c, const MapSource& map, float fx0, float fy0, float fw, float fh) {
    // Fit the map (baked image rectangle, else zone bounds) inside the frame, preserving
    // aspect. Map +Y is up.
    theme::paper(fx0, fy0, fw, fh, 6.0f, theme::kPaperShade);
    const float areaX = fx0 + 5, areaY = fy0 + 5, areaW = fw - 10, areaH = fh - 10;
    const bool hasMap = map.image && map.w && map.h;
    const float minX = hasMap ? map.minX : c.zoneMinX, maxX = hasMap ? map.maxX : c.zoneMaxX;
    const float minY = hasMap ? map.minY : c.zoneMinY, maxY = hasMap ? map.maxY : c.zoneMaxY;
    const float zw = fmaxf(1.0f, maxX - minX), zh = fmaxf(1.0f, maxY - minY);
    const float scale = hasMap ? fminf(areaW / map.w, areaH / map.h) * map.w / zw : fminf(areaW / zw, areaH / zh);
    const float ox = areaX + (areaW - zw * scale) * 0.5f;
    const float oy = areaY + (areaH - zh * scale) * 0.5f;
    auto mx = [&](float x) { return ox + (x - minX) * scale; };
    auto my = [&](float y) { return oy + (maxY - y) * scale; };

    const bool indoors = !hasMap && world::placeKind(c.currentZone) == world::PlaceKind::Interior;
    if (hasMap) {
        C2D_DrawImageAt(*map.image, ox, oy, 0.5f, nullptr, zw * scale / map.w, zh * scale / map.h);
    } else if (indoors) {
        // No baked map indoors (upstream has none either): a plain floor plan of the room,
        // so the markers still read as "who is where" instead of sitting on grass.
        C2D_DrawRectSolid(ox, oy, 0.5f, zw * scale, zh * scale, C2D_Color32(0x8A, 0x63, 0x3E, 0xFF));
        C2D_DrawRectSolid(ox + 2, oy + 2, 0.5f, zw * scale - 4, zh * scale - 4, C2D_Color32(0xB9, 0x8A, 0x5A, 0xFF));
    } else {
        C2D_DrawRectSolid(ox, oy, 0.5f, zw * scale, zh * scale, C2D_Color32(0x4E, 0x9A, 0x3C, 0xFF));
    }
    // ---- qafix: all marker geometry stays inside the map, above Home ----
    const MapRect rect{ox, oy, zw * scale, zh * scale};
    // Squares, not C2D_DrawCircleSolid: citro2d circles use the procedural-texture
    // unit and did not render after the 3D pass in Azahar (unverified on hardware).
    for (int i = 0; i < c.numMarkers; i++) {
        float x = mx(c.markers[i].x), y = my(c.markers[i].y);
        if (rect.marker(&x, &y, 4)) {
            C2D_DrawRectSolid(x - 4, y - 4, 0.55f, 8, 8, theme::kInk);
            C2D_DrawRectSolid(x - 3, y - 3, 0.55f, 6, 6, c.markers[i].color);
        }
    }
    // Player arrow (heading 0 = +Y = up on the map; positive heading turns left), outlined in
    // ink so it reads on grass, water and paths alike.
    const float h = c.playerHeadingDeg * 3.14159265f / 180.0f;
    float px = mx(c.playerX), py = my(c.playerY);
    if (rect.marker(&px, &py, 13)) {
        const float fx = -sinf(h), fy = -cosf(h); // screen-space forward
        const float sx = -fy, sy = fx;
        C2D_DrawTriangle(px + fx * 13, py + fy * 13, theme::kInk, px - fx * 7 + sx * 6.5f, py - fy * 7 + sy * 6.5f,
                         theme::kInk, px - fx * 7 - sx * 6.5f, py - fy * 7 - sy * 6.5f, theme::kInk, 0.6f);
        C2D_DrawTriangle(px + fx * 10, py + fy * 10, theme::kRed, px - fx * 5 + sx * 4.5f, py - fy * 5 + sy * 4.5f,
                         theme::kRed, px - fx * 5 - sx * 4.5f, py - fy * 5 - sy * 4.5f, theme::kRed, 0.6f);
    }
    // ---- end qafix ----
}

void drawHoodControls(const Context& c) {
    using namespace pad::hoods;
    initText();
    // Go Home, and where we are (the hood / street / building title upstream shows on arrival).
    theme::button(kHomeX, kHomeY, kHomeW, kHomeH, theme::kGreen, theme::kGreenDark);
    theme::textCentered(gHome, kHomeX + kHomeW * 0.5f, kHomeY + 3, 0.5f, theme::kWhite, kHomeW - 8);
    const float labelX = kHomeX + kHomeW + 6, labelW = pad::kScreenW - 4 - labelX;
    theme::paper(labelX, kHomeY, labelW, kHomeH - 3, 5.0f);
    const bool indoors = world::placeKind(c.currentZone) == world::PlaceKind::Interior;
    const char* where = quest::placeTitle(static_cast<int32_t>(c.currentZone));
    if (!where) where = indoors ? "Indoors" : "Toontown";
    C2D_Text label;
    gCache.parse(&label, gDynamic, where, 256);
    theme::textCentered(label, labelX + labelW * 0.5f, kHomeY + 2, 0.5f, theme::kInk, labelW - 12);
    // Teleport row: tap a playground to go there (installed ones only).
    for (int i = 0; i < kNumTeleportHoods; i++) {
        const bool have = c.hoodsAvailable & (1u << i);
        const bool here = kTeleportHoods[i] == c.currentHood;
        const float x = i * kHoodW + 2;
        const u32 face = here ? theme::kYellow : have ? theme::kBlue : theme::kGrey;
        const u32 lip = here ? theme::kYellowDark : have ? theme::kBlueDark : theme::kGreyDark;
        theme::button(x, kRowY, kHoodW - 4, kRowH, face, lip);
        theme::textCentered(gHoods[i], x + (kHoodW - 4) * 0.5f, kRowY + 4, 0.48f,
                            here ? theme::kInk : have ? theme::kWhite : theme::kInkSoft, kHoodW - 8);
    }
}

} // namespace tt::ui
