#include "core/gpu_sync.h"
#include "core/boot_screen.h"

#include <3ds.h>
#include <citro2d.h>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "core/memory.h"
#include "core/screenshot.h"
#include "render/ui_font.h"

namespace tt::boot {

namespace {

C3D_RenderTarget* gTop = nullptr;
C3D_RenderTarget* gBottom = nullptr;
C2D_TextBuf gBuf = nullptr;
bool gActive = false;
char gStage[96] = "";
float gProgress = 0.0f;
// Autotest runs keep one loading-screen frame (shot_boot_*) as evidence the first frame shows.
int gShot = 0; // 1 = dump the previous frame at the next FrameBegin, 2 = done

// Toontown's loading palette: sky blue, sunny yellow title, deep blue trim.
constexpr u32 kSkyTop = C2D_Color32(0x2E, 0x8C, 0xE0, 0xFF);
constexpr u32 kSkyBottom = C2D_Color32(0x9E, 0xDA, 0xFF, 0xFF);
constexpr u32 kGrass = C2D_Color32(0x5C, 0xC0, 0x3A, 0xFF);
constexpr u32 kGrassDark = C2D_Color32(0x3E, 0x96, 0x24, 0xFF);
constexpr u32 kYellow = C2D_Color32(0xFF, 0xE0, 0x2A, 0xFF);
constexpr u32 kOrange = C2D_Color32(0xFF, 0x8A, 0x1E, 0xFF);
constexpr u32 kInk = C2D_Color32(0x10, 0x24, 0x5A, 0xFF);
constexpr u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
constexpr u32 kBarBack = C2D_Color32(0x16, 0x3A, 0x7A, 0xFF);
constexpr u32 kRed = C2D_Color32(0xD8, 0x2A, 0x2A, 0xFF);

void text(const char* s, float x, float y, float scale, u32 color, bool outline, float maxWidth = 0) {
    if (!gBuf) return;
    C2D_Text t;
    const C2D_Font font = render::uiFont();
    if (font) C2D_TextFontParse(&t, font, gBuf, s);
    else C2D_TextParse(&t, gBuf, s);
    C2D_TextOptimize(&t);
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    if (maxWidth > 0 && w > maxWidth) scale *= maxWidth / w;
    const u32 flags = C2D_WithColor | C2D_AlignCenter;
    if (outline) {
        static const float kOff[8][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}, {-1.5f, -1.5f}, {1.5f, -1.5f}, {-1.5f, 1.5f}, {1.5f, 1.5f}};
        for (const auto& o : kOff) C2D_DrawText(&t, flags, x + o[0], y + o[1], 0.5f, scale, scale, kInk);
    }
    C2D_DrawText(&t, flags, x, y, 0.5f, scale, scale, color);
}

void drawBackground(float w, float h) {
    C2D_DrawRectangle(0, 0, 0, w, h * 0.78f, kSkyTop, kSkyTop, kSkyBottom, kSkyBottom);
    C2D_DrawRectangle(0, h * 0.78f, 0, w, h * 0.22f, kGrass, kGrass, kGrassDark, kGrassDark);
    // Puffy cartoon clouds.
    const u32 cloud = C2D_Color32(0xFF, 0xFF, 0xFF, 0xD8);
    C2D_DrawCircleSolid(w * 0.16f, h * 0.20f, 0, 16, cloud);
    C2D_DrawCircleSolid(w * 0.22f, h * 0.17f, 0, 21, cloud);
    C2D_DrawCircleSolid(w * 0.29f, h * 0.21f, 0, 15, cloud);
    C2D_DrawCircleSolid(w * 0.74f, h * 0.30f, 0, 13, cloud);
    C2D_DrawCircleSolid(w * 0.80f, h * 0.26f, 0, 18, cloud);
    C2D_DrawCircleSolid(w * 0.86f, h * 0.31f, 0, 12, cloud);
}

// The game's own targets (renderer, bottom UI) take over the screens once they are created,
// which unlinks ours (and an unlinked C2D target also loses its rotated projection). Re-link
// every frame so the loading and error screens always reach the LCDs. Called after
// C3D_FrameBegin, when the previous frame's transfers are done.
void linkScreens() {
    constexpr u32 kFlags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                           GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                           GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    C3D_RenderTargetSetOutput(gTop, GFX_TOP, GFX_LEFT, kFlags);
    C3D_RenderTargetSetOutput(gBottom, GFX_BOTTOM, GFX_LEFT, kFlags);
}

void drawFrame() {
    if (!gTop || !gBottom) return;
    C2D_TextBufClear(gBuf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    linkScreens();
    if (gShot == 1) {
        tt::shot::dumpTarget(gTop, "sdmc:/3ds/toontown3ds/shot_boot_top.bin");
        tt::shot::dumpTarget(gBottom, "sdmc:/3ds/toontown3ds/shot_boot_bottom.bin");
        gShot = 2;
    }
    C2D_TargetClear(gTop, kSkyTop);
    C2D_SceneBegin(gTop);
    drawBackground(400, 240);
    text("TOONTOWN", 200, 56, 1.45f, kYellow, true, 360);
    text("3DS", 200, 112, 0.9f, kOrange, true);
    text("Loading...", 200, 156, 0.75f, kWhite, true);
    // Progress bar.
    const float bx = 70, by = 196, bw = 260, bh = 16;
    C2D_DrawRectSolid(bx - 3, by - 3, 0, bw + 6, bh + 6, kInk);
    C2D_DrawRectSolid(bx, by, 0, bw, bh, kBarBack);
    float p = gProgress < 0 ? 0 : (gProgress > 1 ? 1 : gProgress);
    if (p > 0) C2D_DrawRectangle(bx, by, 0, bw * p, bh, kYellow, kYellow, kOrange, kOrange);

    C2D_TargetClear(gBottom, kSkyBottom);
    C2D_SceneBegin(gBottom);
    drawBackground(320, 240);
    text(gStage, 160, 92, 0.6f, kWhite, true, 300);
    text("Hang tight, toon!", 160, 140, 0.55f, kYellow, true);
    char mem[64];
    const mem::HeapReport r = mem::heapReport();
    snprintf(mem, sizeof(mem), "mem %u/%u MB", static_cast<unsigned>(r.heap >> 20), static_cast<unsigned>(r.linear >> 20));
    text(mem, 160, 216, 0.4f, kWhite, false);
    C3D_FrameEnd(0);
}

} // namespace

void begin() {
    if (gActive) return;
    gBuf = C2D_TextBufNew(512);
    gTop = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    gBottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    gActive = gTop && gBottom && gBuf;
    if (!gActive) TT_LOG("boot: loading screen unavailable (top %p bottom %p buf %p)", static_cast<void*>(gTop),
                         static_cast<void*>(gBottom), static_cast<void*>(gBuf));
}

void stage(const char* what, float progress) {
    TT_LOG("boot: %s", what);
    log::flush();
    snprintf(gStage, sizeof(gStage), "%s", what);
    gProgress = progress;
    if (gActive) drawFrame();
    if (gShot == 0 && progress >= 0.4f) {
        FILE* f = fopen("sdmc:/3ds/toontown3ds/autotest", "r");
        gShot = f ? 1 : 2;
        if (f) fclose(f);
    }
}

void end() {
    if (!gActive) return;
    gActive = false;
    ::tt::gpu::frameSync(); // the GPU is done with both targets
    if (gTop) C3D_RenderTargetDelete(gTop);
    if (gBottom) C3D_RenderTargetDelete(gBottom);
    if (gBuf) C2D_TextBufDelete(gBuf);
    gTop = gBottom = nullptr;
    gBuf = nullptr;
}

bool active() { return gActive; }

void showFatal(const char* title, const char* detail) {
    TT_LOG("FATAL: %s: %s", title, detail);
    log::flush();
    if (!gActive) begin();
    if (!gActive) return;
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        C2D_TextBufClear(gBuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        linkScreens();
        C2D_TargetClear(gTop, kSkyTop);
        C2D_SceneBegin(gTop);
        drawBackground(400, 240);
        text("Uh oh, toon!", 200, 40, 1.1f, kYellow, true, 360);
        text(title, 200, 104, 0.7f, kWhite, true, 380);
        text("Press START to go back to the HOME Menu.", 200, 186, 0.5f, kWhite, true, 380);
        C2D_TargetClear(gBottom, kSkyBottom);
        C2D_SceneBegin(gBottom);
        drawBackground(320, 240);
        // The detail can be long: wrap it on '\n'.
        char lines[256];
        snprintf(lines, sizeof(lines), "%s", detail);
        float y = 30;
        for (char* line = strtok(lines, "\n"); line && y < 220; line = strtok(nullptr, "\n"), y += 26) {
            text(line, 160, y, 0.5f, kWhite, true, 304);
        }
        C2D_DrawRectSolid(0, 236, 0, 320, 4, kRed);
        C3D_FrameEnd(0);
    }
}

} // namespace tt::boot
