// Toontown's interface font (ImpressBT) for citro2d strings, and the sign font
// (MickeyFont) for the HQ desk title. Both are baked to the same cell height, so a
// C2D text scale means the same size as the system font.
// romfs:/baked/fonts/impress.bcfnt — tools/pipeline/bake_ui_font.py
// romfs:/baked/fonts/mickey.bcfnt — tools/pipeline/bake_mickey_font.py
#pragma once

#include <citro2d.h>

namespace tt::render {

// Main thread, after C2D_Init and before any text is parsed. A missing interface
// font uses the system font (logged), never a failure. A missing sign font falls
// back to the interface font.
bool loadUiFont();
void freeUiFont();
C2D_Font uiFont(); // null = the system font
C2D_Font signFont(); // MickeyFont, or the interface font if that file is missing

// Drop-in for C2D_TextParse: every UI string goes through here.
inline const char* textParse(C2D_Text* text, C2D_TextBuf buf, const char* str) {
    return C2D_TextFontParse(text, uiFont(), buf, str);
}

// SignFont. The shipped MickeyFont has no lowercase; a-z use the A-Z glyphs,
// which is what TextNode does with this font.
inline const char* textParseSign(C2D_Text* text, C2D_TextBuf buf, const char* str) {
    return C2D_TextFontParse(text, signFont(), buf, str);
}

} // namespace tt::render
