#include "render/ui_font.h"

#include "core/log.h"
#include "core/memory.h"

namespace tt::render {

namespace {
C2D_Font gFont = nullptr;
}

bool loadUiFont() {
    if (gFont) return true;
    {
        mem::LinearGuard g; // the glyph sheet lives in linear memory
        gFont = C2D_FontLoad("romfs:/baked/fonts/impress.bcfnt");
    }
    if (!gFont) {
        TT_LOG("font: impress.bcfnt missing or bad, using the system font");
        return false;
    }
    // UI text is drawn at 0.3-0.8x the baked size: filter so strokes stay smooth.
    C2D_FontSetFilter(gFont, GPU_LINEAR, GPU_LINEAR);
    TT_LOG("font: ImpressBT loaded");
    return true;
}

void freeUiFont() {
    if (!gFont) return;
    mem::LinearGuard g;
    C2D_FontFree(gFont);
    gFont = nullptr;
}

C2D_Font uiFont() { return gFont; }

} // namespace tt::render
