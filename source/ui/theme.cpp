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

namespace {
// A pill (fully rounded ends) as horizontal strips, so the ends stay round at any size and the
// face can carry a vertical gradient: top -> mid over the upper `knee` of the height, then mid ->
// bottom (the quit_button face stays bright and darkens only towards its lower edge).
u32 gradientAt(u32 top, u32 mid, u32 bottom, float knee, float t) {
    return t < knee ? lerpColor(top, mid, t / knee) : lerpColor(mid, bottom, (t - knee) / (1.0f - knee));
}

void stripPill(float x, float y, float w, float h, u32 top, u32 mid, u32 bottom, float knee, float z) {
    if (w <= 0 || h <= 0) return;
    const float r = (w < h ? w : h) * 0.5f;
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
        w = maxW;
    }
    C2D_DrawText(&t, C2D_WithColor, cx - w * 0.5f, y, z, scale, scale, color);
}

void textFit(const C2D_Text& t, float x, float y, float scale, u32 color, float maxW, float z) {
    float w = 0.0f;
    C2D_TextGetDimensions(&t, scale, scale, &w, nullptr);
    if (maxW > 0.0f && w > maxW) scale *= maxW / w;
    C2D_DrawText(&t, C2D_WithColor, x, y, z, scale, scale, color);
}

} // namespace tt::ui::theme
