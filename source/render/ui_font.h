// Toontown's interface font (ImpressBT) for every citro2d string.
// romfs:/baked/fonts/impress.bcfnt is baked by tools/pipeline/bake_ui_font.py; its line height
// matches the system font's, so a C2D text scale means the same size with either font.
#pragma once

#include <citro2d.h>

namespace tt::render {

// Main thread, after C2D_Init and before any text is parsed. Missing or bad file: the system
// font is used (logged), never a failure.
bool loadUiFont();
void freeUiFont();
C2D_Font uiFont(); // null = the system font

// Drop-in for C2D_TextParse: every UI string goes through here.
inline const char* textParse(C2D_Text* text, C2D_TextBuf buf, const char* str) {
    return C2D_TextFontParse(text, uiFont(), buf, str);
}

} // namespace tt::render
