#include "ui/theme.h"

#include <cmath>

namespace tt::ui::theme {

namespace {

// Corner fans: 4 segments per quarter circle read as round at the 4-10 px radii used here.
constexpr int kCornerSegments = 4;
float gCos[kCornerSegments + 1], gSin[kCornerSegments + 1];
bool gTrigReady = false;

void initTrig() {
    if (gTrigReady) return;
    for (int i = 0; i <= kCornerSegments; i++) {
        const float a = 1.5707963f * i / kCornerSegments;
        gCos[i] = cosf(a);
        gSin[i] = sinf(a);
    }
    gTrigReady = true;
}

// Quarter fan centred at (cx, cy); sx, sy pick the quadrant (+1 / -1).
void corner(float cx, float cy, float r, float sx, float sy, u32 c, float z) {
    for (int i = 0; i < kCornerSegments; i++) {
        C2D_DrawTriangle(cx, cy, c, cx + sx * gCos[i] * r, cy + sy * gSin[i] * r, c, cx + sx * gCos[i + 1] * r,
                         cy + sy * gSin[i + 1] * r, c, z);
    }
}

} // namespace

void sky(float w, float h) {
    C2D_DrawRectangle(0, 0, 0.1f, w, h, kSkyTop, kSkyTop, kSkyBottom, kSkyBottom);
}

namespace {
u32 lerpColor(u32 a, u32 b, float t) {
    u32 out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float ca = float((a >> shift) & 0xFF), cb = float((b >> shift) & 0xFF);
        out |= u32(ca + (cb - ca) * t + 0.5f) << shift;
    }
    return out;
}
} // namespace

void skyBand(float y0, float y1, float w, float h, float z) {
    const u32 top = lerpColor(kSkyTop, kSkyBottom, y0 / h), bottom = lerpColor(kSkyTop, kSkyBottom, y1 / h);
    C2D_DrawRectangle(0, y0, z, w, y1 - y0, top, top, bottom, bottom);
}

void roundRect(float x, float y, float w, float h, float r, u32 c, float z) {
    if (w <= 0 || h <= 0) return;
    const float maxR = (w < h ? w : h) * 0.5f;
    if (r > maxR) r = maxR;
    if (r < 1.0f) {
        C2D_DrawRectSolid(x, y, z, w, h, c);
        return;
    }
    initTrig();
    C2D_DrawRectSolid(x + r, y, z, w - 2 * r, h, c);
    C2D_DrawRectSolid(x, y + r, z, r, h - 2 * r, c);
    C2D_DrawRectSolid(x + w - r, y + r, z, r, h - 2 * r, c);
    corner(x + r, y + r, r, -1, -1, c, z);
    corner(x + w - r, y + r, r, 1, -1, c, z);
    corner(x + r, y + h - r, r, -1, 1, c, z);
    corner(x + w - r, y + h - r, r, 1, 1, c, z);
}

void roundRing(float x, float y, float w, float h, float r, float t, u32 c, float z) {
    if (w <= 0 || h <= 0 || t <= 0) return;
    const float maxR = (w < h ? w : h) * 0.5f;
    if (r > maxR) r = maxR;
    if (r < t) r = t;
    // The four straight edges between the corners...
    C2D_DrawRectSolid(x + r, y, z, w - 2 * r, t, c);
    C2D_DrawRectSolid(x + r, y + h - t, z, w - 2 * r, t, c);
    C2D_DrawRectSolid(x, y + r, z, t, h - 2 * r, c);
    C2D_DrawRectSolid(x + w - t, y + r, z, t, h - 2 * r, c);
    // ...and each corner as an annulus quarter (two triangles per segment; 8 segments, so a
    // pill-sized radius stays round).
    constexpr int kSeg = 8;
    float co[kSeg + 1], si[kSeg + 1];
    for (int i = 0; i <= kSeg; i++) {
        co[i] = cosf(1.5707963f * i / kSeg);
        si[i] = sinf(1.5707963f * i / kSeg);
    }
    const float ri = r - t;
    const float cx[4] = {x + r, x + w - r, x + r, x + w - r}, cy[4] = {y + r, y + r, y + h - r, y + h - r};
    const float sx[4] = {-1, 1, -1, 1}, sy[4] = {-1, -1, 1, 1};
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < kSeg; i++) {
            const float ox0 = cx[k] + sx[k] * co[i] * r, oy0 = cy[k] + sy[k] * si[i] * r;
            const float ox1 = cx[k] + sx[k] * co[i + 1] * r, oy1 = cy[k] + sy[k] * si[i + 1] * r;
            const float ix0 = cx[k] + sx[k] * co[i] * ri, iy0 = cy[k] + sy[k] * si[i] * ri;
            const float ix1 = cx[k] + sx[k] * co[i + 1] * ri, iy1 = cy[k] + sy[k] * si[i + 1] * ri;
            C2D_DrawTriangle(ox0, oy0, c, ox1, oy1, c, ix0, iy0, c, z);
            C2D_DrawTriangle(ix0, iy0, c, ox1, oy1, c, ix1, iy1, c, z);
        }
    }
}

