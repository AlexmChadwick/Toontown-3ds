// Tutorial Tom's lesson cards (quest_assign_101: Cogs, Toon buildings, Cog buildings and the two
// squirt cards) and the BlinkingArrows arrow, one copy of romfs:/baked/tutorial_gui.ttm
// (bake_tutorial_gui.py), loaded on first use on the main thread.
#pragma once

#include "ui/tutorial_gui_atlas.h"

namespace tt::ui {

// Draws `card` fitted (aspect kept) and centred in (x, y, w, h). Returns false when the atlas is
// missing.
bool drawTutorialCard(TutorialCard card, float x, float y, float w, float h, float z = 0.5f);

// One of QuestParser ARROWS_ON's arrows (phase_3/models/props/arrow), `size` px square, its tip at
// (tipX, tipY), pointing `angleDeg` clockwise from screen right (90 = down), at `alpha`.
bool drawTutorialArrow(float tipX, float tipY, float angleDeg, float size, float alpha, float z = 0.9f);

// BlinkingArrows' colour-scale loop: 0.25 -> 1 over 0.75 s, then back, from `seconds` = 0.
float tutorialArrowAlpha(float seconds);

} // namespace tt::ui
