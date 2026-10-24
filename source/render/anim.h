// Runtime side of the TTA vertex-animation format (writer: tools/pipeline/tta.py).
#pragma once

#include <3ds.h>
#include <citro3d.h>

namespace tt::render {

class Anim {
public:
    Anim() = default;
    ~Anim();
    Anim(const Anim&) = delete;
    Anim& operator=(const Anim&) = delete;

    // Frames go to linear memory (the GPU reads them directly); attach matrices to the heap.
    bool load(const char* path);
    void unload();
    bool loaded() const { return frames_ != nullptr; }
    // Exchanges contents (no GPU work): a loader thread fills a staging Anim, the main thread swaps it in.
    void swap(Anim& other);

    u32 numVertices() const { return numVertices_; }
    u32 numFrames() const { return numFrames_; }
    float fps() const { return fps_; }
    float duration() const { return numFrames_ / fps_; }
    const float* posScale() const { return posScale_; }
    const float* posOffset() const { return posOffset_; }
    const void* frame(u32 i) const { return static_cast<const u8*>(frames_) + i * frameStride_; }

    bool hasAttach() const { return attach_ != nullptr; }
    // Child part's root in this part's space, lerped between frames a and b.
    void attach(u32 a, u32 b, float t, C3D_Mtx* out) const;

    u32 linearBytes() const { return numFrames_ * frameStride_; }

private:
    u32 numVertices_ = 0;
    u32 numFrames_ = 0;
    u32 frameStride_ = 0;
    float fps_ = 12.0f;
    float posScale_[3] = {};
    float posOffset_[3] = {};
    void* frames_ = nullptr;
    float* attach_ = nullptr; // numFrames x 12 (3x4 row-major)
};

// Samples a looping clip: frame indices a/b and blend weight for time `t` seconds.
void sampleClip(const Anim& anim, float t, u32* a, u32* b, float* blend);

} // namespace tt::render
