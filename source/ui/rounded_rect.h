#pragma once

#include <citro2d.h>

namespace tt::ui {

// Rounded rectangle built only from solid rectangles and triangles. Citro2D's circle
// primitive uses the procedural-texture unit, which is unreliable after our world pass.
inline void drawRoundedRect(float x, float y, float z, float w, float h, float radius, u32 color) {
    if (radius * 2.0f > w) radius = w * 0.5f;
    if (radius * 2.0f > h) radius = h * 0.5f;
    C2D_DrawRectSolid(x + radius, y, z, w - 2.0f * radius, h, color);
    C2D_DrawRectSolid(x, y + radius, z, radius, h - 2.0f * radius, color);
    C2D_DrawRectSolid(x + w - radius, y + radius, z, radius, h - 2.0f * radius, color);

    // Three segments per quarter circle are smooth enough at 240p and cost only 12 triangles.
    constexpr float arc[4][2] = {
        {1.0f, 0.0f}, {0.866025f, 0.5f}, {0.5f, 0.866025f}, {0.0f, 1.0f},
    };
    const float cx[4] = {x + w - radius, x + radius, x + radius, x + w - radius};
    const float cy[4] = {y + radius, y + radius, y + h - radius, y + h - radius};
    const float sx[4] = {1.0f, -1.0f, -1.0f, 1.0f};
    const float sy[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i < 3; ++i) {
            C2D_DrawTriangle(cx[corner], cy[corner], color,
                             cx[corner] + sx[corner] * arc[i][0] * radius,
                             cy[corner] + sy[corner] * arc[i][1] * radius, color,
                             cx[corner] + sx[corner] * arc[i + 1][0] * radius,
                             cy[corner] + sy[corner] * arc[i + 1][1] * radius, color, z);
        }
    }
}

inline void drawRoundedPanel(float x, float y, float z, float w, float h, float radius,
                             u32 border, u32 fill, float borderWidth = 1.5f) {
    drawRoundedRect(x, y, z, w, h, radius, border);
    drawRoundedRect(x + borderWidth, y + borderWidth, z + 0.001f,
                    w - 2.0f * borderWidth, h - 2.0f * borderWidth,
                    radius > borderWidth ? radius - borderWidth : 0.0f, fill);
}

} // namespace tt::ui
