#include "render/ui_font.h"
#include "ui/book_pages/page_map.h"
#include "ui/pad_tables.h"
#include "render/ui.h"
#include "ui/map_view.h"
#include <cstdio>
#include <cstdarg>
#include <cmath>

namespace tt::ui {

namespace {

constexpr float kW = 320.0f;

} // namespace

void PageMap::init() {
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
    // reached with the D-Pad and pressed with A. Hoods without teleport access stay
    // focusable; pressHoodControl explains why they don't teleport.
    int activated = -1;
    const u32 used = focus_.handleInput(keysDown, touch, &activated);
    if (activated >= 0) pressHoodControl(activated, *bdc.state);
    // The page keeps the whole D-Pad (L / R turn pages), even where a row has one control.
    return used | (keysDown & kDpadKeys);
    // ---- end padnav ----
}

void PageMap::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)x; (void)y; (void)w; (void)h;
    if (!bdc.ctx) return;
    MapSource map;
    if (bdc.hasMap && bdc.mapImage) {
        map.image = bdc.mapImage;
        map.minX = bdc.mapMinX;
        map.minY = bdc.mapMinY;
        map.maxX = bdc.mapMaxX;
        map.maxY = bdc.mapMaxY;
        map.w = bdc.mapW;
        map.h = bdc.mapH;
    }
    // The same map and controls as the Map tab, below the book's title bar.
    drawMapFrame(*bdc.ctx, map, 4, 27, kW - 8, pad::hoods::kHomeY - 30);
    drawHoodControls(*bdc.ctx);
    focus_.drawHighlight(); // padnav
}

} // namespace tt::ui
