// Make-A-Toon's button art (tt_m_gui_mat_mainGui): one copy of romfs:/baked/mat_gui.ttm
// (bake_mat_gui.py), loaded on first use on the main thread and released with the front end.
#pragma once

#include "ui/mat_gui_atlas.h"

namespace tt::ui {

// Draws `art` stretched into (x, y, w, h), mirrored left-right when flipX (MakeAToon's back
// button is the next button mirrored). Returns false when the atlas is missing.
bool drawMatArt(MatArt art, float x, float y, float w, float h, float z = 0.5f, bool flipX = false);
// The part's width / height, for callers that keep its aspect.
float matArtAspect(MatArt art);
// Frees the atlas (the front end calls this when it shuts down).
void releaseMatArt();

} // namespace tt::ui
