// Host-side text metrics for the citro2d stub: parses the real committed interface font
// (romfs/baked/fonts/impress.bcfnt, baked by tools/pipeline/bake_ui_font.py) so every
// measured width uses the same per-glyph numbers the on-device citro2d sees, with a
// deterministic monospace fallback if the file is missing.
//
// Format (from bake_ui_font.py, which mirrors mkbcfnt output):
//   header: 'CFNT'/'CFNU', u16 headerSize @6, u32 nBlocks @16
//   FINF @headerSize: tglpPtr, cwdhPtr, cmapPtr at FINF+16 (u32 absolute offsets)
//   CWDH block @ptr-8: [8-byte block header][u16 first][u16 last][u32 next]
//     entries at block+16: {u8 left, u8 glyphWidth, u8 right}, advance = right - left
//   CMAP block @ptr-8: [8-byte block header][u16 codeBegin][u16 codeEnd][u16 method][u16 pad]
//     [u32 next at block+16], mapping at block+20
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

namespace host {

class FontMetrics {
public:
    struct Glyph {
        int left = 0, glyphWidth = 0, right = 8;
    };

    // Loads `path`; on any failure keeps the fallback model (monospace 15 px advance,
    // 31 px line height, i.e. the ImpressBT cell bounds).
    void load(const char* path) {
        em_ = 31.0f; // ImpressBT cell: tglp (cw=31, ch=31, base=23)
        FILE* f = fopen(path, "rb");
        if (!f) return;
        std::string data;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
        fclose(f);
        if (data.size() < 32 || (data.compare(0, 4, "CFNT") && data.compare(0, 4, "CFNU")))
            return;
        const auto read16 = [&](size_t off) -> unsigned {
            return off + 1 < data.size() ? (unsigned(uint8_t(data[off])) | unsigned(uint8_t(data[off + 1])) << 8) : 0;
        };
        const auto read32 = [&](size_t off) -> unsigned {
            return read16(off) | read16(off + 2) << 16;
        };
        const size_t headerSize = read16(6);
        if (headerSize + 32 > data.size()) return;
        // FINF block: at headerSize; the entry lists tglp/cwdh/cmap pointers at FINF+16.
        const unsigned tglp = read32(headerSize + 16), cwdh = read32(headerSize + 20),
                       cmap = read32(headerSize + 24);
        if (!tglp || !cwdh || !cmap || cmap + 8 > data.size()) return;
        // CWDH chain: entries of 3 bytes per glyph index.
        unsigned p = cwdh, guard = 0;
        std::vector<Glyph>& glyphs = glyphs_;
        while (p && p + 16 <= data.size() && ++guard < 64) {
            const size_t b = p - 8;
            const unsigned first = read16(b + 8), last = read16(b + 10);
            // u32 next at b+12; entries start at b+16.
            const unsigned next = read32(b + 12);
            if (last >= first && b + 16 + 3u * (last - first + 1) <= data.size()) {
                if (glyphs.size() < last + 1) glyphs.resize(last + 1);
                for (unsigned g = first; g <= last; ++g) {
                    glyphs[g] = Glyph{data[b + 16 + 3 * (g - first)], data[b + 17 + 3 * (g - first)],
                                      data[b + 18 + 3 * (g - first)]};
                }
            }
            p = next;
        }
        if (glyphs.empty()) return;
        // CMAP chain: codepoint -> glyph index.
        p = cmap;
        guard = 0;
        while (p && p + 20 <= data.size() && ++guard < 64) {
            const size_t b = p - 8;
            const unsigned codeBegin = read16(b + 8), codeEnd = read16(b + 10),
                           method = read16(b + 12), next = read32(b + 16);
            if (codeEnd >= codeBegin) {
                const unsigned count = codeEnd - codeBegin + 1;
                if (method == 0) {
                    const unsigned firstGlyph = read16(b + 20);
                    if (b + 22 <= data.size())
                        for (unsigned k = 0; k < count; ++k) map_[codeBegin + k] = firstGlyph + k;
                } else if (method == 1) {
                    if (b + 20 + 2u * count <= data.size())
                        for (unsigned k = 0; k < count; ++k) {
                            const unsigned g = read16(b + 20 + 2 * k);
                            if (g != 0xFFFF) map_[codeBegin + k] = g;
                        }
                } else if (method == 2) {
                    const unsigned pairs = read16(b + 20);
                    if (b + 22 + 4u * pairs <= data.size())
                        for (unsigned k = 0; k < pairs; ++k) {
                            const unsigned c = read16(b + 22 + 4 * k), g = read16(b + 24 + 4 * k);
                            if (g != 0xFFFF) map_[c] = g;
                        }
                }
            }
            p = next;
        }
        loaded_ = true;
    }

    bool loaded() const { return loaded_; }

    // Advance of one codepoint in font pixels at scale 1 (advance = right - left; a left
    // bearing byte of 255 means "none", which still gives right + 1).
    float advance(uint32_t cp) const {
        if (!loaded_) return 15.0f;
        auto it = map_.find(cp);
        unsigned g = it == map_.end() ? 0 : it->second;
        if (g >= glyphs_.size()) return 0.0f;
        const Glyph& gl = glyphs_[g];
        return float(gl.right) - float(gl.left == 255 ? -1 : gl.left);
    }

    float lineHeight() const { return em_; }

private:
    bool loaded_ = false;
    std::vector<Glyph> glyphs_;
    std::unordered_map<uint32_t, unsigned> map_;
    float em_ = 31.0f;
};

} // namespace host
