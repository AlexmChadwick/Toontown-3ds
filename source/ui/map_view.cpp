#include "ui/map_view.h"

#include <cmath>
#include <cstdio>
#include <cstring>

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
// Legend rows beside the map, in drawing order; "You" is always shown.
struct LegendRow {
    u32 color;
    const char* label;
};
constexpr LegendRow kLegend[] = {{0, "You"},          {kMarkToon, "Toons"},     {kMarkHq, "Toon HQ"},
                                 {kMarkShop, "Shops"}, {kMarkTrolley, "Trolley"}, {kMarkFishing, "Fishing"},
                                 {kMarkTunnel, "Tunnels"}};
constexpr int kLegendRows = sizeof kLegend / sizeof kLegend[0];
C2D_Text gLegend[kLegendRows];
render::TextCache gCache;

void initText() {
    if (gStatic) return;
    gStatic = C2D_TextBufNew(192);
    gDynamic = C2D_TextBufNew(256);
    render::textParse(&gHome, gStatic, "Go Home");
    C2D_TextOptimize(&gHome);
    for (int i = 0; i < kNumTeleportHoods; i++) {
        render::textParse(&gHoods[i], gStatic, kHoodShort[i]);
        C2D_TextOptimize(&gHoods[i]);
    }
    for (int i = 0; i < kLegendRows; i++) {
        render::textParse(&gLegend[i], gStatic, kLegend[i].label);
        C2D_TextOptimize(&gLegend[i]);
    }
}

void drawArrow(float px, float py, float h, float big) {
    const float fx = -sinf(h), fy = -cosf(h); // screen-space forward
    const float sx = -fy, sy = fx, b = big, t = big * 0.54f, w = big * 0.5f, s = big * 0.77f;
    const float tw = w * 0.69f, tt = t * 0.71f;
    C2D_DrawTriangle(px + fx * b, py + fy * b, theme::kInk, px - fx * t + sx * w, py - fy * t + sy * w, theme::kInk,
                     px - fx * t - sx * w, py - fy * t - sy * w, theme::kInk, 0.6f);
    C2D_DrawTriangle(px + fx * s, py + fy * s, theme::kRed, px - fx * tt + sx * tw, py - fy * tt + sy * tw,
                     theme::kRed, px - fx * tt - sx * tw, py - fy * tt - sy * tw, theme::kRed, 0.6f);
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
    // A legend in the margin the (usually square) map leaves on the right, when it fits.
    constexpr float kLegendW = 66.0f;
    const bool legend = areaW - zw * scale >= kLegendW + 4.0f &&
                        (c.mapOutside || world::placeKind(c.currentZone) != world::PlaceKind::Interior);
    const float ox = legend ? areaX + (areaW - kLegendW - zw * scale) * 0.5f : areaX + (areaW - zw * scale) * 0.5f;
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
    if (rect.marker(&px, &py, 13)) drawArrow(px, py, h, 13.0f);
    if (legend) {
        initText();
        const float lx = areaX + areaW - kLegendW + 2;
        float ly = areaY + 2;
        for (int r = 0; r < kLegendRows; r++) {
            bool present = r == 0;
            for (int i = 0; i < c.numMarkers && !present; i++) present = c.markers[i].color == kLegend[r].color;
            if (!present) continue;
            if (r == 0) {
                drawArrow(lx + 5, ly + 7, 0.0f, 8.0f);
            } else {
                C2D_DrawRectSolid(lx + 1, ly + 3, 0.55f, 8, 8, theme::kInk);
                C2D_DrawRectSolid(lx + 2, ly + 4, 0.55f, 6, 6, kLegend[r].color);
            }
            C2D_DrawText(&gLegend[r], C2D_WithColor, lx + 13, ly, 0.5f, 0.42f, 0.42f, theme::kInk);
            ly += 15.0f;
        }
    }
    // ---- end qafix ----
}

namespace {

// What the last drawHoodControls showed, so a press acts on exactly what the player saw.
struct RowState {
    unsigned avail = 0, teleport = ~0u, visited = ~0u, currentHood = 0;
    bool inPlayground = true, homeAvail = false;
};
RowState gRow;
char gNote[64] = "";
u64 gNoteUntil = 0;
C2D_Text gPlayground, gCogHq;

void note(const char* fmt, const char* name) {
    snprintf(gNote, sizeof gNote, fmt, name ? name : "there");
    gNoteUntil = osGetTime() + 2500;
}

} // namespace

