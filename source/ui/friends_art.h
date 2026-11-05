// FriendsListPanel's original phase_3.5 GUI pieces, baked by bake_friends_gui.py.
#pragma once

#include <citro2d.h>

#include "ui/friends_gui_atlas.h"

namespace tt::ui {

// Draws one stock FriendsListPanel part stretched into the destination rectangle. Returns false
// when the RomFS atlas is unavailable so callers can retain a readable procedural fallback.
bool drawFriendsArt(FriendsArt art, float x, float y, float w, float h, float z = 0.5f);

} // namespace tt::ui
