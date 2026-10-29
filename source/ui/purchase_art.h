// The gag purchase screens' art (PurchaseBase / ClerkPurchase): the Goofys_Sign plank and the
// jellybean Jar, one copy of romfs:/baked/purchase_gui.ttm (bake_purchase_gui.py), loaded on
// first use on the main thread.
#pragma once

#include <citro2d.h>

#include "ui/purchase_gui_atlas.h"

namespace tt::ui {

// Draws `art` stretched into (x, y, w, h). Returns false when the atlas is missing.
bool drawPurchaseArt(PurchaseArt art, float x, float y, float w, float h, float z = 0.5f);
// The part's width / height, for callers that keep its aspect.
float purchaseArtAspect(PurchaseArt art);
// PurchaseBase.pointDisplay: the Jar, height h with its top-left at (x, y), and `count` over it
// in yellow (0.95, 0.95, 0) with a black shadow. Falls back to a plain count without the atlas.
void drawBeanJar(const C2D_Text& count, float x, float y, float h, float z = 0.5f);
// A small jar icon (height h, top-left at x, y) before a jellybean count, as InventoryPage's
// moneyDisplay; three beans in upstream's colours without the atlas. Returns its width.
float drawJarIcon(float x, float y, float h, float z = 0.5f);
// The plank with `title` on it in the sign's brown (text_fg 0.6, 0.2, 0).
void drawGagShopSign(const C2D_Text& title, float x, float y, float w, float h, float z = 0.5f);

} // namespace tt::ui
