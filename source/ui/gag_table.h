// The gag inventory table shared by the Gags tab and the Shtiker Book's Gags page
// (upstream InventoryPage / InventoryNew): one row per track in its battle colour, a cell
// per unlocked gag level with its icon and count, and the experience toward the next gag.
#pragma once

#include <cstdint>

#include "ui/page_text.h"

namespace tt::ui {

struct GagTableData {
    const uint8_t (*count)[7] = nullptr; // [track][level]
    const uint16_t* exp = nullptr;       // [track]
    const uint8_t* access = nullptr;     // [track]
    int trainingTrack = -1;              // TrackProgress id (the track being trained), or -1
    uint32_t trainingBits = 0;           // trackProgress: bit i = TrackPage frame i+1 earned (15 frames)
};

// Rows of rowH px from (x, y), w wide. Counts and the experience column are cached text.
void drawGagTable(PageText& text, const GagTableData& d, float x, float y, float w, float rowH);

} // namespace tt::ui
