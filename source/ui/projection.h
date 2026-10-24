// World point -> top-screen logical pixel (400x240, origin top-left, as citro2d draws).
//
// Mtx_PerspTilt / Mtx_PerspStereoTilt bake a quarter turn into the projection because
// the top framebuffer is 240x400 portrait. Derivation (citro3d maths/mtx_persptilt.c):
//   untilted:  ndc' = (eye.x / (tan*aspect), eye.y / tan)
//   tilted:    clip.x = eye.y / tan            -> ndc.x =  ndc'.y
//              clip.y = -eye.x / (tan*aspect)  -> ndc.y = -ndc'.x
// citro2d's top-screen space is Mtx_OrthoTilt(0, 400, 240, 0), whose inverse is
//   sx = (1 - ndc.y) * 200,  sy = (1 - ndc.x) * 120.
// The matrices are used as given, so the stereo variant (per-eye shift in r[1]) works
// too. Verified on the host by source/ui/test/projection_test.cpp.
//
// Header-only and free of libctru calls so the host test can include it (define
// TT_HOST_TEST and provide C3D_FVec/C3D_Mtx first).
#pragma once

#ifndef TT_HOST_TEST
#include <citro3d.h>
#endif

namespace tt::ui {

constexpr float kTopW = 400.0f;
constexpr float kTopH = 240.0f;

// Returns false when the point is behind the near plane or more than `margin` pixels
// outside the screen. On success writes the logical pixel and, optionally, the eye-space
// distance (clip w), useful for sorting and distance scaling.
inline bool projectToScreen(const C3D_Mtx& proj, const C3D_Mtx& view, float x, float y, float z, float* sx,
                            float* sy, float* outDepth = nullptr, float margin = 40.0f) {
    auto dot = [](const C3D_FVec& r, float a, float b, float c, float d) {
        return r.x * a + r.y * b + r.z * c + r.w * d;
    };
    const float ex = dot(view.r[0], x, y, z, 1.0f);
    const float ey = dot(view.r[1], x, y, z, 1.0f);
    const float ez = dot(view.r[2], x, y, z, 1.0f);
    const float ew = dot(view.r[3], x, y, z, 1.0f);
    const float cx = dot(proj.r[0], ex, ey, ez, ew);
    const float cy = dot(proj.r[1], ex, ey, ez, ew);
    const float cz = dot(proj.r[2], ex, ey, ez, ew);
    const float cw = dot(proj.r[3], ex, ey, ez, ew);
    if (cw <= 1e-4f || cz > 0.0f) return false; // behind the eye / in front of near (PICA z in [-w, 0])
    const float nx = cx / cw, ny = cy / cw;
    const float px = (1.0f - ny) * (kTopW * 0.5f);
    const float py = (1.0f - nx) * (kTopH * 0.5f);
    if (px < -margin || px > kTopW + margin || py < -margin || py > kTopH + margin) return false;
    *sx = px;
    *sy = py;
    if (outDepth) *outDepth = cw;
    return true;
}

} // namespace tt::ui
