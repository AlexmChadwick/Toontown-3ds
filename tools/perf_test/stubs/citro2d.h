#pragma once
#include <cstddef>
struct FakeTextBuffer {
    size_t glyphs = 0;
    unsigned generation = 0;
};
using C2D_TextBuf = FakeTextBuffer*;
struct C2D_Text {
    size_t begin = 0, end = 0;
    unsigned generation = 0;
};
size_t C2D_TextBufGetNumGlyphs(C2D_TextBuf);
void C2D_TextBufClear(C2D_TextBuf);
void C2D_TextParse(C2D_Text*, C2D_TextBuf, const char*);
void C2D_TextOptimize(C2D_Text*);
