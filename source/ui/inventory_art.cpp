#include "ui/inventory_art.h"

#include <3ds.h>

#include "core/log.h"
#include "render/model.h"
#include "ui/theme.h"
#include "world/battle_fx.h"

namespace tt::ui::inventory {

namespace {

render::Model gArt;
bool gTried = false;

C3D_Tex* atlas() {
    if (!gTried) {
        gTried = true;
        if (!gArt.load("romfs:/baked/inventory_gui.ttm")) TT_LOG("ui: no inventory_gui.ttm");
    }
    return gArt.loaded() ? gArt.texture(0) : nullptr;
}

// Source columns [sx, sx + sw) of `art`, drawn into (x, y, w, h), multiplied by `color`.
void slice(C3D_Tex* tex, const InventoryArtRect& r, float sx, float sw, float x, float y, float w, float h, u32 color,
           float z) {
    const float u0 = (r.x + sx) / kInventoryGuiAtlasW, u1 = (r.x + sx + sw) / kInventoryGuiAtlasW;
    const float v0 = 1.0f - r.y / kInventoryGuiAtlasH, v1 = 1.0f - (r.y + r.h) / kInventoryGuiAtlasH; // top row = t 1
    // citro2d sizes the quad from the sub-texture's integer size, so scale from that (a float
    // scale leaves hairline gaps between the slices).
    Tex3DS_SubTexture sub{uint16_t(sw + 0.5f), uint16_t(r.h + 0.5f), u0, v0, u1, v1};
    C2D_Image img{tex, &sub};
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, color, 1.0f);
    C2D_DrawImageAt(img, x, y, z, &tint, w / float(sub.width), h / float(sub.height));
}

} // namespace

bool drawRow(float x, float y, float w, float h, u32 color, float z) {
    C3D_Tex* tex = atlas();
    if (!tex) {
        theme::roundRect(x, y, w, h, h * 0.4f, color, z);
        return false;
    }
    const InventoryArtRect& r = kInventoryArtRects[int(InventoryArt::Row)];
    C2D_SetTintMode(C2D_TintMult);
    slice(tex, r, 0.0f, r.w, x, y, w, h, color, z);
    C2D_SetTintMode(C2D_TintSolid);
    return true;
}

bool drawButton(InventoryArt face, float x, float y, float w, float h, u32 color, float z) {
    C3D_Tex* tex = atlas();
    if (!tex || int(face) <= int(InventoryArt::Row) || int(face) >= int(InventoryArt::Count)) {
        theme::roundRect(x, y, w, h, h * 0.3f, color, z);
        return false;
    }
    const InventoryArtRect& r = kInventoryArtRects[int(face)];
    // The corners are about 12 of the 62 source columns; they scale with the height only.
    constexpr float kCapSrc = 12.0f;
    float cap = kCapSrc * h / r.h;
    if (cap * 2.0f > w) cap = w * 0.5f;
    C2D_SetTintMode(C2D_TintMult);
    slice(tex, r, 0.0f, kCapSrc, x, y, cap, h, color, z);
    slice(tex, r, kCapSrc, r.w - 2.0f * kCapSrc, x + cap, y, w - 2.0f * cap, h, color, z);
    slice(tex, r, r.w - kCapSrc, kCapSrc, x + w - cap, y, cap, h, color, z);
    C2D_SetTintMode(C2D_TintSolid);
    return true;
}

void drawGagButton(InventoryArt face, u32 color, int track, int level, float rowX, float rowY, float rowW,
                   float rowH, float iconAlpha, float out[4], float z) {
    // A button is 0.18 x 0.114 in the row's 1.667 x 0.125 frame: one slot pitch wide, 0.91 tall.
    const float bw = rowW * kSlotPitch, bh = rowH * 0.91f;
    const float bx = slotX(rowX, rowW, level) + rowW * kSlotW * 0.5f - bw * 0.5f, by = rowY + (rowH - bh) * 0.5f;
    drawButton(face, bx, by, bw, bh, color, z);
    const float icon = bh * 0.92f;
    world::GagAtlas::instance().drawIcon(track, level, bx + bw * 0.42f - icon * 0.5f, by + (bh - icon) * 0.5f, icon,
                                         z + 0.01f, iconAlpha);
    out[0] = bx;
    out[1] = by;
    out[2] = bw;
    out[3] = bh;
}

u32 drawPurchaseCell(int track, int level, float x, float y, float w, float h, bool open, bool canBuy, bool bought,
                     bool selected, float iconSize, float z) {
    if (!open) {
        theme::roundRect(x + 1, y + 1, w - 2, h - 2, 4, theme::shade(theme::kTrackColors[track], 0.78f), z);
        if (selected) theme::roundRing(x, y, w, h, 5, 2, theme::kRed, z + 0.02f);
        return theme::shade(theme::kTrackColors[track], 0.45f);
    }
    const InventoryArt face = selected ? InventoryArt::Rollover : canBuy || bought ? InventoryArt::Up : InventoryArt::Flat;
    const u32 color = bought ? theme::kYellow : canBuy ? kPressable : kUnpressable;
    drawButton(face, x, y, w, h, color, z);
    if (selected) theme::roundRing(x, y, w, h, h * 0.3f, 2, theme::kRed, z + 0.02f);
    world::GagAtlas::instance().drawIcon(track, level, x + 2, y + (h - iconSize) * 0.5f, iconSize, z + 0.01f,
                                         canBuy || bought ? 1.0f : 0.3f);
    return bought ? theme::kInk : canBuy ? theme::kWhite : theme::rgb(0xFF, 0xFF, 0xFF, 0x90);
}

} // namespace tt::ui::inventory
