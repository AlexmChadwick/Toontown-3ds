// Host stub of <citro2d.h> for the GUI panel tests. Instead of a GPU this records every
// draw call as a rectangle in the recorder (stubs/host_draw_recorder.h); text metrics come
// from the real committed bcfnt font (stubs/font_metrics.h).
#pragma once

#include "citro3d.h"
#include "font_metrics.h"
#include "host_draw_recorder.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct C3D_Tex;

// Tex3DS sub-texture (same layout as libcitro).
struct Tex3DS_SubTexture {
    uint16_t width = 0, height = 0;
    float left = 0, top = 0, right = 0, bottom = 0;
};

struct C2D_Image {
    C3D_Tex* tex = nullptr;
    const Tex3DS_SubTexture* subtex = nullptr;
};

struct C2D_ImageTint {
    uint32_t corners[4] = {};
    float blend[4] = {};
};

enum C2D_TintMode { C2D_TintSolid = 0, C2D_TintMult = 1 };

enum C2D_TextAlignment {
    C2D_AtBaseline = 1u << 0, // ignored by the recorder: the UI draws by top-left
    C2D_AlignLeft = 1u << 1,
    C2D_AlignCenter = 1u << 2,
    C2D_AlignRight = 1u << 3,
    C2D_WithColor = 1u << 4,
    C2D_WordWrap = 1u << 5, // extra wrap-width parameter on C2D_DrawText
};

using C2D_TextBuf = void*;
struct C2D_FontImpl;
using C2D_Font = C2D_FontImpl*;

// The parsed string is kept by value; TextCache copies C2D_Text around, which this allows.
// (map_view checks .buf to parse its static labels once, so the member exists too.)
struct C2D_Text {
    std::string data;
    C2D_TextBuf buf = nullptr;
};

namespace host {

// The single font instance for the whole (unity) test TU.
extern FontMetrics gFont;
extern std::string gFontName; // description used in failure messages

inline void loadHostFont(const char* path, const char* name) {
    gFont.load(path);
    gFontName = name;
}

} // namespace host

// citro3d scissor: recorded as a scissor record so clipped panel content can be checked.
inline void C3D_SetScissor(GPUScissorMode mode, u32 x0, u32 y0, u32 x1, u32 y1) {
    host::gScissor = {int(mode), float(x0), float(y0), float(x1), float(y1)};
    HostRecord r;
    r.kind = HostRecord::Scissor;
    r.x = float(x0);
    r.y = float(y0);
    r.w = float(x1) - float(x0);
    r.h = float(y1) - float(y0);
    r.tag = mode == GPU_SCISSOR_DISABLE ? "disable" : "scissor";
    host::pushRecord(r);
}

// ---- text ------------------------------------------------------------------

inline C2D_TextBuf C2D_TextBufNew(size_t) {
    static int cookie = 0; // unique non-null handles
    return &cookie;
}
inline void C2D_TextBufDelete(C2D_TextBuf) {}
inline void C2D_TextBufClear(C2D_TextBuf) {}
inline size_t C2D_TextBufGetNumGlyphs(C2D_TextBuf) { return 0; }

inline C2D_Font C2D_FontLoad(const char*) { return nullptr; }
inline void C2D_FontFree(C2D_Font) {}
inline void C2D_FontSetFilter(C2D_Font, GPU_TexFilter, GPU_TexFilter) {}

inline const char* C2D_TextFontParse(C2D_Text* text, C2D_Font, C2D_TextBuf, const char* str) {
    text->data = str;
    return str;
}
inline void C2D_TextOptimize(C2D_Text*) {}

// Width in pixels: sum of the real per-glyph advances (right - left) of the committed
// ImpressBT cell, at the given scale; '\n' starts a new line, the width is the widest line.
inline void C2D_TextGetDimensions(const C2D_Text* text, float scaleX, float scaleY, float* outW,
                                   float* outH) {
    float w = 0, lineW = 0;
    int lines = 1;
    const char* s = text ? text->data.c_str() : "";
    uint32_t cp = 0;
    int remain = 0;
    for (const char* p = s; *p; ++p) {
        const uint8_t c = uint8_t(*p);
        if (c < 0x80) {
            cp = c;
            remain = 0;
        } else if (remain) {
            cp = (cp << 6) | (c & 0x3F);
            --remain;
        } else {
            if ((c >> 5) == 0x6) {
                cp = c & 0x1F;
                remain = 1;
            } else if ((c >> 4) == 0xE) {
                cp = c & 0x0F;
                remain = 2;
            } else {
                cp = c & 0x07;
                remain = 3;
            }
        }
        if (remain) continue;
        if (cp == '\n') {
            if (lineW > w) w = lineW;
            lineW = 0;
            ++lines;
        } else {
            lineW += host::gFont.advance(cp) * scaleX;
        }
    }
    if (lineW > w) w = lineW;
    if (outW) *outW = w;
    if (outH) *outH = host::gFont.lineHeight() * scaleY * lines;
}

