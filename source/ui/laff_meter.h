// LaffMeter drawing (toontown/toon/LaffMeter.py) shared by the top-screen HUD and the bottom
// screen's Toon tab: one copy of romfs:/baked/laff_meter.ttm (bake_laff_meter.py), loaded on
// first use on the main thread.
#pragma once

#include <3ds.h>
#include <citro2d.h>

namespace tt::ui {

struct LaffFace {
    int hp = 15, maxHp = 15;
    float rgb[3] = {0.7f, 0.7f, 0.8f}; // the toon's head colour
    int species = 0;                   // world::Species: which head
};

// Draws the meter in the size x size square at (x0, y0). hpText / maxText are the parsed
// numbers for the eyes (either may be null). Returns false when the atlas is missing.
bool drawLaffMeter(const LaffFace& face, float x0, float y0, float size, const C2D_Text* hpText,
                   const C2D_Text* maxText, float z = 0.5f);

} // namespace tt::ui
