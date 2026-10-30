// InventoryNew's gag grid art (phase_3.5/models/gui/inventory_gui): the InventoryRow track row
// and the InventoryButton faces, one copy of romfs:/baked/inventory_gui.ttm
// (bake_inventory_gui.py), loaded on first use on the main thread. The art is grey; every draw
// takes the image_color it is tinted with, as upstream.
#pragma once

#include <citro2d.h>

#include "ui/inventory_gui_atlas.h"
#include "ui/theme.h"

namespace tt::ui::inventory {

// InventoryNew's button colours (image_color).
constexpr u32 kPressable = theme::rgb(0x00, 0x99, 0xFF);   // PressableImageColor (0, 0.6, 1)
constexpr u32 kBookFace = theme::rgb(0x00, 0x99, 0xFF);    // BookUnpressableImage0Color (0, 0.6, 1)
constexpr u32 kUnpressable = theme::rgb(0x4D, 0x4D, 0x4D, 0xCC); // UnpressableImageColor (0.3, 0.3, 0.3, 0.8)
constexpr u32 kRollover = theme::rgb(0x1A, 0xB3, 0xFF);  // BookUnpressableImage2Color (0.1, 0.7, 1)
constexpr u32 kDisabled = theme::rgb(0x66, 0x66, 0x66);    // the Fire button without pink slips (0.4, 0.4, 0.4)

// The row's slots, as fractions of its width: slot i starts at kSlot0 + i * kSlotPitch.
constexpr float kSlot0 = 0.2115f, kSlotPitch = 0.1111f, kSlotW = 0.1040f;
// The row's label area (the track name and its experience bar) ends at kSlot0.
inline float slotX(float rowX, float rowW, int i) { return rowX + rowW * (kSlot0 + i * kSlotPitch); }

// The row, stretched into (x, y, w, h), tinted `color` (the track colour). False without the atlas.
bool drawRow(float x, float y, float w, float h, u32 color, float z = 0.5f);
// A button face (Up / Down / Rollover / Flat) in (x, y, w, h), tinted `color`. The rounded ends
// keep their shape (three slices). False without the atlas.
bool drawButton(InventoryArt face, float x, float y, float w, float h, u32 color, float z = 0.5f);
// A gag button on slot `level` of a row drawn at (rowX, rowY, rowW, rowH): the face, then the
// gag icon (alpha `iconAlpha`) left of centre as geom_pos (-0.01, 0, 0) at geom_scale 0.7. Returns
// the button rectangle in *out (x, y, w, h) for the caller's count text and focus.
void drawGagButton(InventoryArt face, u32 color, int track, int level, float rowX, float rowY, float rowW,
                   float rowH, float iconAlpha, float out[4], float z = 0.5f);

// A purchase-mode gag cell (Purchase / ClerkPurchase, purchaseActivateButtons) in (x, y, w, h):
// a gag we can buy is a pressable blue button, one we cannot a flat grey one, one bought this
// visit stays yellow, a level not reached is an empty slot in the row's shade. `selected` is the
// D-Pad / touch selection (the rollover face and a red ring). Draws the icon (iconSize px at the
// cell's left) and returns the ink for the cell's label.
u32 drawPurchaseCell(int track, int level, float x, float y, float w, float h, bool open, bool canBuy, bool bought,
                     bool selected, float iconSize, float z = 0.5f);

} // namespace tt::ui::inventory
