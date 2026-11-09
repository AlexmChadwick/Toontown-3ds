// Host-side text metrics for the citro2d stub: parses the real committed interface font
// (romfs/baked/fonts/impress.bcfnt, baked by tools/pipeline/bake_ui_font.py) so every
// measured width uses the same per-glyph numbers the on-device citro2d sees, with a
// deterministic monospace fallback if the file is missing.
//
// BCFNT grammar this stub reads. mkbcfnt output only (the committed fonts):
//   header: 'CFNT'/'CFNU', u16 headerSize @6, u32 nBlocks @16
//   FINF @headerSize: tglpPtr, cwdhPtr, cmapPtr at FINF+16 (u32 absolute offsets)
//   TGLP @ptr-8: [8-byte block header] then (mirroring bake_ui_font.py)
//     u8 cellW, u8 cellH, u8 baseline, u8 maxCharWidth, u32 sheetSize, u16 nSheets,
//     u16 format (11 = A4), u16 sheetCols, u16 sheetLines, u16 sheetW, u16 sheetH,
//     u32 sheetData (absolute); sheet data follows, 8x8 tiles, pixels in Morton order
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
#include <utility>
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
        // CMAP chain: codepoint -> glyph index. The loop only needs the chain header
        // (code range, method, next pointer at p..p+11); each branch bounds-checks its
        // own mapping payload by size. mkbcfnt appends the last CMAP block flush with
        // end-of-file, so the old p+20 guard skipped it and left map_ empty: every
        // advance() silently fell back to glyph 0 (monospace) for all codepoints.
        p = cmap;
        guard = 0;
        while (p && p + 12 <= data.size() && ++guard < 64) {
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
        parseInkSheets(data, tglp);
        loaded_ = true;
    }

    // The A4 glyph sheets, untilied: per glyph index, the ink geometry inside its cell
    // (bake_ui_font.py / mkbcfnt keep the CWDH/CMAP glyph order, so cell k of sheet
    // b = glyph index). Deterministic in the file bytes; no-op on any parse failure.
    void parseInkSheets(const std::string& data, unsigned tglp) {
        // The FINF blocks list pointers at block DATA: the 'TGLP' tag + u32 size sit at
        // ptr - 8, and bake_ui_font.py unpacks the fields at tag + 8, i.e. ptr + 0 here.
        if (tglp < 8 || tglp + 24 > data.size() || data.compare(tglp - 8, 4, "TGLP")) return;
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
        const unsigned cw = rd8(tglp), chh = rd8(tglp + 1);
        const unsigned sheetSize = rd32(tglp + 4), nSheets = rd16(tglp + 8);
        const unsigned fmt = rd16(tglp + 10), cols = rd16(tglp + 12), lines = rd16(tglp + 14);
        const unsigned sw = rd16(tglp + 16), sh = rd16(tglp + 18), dataPtr = rd32(tglp + 20);
        constexpr unsigned kA4 = 11;
        if (!cw || !chh || !nSheets || fmt != kA4 || !cols || !lines || !sw || !sh) return;
        if (dataPtr >= data.size() ||
            dataPtr + size_t(sheetSize) * nSheets > data.size())
            return;
        std::vector<std::vector<uint8_t>> sheets(nSheets);
        for (unsigned s = 0; s < nSheets; ++s) {
            // One sheet: 8x8 tiles in row order, pixels inside a tile in Morton order,
            // each pixel a 4-bit alpha (bake_ui_font.py untile_a4).
            std::vector<uint8_t> px(size_t(sw) * sh, 0);
            size_t i = 0;
            for (unsigned ty = 0; ty < sh; ty += 8)
                for (unsigned tx = 0; tx < sw; tx += 8) {
                    for (unsigned m = 0; m < 64; m += 2, ++i) {
                        unsigned px0 = m, px1 = m + 1;
                        unsigned x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                        for (unsigned b = 0; b < 3; ++b) {
                            x0 |= ((px0 >> (2 * b)) & 1u) << b;
                            y0 |= ((px0 >> (2 * b + 1)) & 1u) << b;
                            x1 |= ((px1 >> (2 * b)) & 1u) << b;
                            y1 |= ((px1 >> (2 * b + 1)) & 1u) << b;
                        }
                        const uint8_t byte = uint8_t(data[dataPtr + size_t(s) * sheetSize + i]);
                        px[size_t(ty + y0) * sw + tx + x0] = byte & 0xF;
                        px[size_t(ty + y1) * sw + tx + x1] = byte >> 4;
                    }
                }
            sheets[s] = std::move(px);
        }
        const unsigned perSheet = cols * lines;
        ink_.resize(glyphs_.size());
        for (size_t g = 0; g < glyphs_.size(); ++g) {
            if (g >= size_t(perSheet) * nSheets) break;
            const unsigned s = unsigned(g) / perSheet, k = unsigned(g) % perSheet;
            const size_t x0 = size_t(k % cols) * cw, y0 = size_t(k / cols) * chh;
            int minRow = -1, maxRow = -1, minCol = 1 << 20, maxCol = -(1 << 20);
            int stroke = 1 << 20;
            for (unsigned y = 0; y < chh; ++y) {
                int cnt = 0, cmin = 1 << 20, cmax = -(1 << 20);
                for (unsigned x = 0; x < cw; ++x) {
                    if (sheets[s][size_t(y0 + y) * sw + x0 + x] == 0) continue;
                    ++cnt;
                    if (int(x) < cmin) cmin = int(x);
                    if (int(x) > cmax) cmax = int(x);
                }
                if (!cnt) continue;
                if (minRow < 0) minRow = int(y);
                maxRow = int(y);
                if (cnt < stroke) stroke = cnt;
                if (cmin < minCol) minCol = cmin;
                if (cmax > maxCol) maxCol = cmax;
            }
            if (minRow < 0) continue; // blank cell: no ink to report
            ink_[g].present = true;
            ink_[g].ink = GlyphInk{maxRow - minRow + 1, stroke, maxCol - minCol + 1};
        }
    }

    bool loaded() const { return loaded_; }

    // Ink geometry of one codepoint, measured from the bcfnt's glyph sheet: the
    // inked row span (rows), the narrowest inked run of columns across those rows
    // (stroke) and the ink bbox width (bboxW), all in font pixels at scale 1.
    // Only nonzero alpha counts as ink. False when the cp is unmapped or the
    // font (or its A4 sheet) could not be parsed.
    struct GlyphInk {
        int rows = 0, stroke = 0, bboxW = 0;
    };
    bool glyphInk(uint32_t cp, GlyphInk* out) const {
        if (!loaded_ || !out) return false;
        auto it = map_.find(cp);
        const unsigned g = it == map_.end() ? 0xFFFFFFFFu : it->second;
        if (g >= ink_.size() || !ink_[g].present) return false;
        *out = ink_[g].ink;
        return true;
    }

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

    struct InkCell {
        bool present = false; // the glyph has ink on its sheet
        GlyphInk ink;
    };
    std::vector<InkCell> ink_;
};

} // namespace host
