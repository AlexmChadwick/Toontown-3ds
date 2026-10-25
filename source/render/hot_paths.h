#pragma once
#include <cstring>
namespace tt::render {
// Citro3D rows are stored w,z,y,x. This is world * dequantize, with no
// general 4x4 multiply: dequantize has only a diagonal and translation.
inline void dequantizeWorld(float out[16], const float world[16], const float scale[3], const float offset[3]) {
    for (int row = 0; row < 4; ++row) {
        const float* w = world + row * 4;
        float* q = out + row * 4;
        q[0] = w[3] * offset[0] + w[2] * offset[1] + w[1] * offset[2] + w[0];
        q[1] = w[1] * scale[2];
        q[2] = w[2] * scale[1];
        q[3] = w[3] * scale[0];
    }
}
// Valid only between beginWorld/endWorld. External citro2d draws invalidate it.
// Keep draw order: alpha surfaces and depth-biased facial layers are never sorted.
struct BatchState {
    const void* texture = nullptr;
    bool geometryValid = false;
    const void* geometry = nullptr;
    bool bindGeometry(const void* next) {
        if (geometryValid && geometry == next)
            return false;
        geometry = next;
        geometryValid = true;
        return true;
    }
    bool textureValid = false;
    bool tintValid = false;
    float tint[4] = {};
    int blend = -1;
    int layer = -1;
    int cull = -1;
    bool bindTexture(const void* next) {
        if (textureValid && texture == next)
            return false;
        texture = next;
        textureValid = true;
        return true;
    }
    bool uploadTint(const float* next) {
        if (tintValid && !std::memcmp(tint, next, sizeof(tint)))
            return false;
        std::memcpy(tint, next, sizeof(tint));
        tintValid = true;
        return true;
    }
};
} // namespace tt::render
