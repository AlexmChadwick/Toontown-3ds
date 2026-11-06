// Draw-call recorder for the host citro stubs: every bottom-screen draw the compiled
// panel sources make is appended here as a normalized rectangle (plus text / UV data),
// so the tests can assert layout invariants without a GPU.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct C3D_Tex;
struct Tex3DS_SubTexture;

// One recorded item. x/y/w/h is the destination rectangle in bottom-screen pixels
// (320x240), whatever unit the caller used.
struct HostRecord {
    enum Kind { Text, Rect, RectGrad, Triangle, Line, Image, Scissor } kind;
    float x = 0, y = 0, w = 0, h = 0;
    float z = 0.5f;
    float scale = 0;        // text
    std::string text;        // text content (optimized glyph runs share one record)
    std::string tag;         // Image: source description
    u32 color = 0;           // text ink / solid rect colour
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1; // Image sub-texture UVs
    u32 tint = 0;            // Image tint colour (C2D_ImageTint applied to it)
    float tintAlpha = 0;     // Image: tint blend (0 = none applied)
};

struct HostScissor {
    int mode = 0; // GPU_SCISSOR_DISABLE
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

namespace host {

extern std::vector<HostRecord> gRecords;
extern HostScissor gScissor;
extern bool gRecordEnabled;

inline void clearRecords() {
    gRecords.clear();
}
inline void pushRecord(const HostRecord& r) {
    if (gRecordEnabled) gRecords.push_back(r);
}

} // namespace host
