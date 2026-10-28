// Cached, printf-style text for bottom-screen panels (book pages, tabs).
//
// Parsing citro2d text costs milliseconds (AGENTS.md), so every string goes through a
// render::TextCache: an unchanged string is never laid out twice. Sizes come from the shared
// theme scale ladder (theme::kTextTitle .. kTextTiny) so every page reads at 3DS resolution.
#pragma once

#include <citro2d.h>
#include <cstddef>

#include "render/text_cache.h"

namespace tt::ui {

class PageText {
public:
    bool init(size_t glyphs = 2048);
    void shutdown();

    // Each returns the drawn width. maxW > 0 shrinks a too-long string to fit.
    float draw(float x, float y, float scale, u32 color, const char* fmt, ...) __attribute__((format(printf, 6, 7)));
    float fit(float x, float y, float scale, u32 color, float maxW, const char* fmt, ...)
        __attribute__((format(printf, 7, 8)));
    float centered(float cx, float y, float scale, u32 color, float maxW, const char* fmt, ...)
        __attribute__((format(printf, 7, 8)));
    float right(float rx, float y, float scale, u32 color, float maxW, const char* fmt, ...)
        __attribute__((format(printf, 7, 8)));
    // Word-wrapped left-aligned text in at most maxLines lines of lineH px; the last line shrinks
    // to fit whatever is left. Newlines in str are treated as spaces. Returns the lines used.
    int wrap(float x, float y, float scale, u32 color, float maxW, float lineH, int maxLines, const char* str);
    // Draw depth for the following calls (citro2d z; panels draw text above their cards).
    void setDepth(float z) { z_ = z; }

private:
    enum class Align { Left, Center, Right };
    float emit(Align a, float x, float y, float scale, u32 color, float maxW, const char* str);

    C2D_TextBuf buf_ = nullptr;
    size_t cap_ = 0;
    float z_ = 0.6f;
    render::TextCache cache_;
};

} // namespace tt::ui
