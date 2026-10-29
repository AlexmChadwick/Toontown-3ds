#include "ui/page_text.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

#include "ui/theme.h"

namespace tt::ui {

bool PageText::init(size_t glyphs) {
    if (!cache_) cache_ = new (std::nothrow) render::TextCache;
    if (!cache_) return false;
    cache_->clear();
    if (!buf_) buf_ = C2D_TextBufNew(glyphs);
    cap_ = glyphs;
    return buf_ != nullptr;
}

void PageText::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    buf_ = nullptr;
    delete cache_;
    cache_ = nullptr;
}

float PageText::emit(Align a, float x, float y, float scale, u32 color, float maxW, const char* str) {
    if (!buf_ || !cache_ || !str[0]) return 0.0f;
    C2D_Text t;
    cache_->parse(&t, buf_, str, cap_);
    float w = 0.0f;
    C2D_TextGetDimensions(&t, scale, scale, &w, nullptr);
    if (maxW > 0.0f && w > maxW) {
        scale *= maxW / w;
        w = maxW;
    }
    const float left = a == Align::Left ? x : a == Align::Center ? x - w * 0.5f : x - w;
    C2D_DrawText(&t, C2D_WithColor, left, y, z_, scale, scale, color);
    return w;
}

#define TT_PAGE_TEXT_FORMAT(last)          \
    char line[192];                        \
    va_list ap;                            \
    va_start(ap, last);                    \
    vsnprintf(line, sizeof line, last, ap); \
    va_end(ap)

float PageText::draw(float x, float y, float scale, u32 color, const char* fmt, ...) {
    TT_PAGE_TEXT_FORMAT(fmt);
    return emit(Align::Left, x, y, scale, color, 0.0f, line);
}

float PageText::fit(float x, float y, float scale, u32 color, float maxW, const char* fmt, ...) {
    TT_PAGE_TEXT_FORMAT(fmt);
    return emit(Align::Left, x, y, scale, color, maxW, line);
}

float PageText::centered(float cx, float y, float scale, u32 color, float maxW, const char* fmt, ...) {
    TT_PAGE_TEXT_FORMAT(fmt);
    return emit(Align::Center, cx, y, scale, color, maxW, line);
}

float PageText::right(float rx, float y, float scale, u32 color, float maxW, const char* fmt, ...) {
    TT_PAGE_TEXT_FORMAT(fmt);
    return emit(Align::Right, rx, y, scale, color, maxW, line);
}

void PageText::countDisc(float tileX, float tileY, float tileW, float tileH, unsigned n) {
    if (!buf_ || !cache_) return;
    char line[8];
    snprintf(line, sizeof line, "%u", n);
    C2D_Text t;
    cache_->parse(&t, buf_, line, cap_);
    theme::countDisc(t, tileX, tileY, tileW, tileH, z_);
}

#undef TT_PAGE_TEXT_FORMAT

int PageText::wrap(float x, float y, float scale, u32 color, float maxW, float lineH, int maxLines,
                   const char* str) {
    if (!buf_ || !cache_ || !str) return 0;
    char rest[192];
    size_t n = 0;
    for (const char* c = str; *c && n + 1 < sizeof rest; ++c) rest[n++] = *c == '\n' ? ' ' : *c;
    rest[n] = 0;
    const char* p = rest;
    int lines = 0;
    while (*p && lines < maxLines) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (lines == maxLines - 1) { // the last line takes the rest, shrunk if needed
            emit(Align::Left, x, y + lines * lineH, scale, color, maxW, p);
            return lines + 1;
        }
        // The longest prefix ending at a word break that fits maxW (measured per candidate).
        char line[192];
        size_t best = 0;
        for (size_t i = 0;; ++i) {
            if (p[i] == ' ' || p[i] == 0) {
                memcpy(line, p, i);
                line[i] = 0;
                C2D_Text t;
                cache_->parse(&t, buf_, line, cap_);
                float w = 0.0f;
                C2D_TextGetDimensions(&t, scale, scale, &w, nullptr);
                if (w > maxW && best) break;
                best = i;
                if (w > maxW || p[i] == 0) break;
            }
        }
        memcpy(line, p, best);
        line[best] = 0;
        emit(Align::Left, x, y + lines * lineH, scale, color, maxW, line);
        p += best;
        ++lines;
    }
    return lines;
}

} // namespace tt::ui
