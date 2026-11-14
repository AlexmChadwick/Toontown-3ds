// The battle inventory's original art (bake_inventory_gui.py): InventoryNew's track rows and
// gag buttons, battle_gui's red BATTLE_Menu frame and TownBattleToonPanel's status card, drawn
// with citro2d and tinted multiplicatively like DirectGui's image_color.
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "ui/inventory_gui_atlas.h"
#include "ui/theme.h"

namespace tt::ui {

// False until the RomFS atlas is loaded (it loads with world::GagAtlas); callers fall back to
// the theme's procedural shapes.
bool invArtReady();
// The pieces are grey art tinted multiplicatively: draw them between invTintBegin / invTintEnd
// (citro2d's C2D_TintMult), and icons and text after invTintEnd. One mode switch per batch keeps
// the command buffer small with 49 buttons on screen.
void invTintBegin();
void invTintEnd();
// One piece stretched to (x, y, w, h), multiplied by `tint` (alpha included).
void drawInvArt(InvArt art, float x, float y, float w, float h, float z, u32 tint);
// InventoryRow tinted with the track colour: the label end is drawn over [x, slotX), the seven
// button slots over [slotX, x + w), so the slots sit under the gag buttons.
void drawInvRow(float x, float y, float w, float h, float slotX, float z, u32 tint);
// BATTLE_Menu's red frame and dark panel as a nine-slice (inside invTintBegin / invTintEnd) (its left end, mirrored for the right;
// the gag detail panel is not part of this layout).
void drawInvMenu(float x, float y, float w, float h, float z);
// Bold text with a one-pixel outline in `outline` (both cached C2D_Text, nothing parsed here).
// flags: C2D_AlignLeft / C2D_AlignRight / C2D_AlignCenter.
void drawOutlinedText(const C2D_Text& t, float x, float y, float z, float scale, u32 fill, u32 outline,
                      u32 flags = C2D_AlignLeft);

// Where the InventoryRow art's first slot starts, as a fraction of the row's width (InventoryNew:
// the first button's left edge sits 22% in; measured on the baked row).
constexpr float kInvRowSlotFrac = 0.218f;
constexpr u32 kInvPressable = theme::rgb(0x00, 0x99, 0xFF);         // PressableImageColor (0, 0.6, 1)
constexpr u32 kInvUnpressable = theme::rgb(0x4C, 0x4C, 0x4C, 0xCC);  // UnpressableImageColor (0.3, 0.3, 0.3, 0.8)
constexpr u32 kInvDisabledButton = theme::rgb(0x66, 0x66, 0x66);     // Fire without pink slips (0.4, 0.4, 0.4)

} // namespace tt::ui
