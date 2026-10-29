// Small citro2d primitives shared by the bottom-screen UI.  Rounded rectangles are
// triangulated because C2D_DrawCircleSolid's procedural texture is unreliable after
// the world pass on the PICA200/Azahar path.
#pragma once

#include <citro2d.h>

namespace tt::ui::shape {

inline void roundRect(float x, float y, float z, float w, float h, float radius, u32 color) {
    constexpr float arc[4][2] = {
        {1.0f, 0.0f}, {0.866025f, 0.5f}, {0.5f, 0.866025f}, {0.0f, 1.0f},
    };
    if (radius * 2.0f > w) radius = w * 0.5f;
    if (radius * 2.0f > h) radius = h * 0.5f;
    C2D_DrawRectSolid(x + radius, y, z, w - 2.0f * radius, h, color);
    C2D_DrawRectSolid(x, y + radius, z, radius, h - 2.0f * radius, color);
    C2D_DrawRectSolid(x + w - radius, y + radius, z, radius, h - 2.0f * radius, color);
    const float cx[4] = {x + w - radius, x + radius, x + radius, x + w - radius};
    const float cy[4] = {y + radius, y + radius, y + h - radius, y + h - radius};
    const float sx[4] = {1.0f, -1.0f, -1.0f, 1.0f};
    const float sy[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    for (int corner = 0; corner < 4; ++corner) {
        for (int segment = 0; segment < 3; ++segment) {
            C2D_DrawTriangle(cx[corner], cy[corner], color,
                             cx[corner] + sx[corner] * arc[segment][0] * radius,
                             cy[corner] + sy[corner] * arc[segment][1] * radius, color,
                             cx[corner] + sx[corner] * arc[segment + 1][0] * radius,
                             cy[corner] + sy[corner] * arc[segment + 1][1] * radius, color, z);
        }
    }
}

inline void roundFrame(float x, float y, float z, float w, float h, float radius,
                       float border, u32 frame, u32 fill) {
    roundRect(x, y, z, w, h, radius, frame);
    roundRect(x + border, y + border, z + 0.01f, w - 2.0f * border,
              h - 2.0f * border, radius > border ? radius - border : 0.0f, fill);
}

} // namespace tt::ui::shape
