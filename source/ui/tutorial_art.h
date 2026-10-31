// Tutorial Tom's lesson cards (quest_assign_101: Cogs, Toon buildings, Cog buildings and the two
// squirt cards), one copy of romfs:/baked/tutorial_gui.ttm (bake_tutorial_gui.py), loaded on
// first use on the main thread.
#pragma once

#include "ui/tutorial_gui_atlas.h"

namespace tt::ui {

// Draws `card` fitted (aspect kept) and centred in (x, y, w, h). Returns false when the atlas is
// missing.
bool drawTutorialCard(TutorialCard card, float x, float y, float w, float h, float z = 0.5f);

} // namespace tt::ui
