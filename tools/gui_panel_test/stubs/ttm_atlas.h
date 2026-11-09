// Host-side TTM atlas reader for the GUI panel tests: the committed bottom-screen atlases
// (romfs/baked/*.ttm, written by tools/pipeline/ttm.py) are real RGBA8 texture bytes, so
// layout assertions can measure the real art the device samples instead of mirroring UI
// layout constants (QA-20261109-09: the Friends left-clip check must key off the panel's
// visible white inner edge, not the list inset constant).
//
// TTM1 v1, little endian, per ttm.py:
//   Header (64 bytes): magic "TTM1"; u16 version @4; u16 numTextures @8; u32 numBatches @12;
//     f32 posScale[3] @20; f32 posOffset[3] @32; u32 texTableOffset @44.
//   Texture entry (48 bytes): u16 width, u16 height; u8 gpuFormat, u8 mipLevels;
//     u8 wrapS, u8 wrapT; u32 dataOffset, u32 dataSize; char name[32].
//   RGBA8 (gpuFormat 0) texture data: 8x8 tiles in row order, pixels inside a tile in
//     Morton order, each pixel the 4 bytes A, B, G, R (ttm.py encode), first image row first.
//
// Only what the small UI atlases need is implemented: texture 0, one mip level, RGBA8. Any
// other format, a short file or a bad magic leaves the reader unloaded and the caller fails
// its checks on purpose. pixel() hands back image-order RGBA (row 0 = top).
#pragma once

#include "host_draw_recorder.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace host {

class TtmAtlas {
public:
    // Loads the atlas's texture 0. Returns false (and stays unloaded) on any parse failure.
    bool load(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        std::string data;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
        fclose(f);
        const auto rd8 = [&](size_t off) -> unsigned {
            return off < data.size() ? unsigned(uint8_t(data[off])) : 0;
        };
        const auto rd16 = [&](size_t off) -> unsigned {
            return off + 1 < data.size()
                       ? unsigned(uint8_t(data[off])) | unsigned(uint8_t(data[off + 1])) << 8
                       : 0;
        };
        const auto rd32 = [&](size_t off) -> unsigned {
            return rd16(off) | rd16(off + 2) << 16;
        };
        if (data.size() < 64 || data.compare(0, 4, "TTM1") != 0) return false;
        if (rd16(4) != 1 || rd16(8) < 1) return false;
        const size_t texOff = rd32(44);
        if (texOff + 48 > data.size()) return false;
        w_ = int(rd16(texOff));
        h_ = int(rd16(texOff + 2));
        const unsigned format = rd8(texOff + 4), mips = rd8(texOff + 5);
        const size_t dataOffset = rd32(texOff + 8);
        constexpr unsigned kRgba8 = 0; // GPU_TEXCOLOR
        if (format != kRgba8 || mips != 1 || w_ <= 0 || h_ <= 0) return false;
        if (dataOffset >= data.size() || size_t(w_) * h_ * 4 > data.size() - dataOffset)
            return false;
        // Untile: 8x8 tiles in row order, Morton order inside each tile (ttm.py tile()).
        // RGBA8 pixels are the bytes A, B, G, R; stored here as R, G, B, A.
        rgba_.assign(size_t(w_) * h_ * 4, 0);
        const uint8_t* src = reinterpret_cast<const uint8_t*>(data.data()) + dataOffset;
        size_t i = 0;
        for (int ty = 0; ty < h_; ty += 8)
            for (int tx = 0; tx < w_; tx += 8)
                for (unsigned m = 0; m < 64; ++m, i += 4) {
                    unsigned px = 0, py = 0;
                    for (unsigned b = 0; b < 3; ++b) {
                        px |= ((m >> (2 * b)) & 1u) << b;
                        py |= ((m >> (2 * b + 1)) & 1u) << b;
                    }
                    const size_t o = (size_t(ty + py) * w_ + tx + px) * 4;
                    rgba_[o] = src[i + 3];      // R
                    rgba_[o + 1] = src[i + 2];  // G
                    rgba_[o + 2] = src[i + 1];  // B
                    rgba_[o + 3] = src[i];      // A
                }
        loaded_ = true;
        return true;
    }

    bool loaded() const { return loaded_; }
    int width() const { return w_; }
    int height() const { return h_; }

    // Image-order pixel (row 0 = top): false when out of range. rgba is R, G, B, A.
    bool pixel(int x, int y, uint8_t rgba[4]) const {
        if (!loaded_ || x < 0 || y < 0 || x >= w_ || y >= h_) return false;
        const size_t o = (size_t(y) * w_ + x) * 4;
        rgba[0] = rgba_[o];
        rgba[1] = rgba_[o + 1];
        rgba[2] = rgba_[o + 2];
        rgba[3] = rgba_[o + 3];
        return true;
    }

    static bool nearWhite(const uint8_t rgba[4]) {
        return rgba[0] > 240 && rgba[1] > 240 && rgba[2] > 240 && rgba[3] > 240;
    }

    // Screen-space x of the visible face's left inner edge for a recorded image draw:
    // maps the screen row band [bandTop, bandBottom] into the sub-image through the
    // draw's own rect and UVs, scans atlas columns from the image's left edge inward,
    // and returns the left boundary of the first column whose sampled rows are at least
    // minFrac near-white (and whose next confirm columns are too, so an antialias blip
    // cannot read as the face). Returns a negative value when nothing matched; the
    // caller fails its checks on that.
    float whiteFaceLeftEdgeX(const HostRecord& img, float bandTop, float bandBottom,
                            float minFrac = 0.6f, int confirm = 3) const {
        if (!loaded_) return -1.0f;
        const float ax0 = img.u0 * w_, ax1 = img.u1 * w_;
        if (!(ax1 > ax0) || img.h <= 0 || img.w <= 0) return -1.0f;
        // Screen y -> sub-image v -> image row (row 0 = top; v0 is the recorded top edge).
        const auto rowAt = [&](float sy) -> float {
            const float t = (sy - img.y) / img.h;
            const float v = img.v0 + t * (img.v1 - img.v0);
            return (1.0f - v) * h_;
        };
        std::vector<int> rows;
        for (float sy = bandTop; sy <= bandBottom; sy += 1.0f) {
            const int r = int(rowAt(sy) + 0.5f);
            if (r >= 0 && r < h_ && (rows.empty() || r != rows.back())) rows.push_back(r);
        }
        if (rows.size() < 4) return -1.0f;
        const auto fracWhite = [&](int col) -> float {
            int white = 0;
            for (int r : rows) {
                uint8_t p[4];
                if (pixel(col, r, p) && nearWhite(p)) ++white;
            }
            return float(white) / float(rows.size());
        };
        for (int col = int(std::ceil(ax0)); col < int(ax1); ++col) {
            bool stable = fracWhite(col) >= minFrac;
            for (int d = 1; stable && d <= confirm; ++d) stable = fracWhite(col + d) >= minFrac;
            if (!stable) continue;
            return img.x + (float(col) - ax0) / (ax1 - ax0) * img.w;
        }
        return -1.0f;
    }

private:
    bool loaded_ = false;
    int w_ = 0, h_ = 0;
    std::vector<uint8_t> rgba_;
};

} // namespace host