void drawHoodControls(const Context& c, float homeX) {
    using namespace pad::hoods;
    initText();
    if (!gPlayground.buf) {
        render::textParse(&gPlayground, gStatic, "Playground");
        C2D_TextOptimize(&gPlayground);
        render::textParse(&gCogHq, gStatic, "Cog HQ"); // TTLocalizer.MapPageBackToCogHQ
        C2D_TextOptimize(&gCogHq);
    }
    const world::PlaceKind kind = world::placeKind(c.currentZone);
    gRow.avail = c.hoodsAvailable;
    gRow.teleport = c.hoodsTeleport;
    gRow.visited = c.hoodsVisited;
    gRow.currentHood = c.currentHood;
    gRow.inPlayground = !c.currentZone || kind == world::PlaceKind::Playground;
    gRow.homeAvail = c.homeAvailable;
    // Left button: back to the playground from a street or building (MapPage's safeZoneButton),
    // else Go Home, greyed when the server runs no estates.
    const bool back = !gRow.inPlayground && c.currentHood;
    const bool homeOn = back || c.homeAvailable;
    theme::button(homeX, kHomeY, kHomeW, kHomeH, homeOn ? theme::kGreen : theme::kGrey,
                  homeOn ? theme::kGreenDark : theme::kGreyDark);
    theme::textCentered(!back ? gHome : c.currentHood >= 10000 ? gCogHq : gPlayground, homeX + kHomeW * 0.5f, kHomeY + 3, 0.5f,
                        homeOn ? theme::kWhite : theme::kInkSoft, kHomeW - 8);
    // Where we are (the hood / street / building title upstream shows on arrival), or for a
    // moment why a press did nothing.
    const float labelX = homeX + kHomeW + 6, labelW = pad::kScreenW - 4 - labelX;
    const bool noting = gNote[0] && osGetTime() < gNoteUntil;
    theme::paper(labelX, kHomeY, labelW, kHomeH - 3, 5.0f, noting ? theme::kPaperShade : theme::kPaper);
    const bool indoors = kind == world::PlaceKind::Interior;
    const char* where = quest::placeTitle(static_cast<int32_t>(c.currentZone));
    if (!where) where = indoors ? "Indoors" : "Toontown";
    C2D_Text label;
    gCache.parse(&label, gDynamic, noting ? gNote : where, 256);
    theme::textCentered(label, labelX + labelW * 0.5f, noting ? kHomeY + 4 : kHomeY + 2,
                        noting ? 0.42f : 0.5f, noting ? theme::kRedDark : theme::kInk, labelW - 12);
    // Teleport row (MapPage): yellow = here, blue = Go To (teleport access), paper = visited
    // but walk there, grey = not visited yet or not installed.
    for (int i = 0; i < kNumTeleportHoods; i++) {
        const bool have = c.hoodsAvailable & (1u << i);
        const bool here = kTeleportHoods[i] == c.currentHood;
        const bool go = have && (c.hoodsTeleport & (1u << i));
        const bool seen = have && (c.hoodsVisited & (1u << i));
        const float x = i * kHoodW + 2;
        const u32 face = here ? theme::kYellow : go ? theme::kBlue : seen ? theme::kPaper : theme::kGrey;
        const u32 lip = here ? theme::kYellowDark : go ? theme::kBlueDark : seen ? theme::kPaperEdge : theme::kGreyDark;
        theme::button(x, kRowY, kHoodW - 4, kRowH, face, lip);
        theme::textCentered(gHoods[i], x + (kHoodW - 4) * 0.5f, kRowY + 4, 0.48f,
                            here || (seen && !go) ? theme::kInk : go ? theme::kWhite : theme::kInkSoft, kHoodW - 8);
    }
}

void pressHoodControl(int id, State& s) {
    using namespace pad::hoods;
    if (id == kIdHome) {
        if (!gRow.inPlayground && gRow.currentHood) {
            s.teleportRequest = gRow.currentHood; // MapPage.backToSafeZone: no access check
        } else {
            s.homeRequest = true; // main explains when the server has no estates
        }
        return;
    }
    if (id < 0 || id >= kNumTeleportHoods) return;
    const unsigned bit = 1u << id;
    const char* name = quest::placeTitle(static_cast<int32_t>(kTeleportHoods[id]));
    if (kTeleportHoods[id] == gRow.currentHood && gRow.inPlayground) {
        note("You're already in %s!", name);
    } else if (!(gRow.avail & bit)) {
        note("%s isn't installed.", name);
    } else if (!(gRow.teleport & bit)) {
        // MapPage.__buttonCallback ignores the press; say why. ToonTasks grant teleport access.
        note(gRow.visited & bit ? "Walk to %s for now." : "Find %s on foot first!", name);
    } else {
        s.teleportRequest = kTeleportHoods[id];
        s.teleportFromMap = true;
    }
}

int placeMarkers(bool playground, const char* const* names, const float* xy, int count, MapMarker* out, int max) {
    int n = 0;
    for (int i = 0; i < count && n < max; i++) {
        const char* name = names[i];
        const size_t len = strlen(name);
        const bool origin = len > 7 && !strcmp(name + len - 7, "_origin"); // door / tunnel exit points
        u32 color = 0;
        if (!strncmp(name, "linktunnel_", 11) && !origin) {
            color = kMarkTunnel;
        } else if (!strcmp(name, "trolley_station")) {
            color = kMarkTrolley;
        } else if (!strcmp(name, "fishing_spot_DNARoot")) {
            color = kMarkFishing;
        } else if (strstr(name, ":toon_landmark_") && !origin) {
            // Roots only ("sz19:toon_landmark_TT_gag_shop_DNARoot"); HQs are baked as door or
            // decal nodes only ("tb71:toon_landmark_hqTT_DNARoot/door_0").
            const char* child = strchr(name, '/');
            if (strstr(name, "_hq")) color = kMarkHq; // door / decal nodes; merged below
            else if (playground && !child) color = kMarkShop;
        }
        if (!color) continue;
        const float x = xy[2 * i], y = xy[2 * i + 1];
        bool dup = false; // one marker per dock (four spots each) and per building (HQ: two doors)
        for (int j = 0; j < n && !dup; j++) {
            const float dx = out[j].x - x, dy = out[j].y - y;
            dup = out[j].color == color && dx * dx + dy * dy < 30.0f * 30.0f;
        }
        if (!dup) out[n++] = {x, y, color};
    }
    return n;
}

} // namespace tt::ui
