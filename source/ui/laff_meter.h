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

// LaffMeter.animatedEffect (ToontownIntervals.getPulseLargerIval / getPulseSmallerIval): the
// meter swells on a heal and shrinks on damage, then eases back. Feed it the laff every frame.
class LaffPulse {
public:
    // Returns the scale to draw at this frame (1 at rest).
    float update(int hp);

private:
    int lastHp_ = -1;
    uint64_t startMs_ = 0;
    float change_ = 1.0f;
};

// The meter plus its cached eye numbers (re-parsed only when the laff changes) and its pulse:
// what each screen that shows our laff owns.
class LaffDisplay {
public:
    bool init();
    void shutdown();
    // Centred on (cx, cy); size is the atlas cell at rest.
    void draw(const LaffFace& face, float cx, float cy, float size, float z = 0.5f);

private:
    C2D_TextBuf buf_ = nullptr;
    C2D_Text hp_{}, max_{};
    int hpShown_ = -1, maxShown_ = -1;
    LaffPulse pulse_;
};

} // namespace tt::ui