// Width of one codepoint run (no wrap) at the given scale.
// (unused helper kept zero-size; text width accounting lives in the stub C2D_DrawText)

inline void C2D_DrawText(const C2D_Text* text, u32 flags, float x, float y, float z, float scaleX,
                         float scaleY, ...) {
    C2D_Text raw;
    if (!text) {
        raw.data = "";
        text = &raw;
    }
    va_list ap;
    va_start(ap, scaleY);
    u32 color = 0xFFFFFF00;
    if (flags & C2D_WithColor) color = va_arg(ap, u32);
    float wrapW = 0.0f;
    if (flags & C2D_WordWrap) wrapW = float(va_arg(ap, double));
    va_end(ap);

    HostRecord r;
    r.kind = HostRecord::Text;
    r.scale = scaleX;
    r.text = text->data;
    r.color = (flags & C2D_WithColor) ? color : 0xFFFFFF00u;
    r.z = z;
    r.y = y;
    float w = 0, h = 0;
    C2D_TextGetDimensions(text, scaleX, scaleY, &w, &h);
    if ((flags & C2D_WordWrap) && wrapW > 0.0f && w > wrapW) {
        // Greedy word wrap: words fill lines up to wrapW, like citro2d's C2D_WordWrap.
        const std::string& s = text->data;
        const float lineH = host::gFont.lineHeight() * scaleY;
        float widest = 0, lineW = 0, wordW = 0;
        int lines = 1;
        const auto flushWord = [&]() {
            if (wordW <= 0) return;
            if (lineW > 0 && lineW + wordW > wrapW) {
                if (lineW > widest) widest = lineW;
                ++lines;
                lineW = 0;
            }
            lineW += wordW;
            wordW = 0;
        };
        uint32_t cp = 0;
        int remain = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            const uint8_t c = uint8_t(s[i]);
            if (c < 0x80) {
                cp = c;
                remain = 0;
            } else if (remain) {
                cp = (cp << 6) | (c & 0x3F);
                --remain;
            } else {
                if ((c >> 5) == 0x6) {
                    cp = c & 0x1F;
                    remain = 1;
                } else if ((c >> 4) == 0xE) {
                    cp = c & 0x0F;
                    remain = 2;
                } else {
                    cp = c & 0x07;
                    remain = 3;
                }
            }
            if (remain) continue;
            if (cp == ' ') {
                flushWord();
            } else if (cp == '\n') {
                flushWord();
                if (lineW + wordW > widest) widest = lineW + wordW;
                ++lines;
                lineW = 0;
                wordW = 0;
            } else {
                wordW += host::gFont.advance(cp) * scaleX;
            }
        }
        flushWord();
        if (lineW > widest) widest = lineW;
        w = widest;
        h = lineH * lines;
    }
    r.w = w;
    r.h = h;
    r.x = (flags & C2D_AlignCenter) ? x - w * 0.5f
         : (flags & C2D_AlignRight) ? x - w
                                    : x;
    host::pushRecord(r);
}

// ---- shapes -----------------------------------------------------------------

inline void C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 color) {
    HostRecord r;
    r.kind = HostRecord::Rect;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    r.z = z;
    r.color = color;
    host::pushRecord(r);
}

inline void C2D_DrawRectangle(float x, float y, float z, float w, float h, u32 tl, u32 tr, u32 bl,
                             u32 br) {
    if (tl == tr && tr == bl && bl == br) {
        C2D_DrawRectSolid(x, y, z, w, h, tl);
        return;
    }
    HostRecord r;
    r.kind = HostRecord::RectGrad;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    r.z = z;
    r.color = tl;
    host::pushRecord(r);
}

