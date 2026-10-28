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

void paper(float x, float y, float w, float h, float r, u32 fill, u32 edge, float z) {
    roundRect(x, y, w, h, r, edge, z);
    roundRect(x + 2, y + 2, w - 4, h - 4, r > 2 ? r - 2 : 0, fill, z);
}

void button(float x, float y, float w, float h, u32 face, u32 lip, bool down, float r, float z) {
    constexpr float kLip = 3.0f;
    roundRect(x, y + kLip, w, h - kLip, r, lip, z);
    roundRect(x, down ? y + kLip : y, w, h - kLip, r, face, z);
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