namespace {
// A pill (fully rounded ends) as horizontal strips, so the ends stay round at any size and the
// face can carry a vertical gradient: top -> mid over the upper `knee` of the height, then mid ->
// bottom (the quit_button face stays bright and darkens only towards its lower edge).
u32 gradientAt(u32 top, u32 mid, u32 bottom, float knee, float t) {
    return t < knee ? lerpColor(top, mid, t / knee) : lerpColor(mid, bottom, (t - knee) / (1.0f - knee));
}

// Rounded-rectangle strips with corner radius r (r = half the short side gives a pill).
void stripRound(float x, float y, float w, float h, float r, u32 top, u32 mid, u32 bottom, float knee, float z) {
    if (w <= 0 || h <= 0) return;
    const float rmax = (w < h ? w : h) * 0.5f;
    if (r > rmax) r = rmax;
    const int n = int(h / 1.5f) + 1;
    for (int i = 0; i < n; i++) {
        const float y0 = y + h * i / n, y1 = y + h * (i + 1) / n;
        const float dy = (y0 + y1) * 0.5f - (y + h * 0.5f);
        // Only the round ends are inset: the band of rows within r of the vertical centre is full width.
        float inset = 0.0f;
        const float over = fabsf(dy) - (h * 0.5f - r);
        if (over > 0.0f) inset = r - sqrtf(r * r - (over < r ? over * over : r * r));
        const u32 c0 = gradientAt(top, mid, bottom, knee, (y0 - y) / h);
        const u32 c1 = gradientAt(top, mid, bottom, knee, (y1 - y) / h);
        C2D_DrawRectangle(x + inset, y0, z, w - 2 * inset, y1 - y0, c0, c0, c1, c1);
    }
}

void stripPill(float x, float y, float w, float h, u32 top, u32 mid, u32 bottom, float knee, float z) {
    stripRound(x, y, w, h, (w < h ? w : h) * 0.5f, top, mid, bottom, knee, z);
}

struct PillColors {
    u32 outline, top, mid, bottom;
};
// Sampled from the quit_button textures (UP / RLVR); red and grey follow the same shading.
constexpr PillColors kPills[4] = {
    {rgb(0x89, 0x6C, 0x15), rgb(0xF8, 0xF6, 0x40), rgb(0xF0, 0xEE, 0x1C), rgb(0xB8, 0xB4, 0x00)},
    {rgb(0x2E, 0x57, 0x60), rgb(0x38, 0xD4, 0xF8), rgb(0x1C, 0xC8, 0xF0), rgb(0x00, 0x8A, 0xA8)},
    {rgb(0x6A, 0x10, 0x0C), rgb(0xF6, 0x70, 0x5E), rgb(0xE8, 0x48, 0x38), rgb(0xA8, 0x20, 0x16)},
    {rgb(0x74, 0x6E, 0x64), rgb(0xE6, 0xE0, 0xD4), rgb(0xD8, 0xD2, 0xC6), rgb(0xA8, 0xA2, 0x96)},
};
} // namespace

void pill(float x, float y, float w, float h, Pill style, bool down, float z) {
    const PillColors& c = kPills[int(style)];
    const u32 top = down ? c.mid : c.top;
    stripPill(x, y, w, h, c.outline, c.outline, c.outline, 0.5f, z);
    stripPill(x + 1.5f, y + 1.5f, w - 3, h - 3, top, c.mid, c.bottom, 0.6f, z);
    // The gloss: a soft lighter band across the upper part of the face.
    const float gh = (h - 3) * 0.3f;
    const u32 g0 = lerpColor(top, kWhite, 0.45f), g1 = lerpColor(c.mid, kWhite, 0.08f);
    stripPill(x + h * 0.3f, y + 2.5f, w - h * 0.6f, gh, g0, lerpColor(g0, g1, 0.5f), g1, 0.5f, z);
}