inline void C2D_DrawTriangle(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float x2,
                             float y2, u32 c2, float z) {
    HostRecord r;
    r.kind = HostRecord::Triangle;
    r.x = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    r.y = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    const float mx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    const float my = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    r.w = mx - r.x;
    r.h = my - r.y;
    r.z = z;
    r.color = c0;
    (void)c1;
    (void)c2;
    (void)y1;
    host::pushRecord(r);
}

inline void C2D_DrawLine(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float thickness,
                        float z) {
    HostRecord r;
    r.kind = HostRecord::Line;
    r.x = x0 < x1 ? x0 : x1;
    r.y = y0 < y1 ? y0 : y1;
    r.w = ((x0 > x1 ? x0 : x1) - r.x) + thickness;
    r.h = ((y0 > y1 ? y0 : y1) - r.y) + thickness;
    if (r.w < thickness) r.w = thickness;
    if (r.h < 1) r.h = 1;
    r.z = z;
    r.color = c0;
    (void)c1;
    host::pushRecord(r);
}

inline void C2D_DrawCircleSolid(float cx, float cy, float z, float radius, u32 color) {
    HostRecord r;
    r.kind = HostRecord::Rect;
    r.x = cx - radius;
    r.y = cy - radius;
    r.w = radius * 2;
    r.h = radius * 2;
    r.z = z;
    r.color = color;
    host::pushRecord(r);
}

inline void C2D_PlainImageTint(C2D_ImageTint* tint, u32 color, float blend) {
    for (int i = 0; i < 4; i++) {
        tint->corners[i] = color;
        tint->blend[i] = blend;
    }
}

// citro2d declares a function with the same name as the struct; a same-scope function
// would hide the class name everywhere below, so dispatch through a macro instead.
#define C2D_ImageTint(tint, color, blend) C2D_PlainImageTint((tint), (color), (blend))

namespace host {
inline void recordImage(const C2D_Image& img, float x, float y, float z, const C2D_ImageTint* tint,
                        float sx = 1.0f, float sy = 1.0f, const char* tag = "image") {
    HostRecord r;
    r.kind = HostRecord::Image;
    r.x = x;
    r.y = y;
    r.w = img.subtex ? img.subtex->width * sx : 0;
    r.h = img.subtex ? img.subtex->height * sy : 0;
    r.z = z;
    r.tag = tag;
    if (img.subtex) {
        r.u0 = img.subtex->left;
        r.v0 = img.subtex->top;
        r.u1 = img.subtex->right;
        r.v1 = img.subtex->bottom;
    }
    if (tint) {
        r.tint = tint->corners[0];
        r.tintAlpha = tint->blend[0];
    } else {
        r.tintAlpha = 0.0f;
    }
    (void)img.tex;
    pushRecord(r);
}
} // namespace host

inline void C2D_DrawImageAt(const C2D_Image& img, float x, float y, float z,
                            const C2D_ImageTint* tint = nullptr, float sx = 1.0f, float sy = 1.0f) {
    host::recordImage(img, x, y, z, tint, sx, sy, "image");
}

inline void C2D_DrawImageAtRotated(const C2D_Image& img, float x, float y, float z, float angle,
                                   const C2D_ImageTint* tint = nullptr, float sx = 1.0f,
                                   float sy = 1.0f) {
    (void)angle;
    host::recordImage(img, x, y, z, tint, sx, sy, "image-rotated");
}

// Unused by layout math, but part of the drawn-frame API the sources call.
inline void C2D_Flush() {}
inline void C2D_TargetClear(C3D_RenderTarget*, u32) {}
inline void C2D_SceneBegin(C3D_RenderTarget*) {}
inline void C2D_SceneEnd() {}
inline void C2D_Prepare() {}

constexpr u32 C2D_Color32(u8 r, u8 g, u8 b, u8 a) {
    return u32(r) | u32(g) << 8 | u32(b) << 16 | u32(a) << 24;
}
inline u32 C2D_Color32f(float r, float g, float b, float a) {
    const auto q = [](float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; };
    return C2D_Color32(u8(q(r) * 255.0f + 0.5f), u8(q(g) * 255.0f + 0.5f), u8(q(b) * 255.0f + 0.5f),
                       u8(q(a) * 255.0f + 0.5f));
}

inline void C2D_SetTintMode(C2D_TintMode) {}
