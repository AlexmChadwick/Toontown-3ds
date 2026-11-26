#include "render/ui_font.h"

#include "core/log.h"
#include "core/memory.h"

namespace tt::render {

namespace {
C2D_Font gFont = nullptr;
C2D_Font gSign = nullptr;
}

bool loadUiFont() {
    if (gFont) return true;
    {
        mem::LinearGuard g; // the glyph sheets live in linear memory
        gFont = C2D_FontLoad("romfs:/baked/fonts/impress.bcfnt");
        gSign = C2D_FontLoad("romfs:/baked/fonts/mickey.bcfnt");
    }
    if (!gFont) {
        if (gSign) {
            mem::LinearGuard g;
            C2D_FontFree(gSign);
            gSign = nullptr;
        }
        TT_LOG("font: impress.bcfnt missing or bad, using the system font");
        return false;
    }
    // UI text is drawn at 0.3-0.8x the baked size: filter so strokes stay smooth.
    C2D_FontSetFilter(gFont, GPU_LINEAR, GPU_LINEAR);
    TT_LOG("font: ImpressBT loaded");
    if (gSign) {
        C2D_FontSetFilter(gSign, GPU_LINEAR, GPU_LINEAR);
        TT_LOG("font: MickeyFont loaded");
    } else {
        TT_LOG("font: mickey.bcfnt missing or bad, signs use ImpressBT");
    }
    return true;
}

void freeUiFont() {
    mem::LinearGuard g;
    if (gSign) {
        C2D_FontFree(gSign);
        gSign = nullptr;
    }
    if (!gFont) return;
    C2D_FontFree(gFont);
    gFont = nullptr;
}

C2D_Font uiFont() { return gFont; }

C2D_Font signFont() { return gSign ? gSign : gFont; }

} // namespace tt::render
