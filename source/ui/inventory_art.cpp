#include "ui/inventory_art.h"

#include "ui/panel_geometry.h"
#include "world/battle_fx.h"

namespace tt::ui {

namespace {

C3D_Tex* guiTex() { return world::GagAtlas::instance().guiTexture(); }

// Texture-pixel rectangle [u0, u1) x [v0, v1) of texture 1 to the screen rectangle; flipX mirrors.
void drawSub(C3D_Tex* tex, float u0, float v0, float u1, float v1, float x, float y, float w, float h, float z,
             u32 tint, bool flipX = false) {
    if (w <= 0.0f || h <= 0.0f) return;
    const float sw = u1 - u0, sh = v1 - v0;
    Tex3DS_SubTexture sub;
    sub.width = uint16_t(sw + 0.5f);
    sub.height = uint16_t(sh + 0.5f);
    const float l = u0 / kInvGuiTexW, r = u1 / kInvGuiTexW;
    sub.left = flipX ? r : l;
    sub.right = flipX ? l : r;
    sub.top = 1.0f - v0 / kInvGuiTexH; // the first row in memory samples as t = 1
    sub.bottom = 1.0f - v1 / kInvGuiTexH;
    C2D_Image img{tex, &sub};
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, tint, 1.0f);
    C2D_DrawImageAt(img, x, y, z, &t, w / sub.width, h / sub.height);
}

} // namespace

bool invArtReady() {
    world::GagAtlas::instance().init();
    return guiTex() != nullptr;
}

void invTintBegin() { C2D_SetTintMode(C2D_TintMult); }
void invTintEnd() { C2D_SetTintMode(C2D_TintSolid); }

void drawInvArt(InvArt art, float x, float y, float w, float h, float z, u32 tint) {
    C3D_Tex* tex = guiTex();
    if (!tex || int(art) < 0 || int(art) >= int(InvArt::Count)) return;
    const InvArtRect& r = kInvArtRects[int(art)];
    drawSub(tex, r.x, r.y, r.x + r.w, r.y + r.h, x, y, w, h, z, tint);
}

void drawInvHint(float x, float y, float w, float h, float z, u32 tint) {
    C3D_Tex* tex = guiTex();
    if (!tex || w <= 0.0f || h <= 0.0f) return;
    const InvArtRect& r = kInvArtRects[int(InvArt::Hint)];
    // A 15 px corner on a short card leaves no cream. Cap it so the centre stays open.
    const float kC = layout::hintCorner(w, h);
    const float sx[4] = {r.x, r.x + kC, r.x + r.w - kC, r.x + r.w};
    const float sy[4] = {r.y, r.y + kC, r.y + r.h - kC, r.y + r.h};
    const float dx[4] = {x, x + kC, x + w - kC, x + w};
    const float dy[4] = {y, y + kC, y + h - kC, y + h};
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            drawSub(tex, sx[col], sy[row], sx[col + 1], sy[row + 1], dx[col], dy[row],
                    dx[col + 1] - dx[col], dy[row + 1] - dy[row], z, tint);
        }
    }
}

void drawInvRow(float x, float y, float w, float h, float slotX, float z, u32 tint) {
    C3D_Tex* tex = guiTex();
    if (!tex) return;
    const InvArtRect& r = kInvArtRects[int(InvArt::Row)];
    const float split = r.x + r.w * kInvRowSlotFrac;
    drawSub(tex, r.x, r.y, split, r.y + r.h, x, y, slotX - x, h, z, tint);
    drawSub(tex, split, r.y, r.x + r.w, r.y + r.h, slotX, y, x + w - slotX, h, z, tint);
}

void drawInvMenu(float x, float y, float w, float h, float z) {
    C3D_Tex* tex = guiTex();
    if (!tex) return;
    const InvArtRect& r = kInvArtRects[int(InvArt::Menu)];
    // The left 50 source pixels are plain frame (the notch, detail panel and button slots are
    // further right). Corners at 1:1, edges and the panel stretched, the right end mirrored.
    constexpr float kC = 9.0f, kMid = 50.0f;
    constexpr u32 kWhite = 0xFFFFFFFF;
    const float u0 = r.x, u1 = r.x + kC, u2 = r.x + kMid;
    const float v[4] = {r.y, r.y + kC, r.y + r.h - kC, r.y + r.h};
    const float dy[4] = {y, y + kC, y + h - kC, y + h};
    for (int row = 0; row < 3; row++) {
        const float sy0 = v[row], sy1 = v[row + 1], ty = dy[row], th = dy[row + 1] - dy[row];
        drawSub(tex, u0, sy0, u1, sy1, x, ty, kC, th, z, kWhite);
        drawSub(tex, u1, sy0, u2, sy1, x + kC, ty, w - 2 * kC, th, z, kWhite);
        drawSub(tex, u0, sy0, u1, sy1, x + w - kC, ty, kC, th, z, kWhite, true);
    }
}

void drawOutlinedText(const C2D_Text& t, float x, float y, float z, float scale, u32 fill, u32 outline, u32 flags) {
    const u32 f = C2D_WithColor | flags;
    static constexpr float kOff[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (const auto& o : kOff) C2D_DrawText(&t, f, x + o[0], y + o[1], z, scale, scale, outline);
    C2D_DrawText(&t, f, x, y, z + 0.001f, scale, scale, fill);
}

} // namespace tt::ui
