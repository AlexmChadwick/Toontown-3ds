#include "render/ui_font.h"
#include "ui/hud.h"

#include <cstdio>

#include "core/log.h"

#include "ui/laff_meter.h"

namespace tt::ui {

namespace {

// The meter is larger than upstream's share of the screen: at 64 px the numbers in the eyes were
// unreadable on the 400x240 top screen. The bottom-left margin stays 6 px.
constexpr float kLaffSize = 88.0f; // on-screen size of one atlas cell (the whole meter)
constexpr float kLaffX = 6.0f + kLaffSize * 0.5f, kLaffY = 234.0f - kLaffSize * 0.5f;

} // namespace

bool Hud::init() {
    raceBuf_ = C2D_TextBufNew(96);
    toastBuf_ = C2D_TextBufNew(160);
    return laff_.init() && raceBuf_ != nullptr && toastBuf_ != nullptr;
}

void Hud::shutdown() {
    laff_.shutdown();
    if (raceBuf_) C2D_TextBufDelete(raceBuf_);
    if (toastBuf_) C2D_TextBufDelete(toastBuf_);
    raceBuf_ = toastBuf_ = nullptr;
}

namespace {

// Photo Fun viewfinder (DistributedPhotoGame's photo_game_viewfinder frame, drawn as flat
// bars): corner brackets around the sight, which stays at the centre of the top screen because
// the tripod camera always looks where the swivel points.
constexpr float kVfLeft = 120.0f, kVfRight = 280.0f, kVfTop = 70.0f, kVfBottom = 170.0f;
constexpr float kVfArm = 22.0f, kVfThick = 3.0f;
constexpr float kVfSightArm = 9.0f;
constexpr float kVfZ = 0.45f;

void drawViewfinder(C3D_RenderTarget* top) {
    C2D_SceneBegin(top);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    const u32 c = C2D_Color32(0xFF, 0xFF, 0xFF, 0xE0);
    const float cx = (kVfLeft + kVfRight) * 0.5f;
    const float cy = (kVfTop + kVfBottom) * 0.5f;
    // Top-left, top-right, bottom-left, bottom-right corners.
    C2D_DrawRectSolid(kVfLeft, kVfTop, kVfZ, kVfArm, kVfThick, c);
    C2D_DrawRectSolid(kVfLeft, kVfTop, kVfZ, kVfThick, kVfArm, c);
    C2D_DrawRectSolid(kVfRight - kVfArm, kVfTop, kVfZ, kVfArm, kVfThick, c);
    C2D_DrawRectSolid(kVfRight - kVfThick, kVfTop, kVfZ, kVfThick, kVfArm, c);
    C2D_DrawRectSolid(kVfLeft, kVfBottom - kVfThick, kVfZ, kVfArm, kVfThick, c);
    C2D_DrawRectSolid(kVfLeft, kVfBottom - kVfArm, kVfZ, kVfThick, kVfArm, c);
    C2D_DrawRectSolid(kVfRight - kVfArm, kVfBottom - kVfThick, kVfZ, kVfArm, kVfThick, c);
    C2D_DrawRectSolid(kVfRight - kVfThick, kVfBottom - kVfArm, kVfZ, kVfThick, kVfArm, c);
    // Sight.
    C2D_DrawRectSolid(cx - kVfSightArm, cy - 0.5f, kVfZ, 2 * kVfSightArm, 1.0f, c);
    C2D_DrawRectSolid(cx - 0.5f, cy - kVfSightArm, kVfZ, 1.0f, 2 * kVfSightArm, c);
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}

} // namespace

void Hud::draw(C3D_RenderTarget* top, const HudValues& v) {
    if (v.toast) {
        if (v.toast != lastToast_) {
            C2D_TextBufClear(toastBuf_);
            tt::render::textParse(&toastText_, toastBuf_, v.toast);
            C2D_TextOptimize(&toastText_);
            lastToast_ = v.toast;
        }
        C2D_SceneBegin(top);
        float w, h;
        C2D_TextGetDimensions(&toastText_, 0.6f, 0.6f, &w, &h);
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        C2D_DrawRectSolid(200 - w * 0.5f - 10, 70, 0.5f, w + 20, h + 12, C2D_Color32(0x10, 0x18, 0x30, 0xD0));
        C2D_DrawText(&toastText_, C2D_WithColor, 200 - w * 0.5f, 76, 0.5f, 0.6f, 0.6f,
                     C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF));
        C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
    }
    if (v.photoViewfinder) drawViewfinder(top);
    if (!v.online) return; // stats come from the server
    C2D_SceneBegin(top);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR); // overlay: never hidden by the world
    if (v.racing) {
        C2D_TextBufClear(raceBuf_);
        char race[96];
        snprintf(race, sizeof(race), "SPEED %d   LAP %d/%d   PLACE %d", v.raceSpeed, v.raceLap, v.raceLaps, v.racePlace);
        tt::render::textParse(&raceText_, raceBuf_, race);
        C2D_TextOptimize(&raceText_);
        C2D_DrawText(&raceText_, C2D_WithColor, 42, 16, 0.5f, 0.65f, 0.65f, C2D_Color32(0xFF, 0xF0, 0x70, 0xFF));
    }

    // Upstream's HUD is the laff meter alone; jellybeans show on the book's Gags page and the
    // Toon tab (InventoryPage.moneyDisplay), not over the world. While the bottom screen shows
    // our laff (its tab bar pocket, the battle panel's Toon cards) the world stays clear.
    if (!v.laffOnBottom) {
        LaffFace face;
        face.hp = v.hp;
        face.maxHp = v.maxHp;
        for (int i = 0; i < 3; i++) face.rgb[i] = v.headRgb[i];
        face.species = v.species;
        laff_.draw(face, kLaffX, kLaffY, kLaffSize);
    }
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL); // citro2d's default
}

} // namespace tt::ui