// PurchScrn_BTN_UP / _DN (phase_4 gag_shop_purchase_gui), sampled: an orange bar with rounded
// corners, a light band under its top edge and a darker lower lip. Disabled is a faded tan (upstream
// dims it with image3_color 0.6, which reads as mud at 3DS size).
void purchaseButton(float x, float y, float w, float h, bool enabled, bool down, float z, bool red) {
    constexpr PillColors kOrange = {rgb(0xA8, 0x65, 0x0C), rgb(0xFF, 0xBF, 0x6C), rgb(0xFE, 0xA6, 0x3A),
                                    rgb(0xF0, 0x8C, 0x17)};
    constexpr PillColors kOff = {rgb(0x7E, 0x60, 0x3A), rgb(0xCC, 0xA8, 0x80), rgb(0xC2, 0x98, 0x6A),
                                 rgb(0xB0, 0x84, 0x58)};
    // Red: our Cancel beside it (no upstream counterpart), the same shading in quit_button's red.
    constexpr PillColors kRedBtn = {rgb(0x6A, 0x10, 0x0C), rgb(0xF6, 0x80, 0x6C), rgb(0xE8, 0x48, 0x38),
                                    rgb(0xC0, 0x2C, 0x20)};
    const PillColors& c = !enabled ? kOff : red ? kRedBtn : kOrange;
    const float r = h * 0.24f;
    stripRound(x, y, w, h, r, c.outline, c.outline, c.outline, 0.5f, z);
    stripRound(x + 1.5f, y + 1.5f, w - 3, h - 3, r - 1.0f, down ? c.mid : c.top, c.mid, c.bottom, 0.25f, z);
}

u32 pillInk(Pill style) {
    return style == Pill::Red ? kWhite : style == Pill::Grey ? kInkSoft : kInk;
}

void paper(float x, float y, float w, float h, float r, u32 fill, u32 edge, float z) {
    roundRect(x, y, w, h, r, edge, z);
    roundRect(x + 2, y + 2, w - 4, h - 4, r > 2 ? r - 2 : 0, fill, z);
}

void button(float x, float y, float w, float h, u32 face, u32 lip, bool down, float r, float z) {
    constexpr float kLip = 3.0f;
    roundRect(x, y + kLip, w, h - kLip, r, lip, z);
    roundRect(x, down ? y + kLip : y, w, h - kLip, r, face, z);
}

void clip(float x, float y, float w, float h) {
    C2D_Flush();
    // The bottom framebuffer is 240 x 320 and rotated: screen x runs down it from the far end,
    // screen y across it from the far side.
    auto px = [](float v, float hi) { return u32(v < 0 ? 0 : v > hi ? hi : v); };
    C3D_SetScissor(GPU_SCISSOR_NORMAL, px(240.0f - (y + h), 240), px(320.0f - (x + w), 320), px(240.0f - y, 240),
                   px(320.0f - x, 320));
}

void unclip() {
    C2D_Flush();
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}

u32 mix(u32 a, u32 b, float t) { return lerpColor(a, b, t); }

u32 shade(u32 c, float s) {
    const u32 r = u32((c & 0xFF) * s), g = u32(((c >> 8) & 0xFF) * s), b = u32(((c >> 16) & 0xFF) * s);
    return r | g << 8 | b << 16 | (c & 0xFF000000u);
}

void textCentered(const C2D_Text& t, float cx, float y, float scale, u32 color, float maxW, float z) {
    float w = 0.0f;
    C2D_TextGetDimensions(&t, scale, scale, &w, nullptr);
    if (maxW > 0.0f && w > maxW) {
        scale *= maxW / w;
    }
    // Each line centred on cx (multi-line labels such as "DONE\nSHOPPING").
    C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter, cx, y, z, scale, scale, color);
}

void textFit(const C2D_Text& t, float x, float y, float scale, u32 color, float maxW, float z) {
    float w = 0.0f;
    C2D_TextGetDimensions(&t, scale, scale, &w, nullptr);
    if (maxW > 0.0f && w > maxW) scale *= maxW / w;
    C2D_DrawText(&t, C2D_WithColor, x, y, z, scale, scale, color);
}

} // namespace tt::ui::theme
