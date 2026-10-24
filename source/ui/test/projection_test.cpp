// Host unit test for tt::ui::projectToScreen (source/ui/projection.h).
//
//   c++ -std=c++17 -Wall -Wextra -I source source/ui/test/projection_test.cpp -o /tmp/pt && /tmp/pt
//
// Not part of the 3DS build (the Makefile does not recurse into source/ui/test).
// The citro3d types and Mtx_* functions below are re-implemented from citro3d
// source/maths/*.c (zlib license) so the math is checked against the real formulas,
// including C3D_FVec's reversed (w, z, y, x) memory layout.
#include <cmath>
#include <cstdio>
#include <cstring>

typedef union {
    struct {
        float w, z, y, x;
    };
    float c[4];
} C3D_FVec;
typedef union {
    C3D_FVec r[4];
    float m[16];
} C3D_Mtx;

#define TT_HOST_TEST 1
#include "ui/projection.h"

namespace {

void Mtx_Zeros(C3D_Mtx* m) { memset(m, 0, sizeof(*m)); }

// mtx_persptilt.c
void Mtx_PerspTilt(C3D_Mtx* mtx, float fovx, float invaspect, float near, float far, bool isLeftHanded) {
    float fovx_tan = tanf(fovx / 2.0f);
    Mtx_Zeros(mtx);
    mtx->r[0].y = 1.0f / fovx_tan;
    mtx->r[1].x = -1.0f / (fovx_tan * invaspect);
    mtx->r[2].w = far * near / (near - far);
    mtx->r[3].z = isLeftHanded ? 1.0f : -1.0f;
    mtx->r[2].z = -mtx->r[3].z * near / (near - far);
}

// mtx_perspstereotilt.c
void Mtx_PerspStereoTilt(C3D_Mtx* mtx, float fovx, float invaspect, float near, float far, float iod, float screen,
                         bool isLeftHanded) {
    float fovx_tan = tanf(fovx / 2.0f);
    float fovx_tan_invaspect = fovx_tan * invaspect;
    float shift = iod / (2.0f * screen);
    Mtx_Zeros(mtx);
    mtx->r[0].y = 1.0f / fovx_tan;
    mtx->r[1].x = -1.0f / fovx_tan_invaspect;
    mtx->r[1].w = iod / 2.0f;
    mtx->r[2].w = near * far / (near - far);
    mtx->r[3].z = isLeftHanded ? 1.0f : -1.0f;
    mtx->r[1].z = -mtx->r[3].z * shift / fovx_tan_invaspect;
    mtx->r[2].z = -mtx->r[3].z * near / (near - far);
}

// mtx_persp.c (untilted reference)
void Mtx_Persp(C3D_Mtx* mtx, float fovy, float aspect, float near, float far, bool isLeftHanded) {
    float fovy_tan = tanf(fovy / 2.0f);
    Mtx_Zeros(mtx);
    mtx->r[0].x = 1.0f / (aspect * fovy_tan);
    mtx->r[1].y = 1.0f / fovy_tan;
    mtx->r[2].w = far * near / (near - far);
    mtx->r[3].z = isLeftHanded ? 1.0f : -1.0f;
    mtx->r[2].z = -mtx->r[3].z * near / (near - far);
}

// mtx_orthotilt.c (citro2d's top-screen space: (0, 400, 240, 0, 1, -1, true))
void Mtx_OrthoTilt(C3D_Mtx* mtx, float left, float right, float bottom, float top, float near, float far,
                   bool isLeftHanded) {
    Mtx_Zeros(mtx);
    mtx->r[0].y = 2.0f / (top - bottom);
    mtx->r[0].w = (bottom + top) / (bottom - top);
    mtx->r[1].x = 2.0f / (left - right);
    mtx->r[1].w = (left + right) / (right - left);
    mtx->r[2].z = isLeftHanded ? 1.0f / (far - near) : 1.0f / (near - far);
    mtx->r[2].w = 0.5f * (near + far) / (near - far) - 0.5f;
    mtx->r[3].w = 1.0f;
}

C3D_FVec vec3(float x, float y, float z) {
    C3D_FVec v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.w = 0.0f;
    return v;
}
float dot3(C3D_FVec a, C3D_FVec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
C3D_FVec sub3(C3D_FVec a, C3D_FVec b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
C3D_FVec cross3(C3D_FVec a, C3D_FVec b) {
    return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
C3D_FVec norm3(C3D_FVec v) {
    const float l = sqrtf(dot3(v, v));
    return vec3(v.x / l, v.y / l, v.z / l);
}

// mtx_lookat.c, right-handed
void Mtx_LookAt(C3D_Mtx* out, C3D_FVec eye, C3D_FVec target, C3D_FVec up) {
    C3D_FVec zaxis = norm3(sub3(eye, target));
    C3D_FVec xaxis = norm3(cross3(up, zaxis));
    C3D_FVec yaxis = cross3(zaxis, xaxis);
    const C3D_FVec axes[3] = {xaxis, yaxis, zaxis};
    for (int i = 0; i < 3; i++) {
        out->r[i].x = axes[i].x;
        out->r[i].y = axes[i].y;
        out->r[i].z = axes[i].z;
        out->r[i].w = -dot3(axes[i], eye);
    }
    out->r[3].x = out->r[3].y = out->r[3].z = 0.0f;
    out->r[3].w = 1.0f;
}

void mulVec(const C3D_Mtx& m, const float in[4], float out[4]) {
    for (int i = 0; i < 4; i++) out[i] = m.r[i].x * in[0] + m.r[i].y * in[1] + m.r[i].z * in[2] + m.r[i].w * in[3];
}

int failures = 0;
void check(bool ok, const char* what, float a = 0, float b = 0) {
    if (!ok) {
        failures++;
        printf("FAIL %s (%g, %g)\n", what, a, b);
    }
}
bool near(float a, float b, float eps = 0.05f) { return fabsf(a - b) <= eps; }

} // namespace

int main() {
    using tt::ui::projectToScreen;
    const float kPi = 3.14159265f;
    const float fovy = 55.0f * 2.0f * kPi / 360.0f; // Renderer::kFovYDeg
    const float aspect = 400.0f / 240.0f;          // C3D_AspectRatioTop
    C3D_Mtx tilt, flat, ortho, view;
    Mtx_PerspTilt(&tilt, fovy, aspect, 1.0f, 300.0f, false);
    Mtx_Persp(&flat, fovy, aspect, 1.0f, 300.0f, false);
    Mtx_OrthoTilt(&ortho, 0.0f, 400.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);

    // Camera like world::Player: behind and above the toon, Z up (Panda convention).
    Mtx_LookAt(&view, vec3(0, -12, 6), vec3(0, 0, 3), vec3(0, 0, 1));

    float sx, sy, depth;
    // 1. Look-at target projects to the centre.
    check(projectToScreen(tilt, view, 0, 0, 3, &sx, &sy, &depth), "centre visible");
    check(near(sx, 200) && near(sy, 120), "centre", sx, sy);
    // 2. World +X (camera right) goes right; world +Z goes up (smaller sy).
    projectToScreen(tilt, view, 2, 0, 3, &sx, &sy);
    check(sx > 210 && near(sy, 120, 0.5f), "right is +sx", sx, sy);
    projectToScreen(tilt, view, 0, 0, 5, &sx, &sy);
    check(sy < 110 && near(sx, 200), "up is -sy", sx, sy);
    // 3. Behind the camera is rejected.
    check(!projectToScreen(tilt, view, 0, -20, 3, &sx, &sy), "behind rejected");
    // 4. Far off to the side is rejected (beyond the margin).
    check(!projectToScreen(tilt, view, 200, 0, 3, &sx, &sy), "offscreen rejected");

    // 5. Grid sweep: tilted result == untilted reference, and the tilted NDC equals
    //    citro2d's OrthoTilt of (sx, sy).
    int n = 0;
    for (float x = -8; x <= 8; x += 2)
        for (float y = -2; y <= 30; y += 4)
            for (float z = 0; z <= 8; z += 2) {
                if (!projectToScreen(tilt, view, x, y, z, &sx, &sy, &depth, 1e9f)) continue;
                n++;
                const float p[4] = {x, y, z, 1};
                float eye[4], cf[4], ct[4];
                mulVec(view, p, eye);
                mulVec(flat, eye, cf);
                mulVec(tilt, eye, ct);
                const float rx = (cf[0] / cf[3] + 1.0f) * 200.0f, ry = (1.0f - cf[1] / cf[3]) * 120.0f;
                check(near(sx, rx, 0.01f) && near(sy, ry, 0.01f), "matches untilted", sx - rx, sy - ry);
                const float sp[4] = {sx, sy, 0, 1};
                float co[4];
                mulVec(ortho, sp, co);
                check(near(co[0], ct[0] / ct[3], 1e-4f) && near(co[1], ct[1] / ct[3], 1e-4f), "matches OrthoTilt",
                      co[0] - ct[0] / ct[3], co[1] - ct[1] / ct[3]);
                check(near(depth, -eye[2], 1e-3f), "depth = eye distance", depth, -eye[2]);
            }
    check(n > 100, "grid coverage", static_cast<float>(n));

    // 6. Stereo: left/right eyes straddle the mono position horizontally, same row.
    C3D_Mtx left, right;
    Mtx_PerspStereoTilt(&left, fovy, aspect, 1.0f, 300.0f, -0.1f, 2.0f, false);
    Mtx_PerspStereoTilt(&right, fovy, aspect, 1.0f, 300.0f, 0.1f, 2.0f, false);
    float lx, ly, rx, ry, mx, my;
    projectToScreen(tilt, view, 1, 10, 4, &mx, &my);
    projectToScreen(left, view, 1, 10, 4, &lx, &ly);
    projectToScreen(right, view, 1, 10, 4, &rx, &ry);
    check(near(ly, my, 0.01f) && near(ry, my, 0.01f), "stereo same row", ly - my, ry - my);
    check((lx - mx) * (rx - mx) < 0.0f, "stereo straddles", lx - mx, rx - mx);

    printf("%s: %d grid points, %d failures\n", failures ? "FAILED" : "OK", n, failures);
    return failures ? 1 : 0;
}
