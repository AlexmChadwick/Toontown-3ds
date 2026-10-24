#include "render/anim.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "core/log.h"
#include "core/memory.h"
#include "world/toon_playback.h"

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
    if (frames_) {
        mem::LinearGuard g;
        linearFree(frames_);
    }
    free(attach_);
    frames_ = nullptr;
    attach_ = nullptr;
    numVertices_ = numFrames_ = frameStride_ = capacity_ = sourceFrames_ = 0;
    attachEnabled_ = false;
}

void Anim::swap(Anim& o) {
    std::swap(numVertices_, o.numVertices_);
    std::swap(numFrames_, o.numFrames_);
    std::swap(frameStride_, o.frameStride_);
    std::swap(fps_, o.fps_);
    for (int i = 0; i < 3; i++) {
        std::swap(posScale_[i], o.posScale_[i]);
        std::swap(posOffset_[i], o.posOffset_[i]);
    }
    std::swap(capacity_, o.capacity_);
    std::swap(sourceFrames_, o.sourceFrames_);
    std::swap(sourcePeriod_, o.sourcePeriod_);
    std::swap(sourceA_, o.sourceA_);
    std::swap(sourceB_, o.sourceB_);
    std::swap(attachEnabled_, o.attachEnabled_);
    std::swap(frames_, o.frames_);
    std::swap(attach_, o.attach_);
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
        attachEnabled_ = (h.flags & 1) != 0;
        numVertices_ = h.numVertices;
        numFrames_ = h.numFrames;
        frameStride_ = h.frameStride;
        fps_ = h.fps;
        memcpy(posScale_, h.posScale, sizeof(posScale_));
        memcpy(posOffset_, h.posOffset, sizeof(posOffset_));
        const u32 bytes = numFrames_ * frameStride_;
        {
            mem::LinearGuard g;
            frames_ = linearAlloc(bytes);
        }
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

bool Anim::reserveWindow(u32 maxVertices) {
    unload();
    if (!maxVertices || maxVertices>512) return false;
    { mem::LinearGuard guard; frames_ = linearAlloc(maxVertices*16); }
    attach_ = static_cast<float*>(malloc(96));
    if (!frames_ || !attach_) { unload(); return false; }
    capacity_ = maxVertices;
    return true;
}

bool Anim::readWindow(FILE* file, float time, bool loop, u32 vertices, float period) {
    clearWindow();
    Header h{};
    if (!file || !capacity_ || !readAt(file,0,&h,sizeof(h)) || memcmp(h.magic,"TTA1",4) ||
        h.version!=1 || (h.flags&~1u) || h.reserved0 || h.numVertices!=vertices ||
        !vertices || vertices>capacity_ || !h.numFrames || h.numFrames>4096 ||
        !std::isfinite(h.fps) || h.fps<=0 || h.fps>240 || h.frameStride!=vertices*8 ||
        h.framesOffset<64 || fseek(file,0,SEEK_END)!=0) return false;
    const long size=ftell(file);
    const uint64_t end=uint64_t(h.framesOffset)+uint64_t(h.numFrames)*h.frameStride;
    if (size<0 || end>uint64_t(size) || ((h.flags&1) &&
        (h.attachOffset<end || uint64_t(h.attachOffset)+uint64_t(h.numFrames)*48>uint64_t(size)))) return false;
    for (int i=0;i<3;i++) if (!std::isfinite(h.posScale[i]) || h.posScale[i]<=0 || !std::isfinite(h.posOffset[i])) return false;
    const auto sample=world::clipSample(time,h.fps,h.numFrames,loop,period);
    if (!readAt(file,h.framesOffset+sample.a*h.frameStride,frames_,h.frameStride) ||
        !readAt(file,h.framesOffset+sample.b*h.frameStride,static_cast<u8*>(frames_)+h.frameStride,h.frameStride)) return false;
    if (h.flags&1) {
        if (!readAt(file,h.attachOffset+sample.a*48,attach_,48) ||
            !readAt(file,h.attachOffset+sample.b*48,attach_+12,48)) return false;
        for (int i=0;i<24;i++) if (!std::isfinite(attach_[i])) return false;
    }
    numVertices_=vertices; numFrames_=2; frameStride_=h.frameStride; fps_=h.fps;
    sourcePeriod_=period;
    sourceFrames_=h.numFrames; sourceA_=sample.a; sourceB_=sample.b;
    attachEnabled_=(h.flags&1)!=0;
    memcpy(posScale_,h.posScale,sizeof(posScale_)); memcpy(posOffset_,h.posOffset,sizeof(posOffset_));
    GSPGPU_FlushDataCache(frames_,2*frameStride_);
    return true;
}

bool Anim::windowMatches(float time, bool loop, float* blend) const {
    if (!loaded() || !sourceFrames_) return false;
    const auto sample=world::clipSample(time,fps_,sourceFrames_,loop,sourcePeriod_);
    if (sample.a!=sourceA_ || sample.b!=sourceB_) return false;
    if (blend) *blend=sample.blend;
    return true;
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
