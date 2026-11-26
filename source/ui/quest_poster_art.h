// QuestPoster's original art (bake_quest_poster.py): stickerbook_gui's questCard and
// questPictureFrame plus the quest icons, one copy of romfs:/baked/quest_poster.ttm, loaded on
// first use on the main thread.
#pragma once

#include <3ds.h>

#include "ui/quest_poster_atlas.h"

namespace tt::ui {

// Draws `art` stretched into (x, y, w, h), multiplied by `tint` (DirectGui image_color).
// Returns false when the atlas is missing.
bool drawQuestArt(QuestArt art, float x, float y, float w, float h, u32 tint, float z = 0.5f);

// The laff meter's species head in npc's head colour (QuestPoster.createNpcToonHead), fitted into
// the size x size square at (x, y). False for ids without a DNA entry (the HQ officer, Any).
bool drawQuestNpcHead(uint32_t npc, float x, float y, float size, float z = 0.5f);

} // namespace tt::ui
