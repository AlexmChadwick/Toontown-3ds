#include "render/anim.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"

namespace tt::render {

namespace {

constexpr u16 kVersion = 1;

struct Header {
    char magic[4];
    u16 version;
    u16 flags;
    u32 numVertices;
    u16 numFrames;
    u16 reserved0;
    float fps;
    float posScale[3];
    float posOffset[3];
    u32 framesOffset;
    u32 frameStride;
    u32 attachOffset;
    u32 reserved[2];
};
static_assert(sizeof(Header) == 64, "must match tta.py header");

bool readAt(FILE* f, u32 offset, void* dst, u32 size) {
    return fseek(f, offset, SEEK_SET) == 0 && fread(dst, 1, size, f) == size;
}

} // namespace

Anim::~Anim() { unload(); }

void Anim::unload() {
    if (frames_) linearFree(frames_);
    free(attach_);
    frames_ = nullptr;
    attach_ = nullptr;
    numVertices_ = numFrames_ = frameStride_ = 0;
}

bool Anim::load(const char* path) {
    unload();
    FILE* f = fopen(path, "rb");
    if (!f) {
        TT_LOG("anim: cannot open %s", path);
        return false;
    }
    Header h;
    bool ok = readAt(f, 0, &h, sizeof(h)) && memcmp(h.magic, "TTA1", 4) == 0 && h.version == kVersion &&
              h.numFrames > 0 && h.fps > 0.0f;
    if (ok) {
        numVertices_ = h.numVertices;
        numFrames_ = h.numFrames;
        frameStride_ = h.frameStride;
        fps_ = h.fps;
        memcpy(posScale_, h.posScale, sizeof(posScale_));
        memcpy(posOffset_, h.posOffset, sizeof(posOffset_));
        const u32 bytes = numFrames_ * frameStride_;
        frames_ = linearAlloc(bytes);
        ok = frames_ && readAt(f, h.framesOffset, frames_, bytes);
        if (ok) GSPGPU_FlushDataCache(frames_, bytes);
        if (ok && (h.flags & 1)) {
            attach_ = static_cast<float*>(malloc(numFrames_ * 12 * sizeof(float)));
            ok = attach_ && readAt(f, h.attachOffset, attach_, numFrames_ * 12 * sizeof(float));
        }
    }
    fclose(f);
    if (!ok) {
        TT_LOG("anim: failed to load %s", path);
        unload();
    }
    return ok;
}

void Anim::attach(u32 a, u32 b, float t, C3D_Mtx* out) const {
    const float* ma = attach_ + a * 12;
    const float* mb = attach_ + b * 12;
    // Component-wise lerp: fine for the small per-frame deltas of 12 fps clips.
    for (int row = 0; row < 3; row++) {
        const float* ra = ma + row * 4;
        const float* rb = mb + row * 4;
        out->r[row].x = ra[0] + (rb[0] - ra[0]) * t;
        out->r[row].y = ra[1] + (rb[1] - ra[1]) * t;
        out->r[row].z = ra[2] + (rb[2] - ra[2]) * t;
        out->r[row].w = ra[3] + (rb[3] - ra[3]) * t;
    }
    out->r[3] = FVec4_New(0.0f, 0.0f, 0.0f, 1.0f);
}

void sampleClip(const Anim& anim, float t, u32* a, u32* b, float* blend) {
    const u32 n = anim.numFrames();
    float f = t * anim.fps();
    f = fmodf(f, static_cast<float>(n));
    if (f < 0.0f) f += n; // negative play rates (walking backwards)
    const u32 i = static_cast<u32>(f);
    *a = i % n;
    *b = (i + 1) % n;
    *blend = f - static_cast<float>(i);
}

} // namespace tt::render
