#include "ui/zone_load_screen.h"

#include <cstdio>
#include <cmath>
#include <new>

#include "core/log.h"
#include "game/loading_tips.h"
#include "render/model.h"
#include "render/renderer.h"
#include "render/text_cache.h"
#include "ui/loading_atlas.h"
#include "ui/theme.h"
#include "world/place.h"

namespace tt::ui {

class ZoneLoadScreen::Models {
public:
    render::Model bg;
    render::Model gui;
    render::Model logo;
    C2D_TextBuf text = nullptr;
    render::TextCache cache;
    bool guiTried = false;
};

namespace {
constexpr float kPi = 3.14159265f;

const char* caption(u32 zoneId) {
    switch (tt::world::placeKind(zoneId)) {
    case tt::world::PlaceKind::Street: return "Loading the street...";
    case tt::world::PlaceKind::Interior: return "Loading the building...";
    case tt::world::PlaceKind::Playground: return "Loading the playground...";
    }
    return "Loading...";
}
} // namespace

void ZoneLoadScreen::ensure() {
    if (tried_) return;
    tried_ = true;
    models_ = new (std::nothrow) Models;
    if (!models_) return;
    models_->text = C2D_TextBufNew(512);
    if (!models_->bg.load("romfs:/baked/loading_bg.ttm")) TT_LOG("loadscreen: no loading_bg.ttm");
    if (!models_->logo.load("romfs:/baked/title_logo.ttm")) TT_LOG("loadscreen: no title_logo.ttm");
    models_->guiTried = true;
    if (!models_->gui.load("romfs:/baked/loading_gui.ttm")) TT_LOG("loadscreen: no loading_gui.ttm");
    tip_ = static_cast<u32>(osGetTime() / 4000u) % static_cast<u32>(tt::game::kLoadingTipCount);
}

void ZoneLoadScreen::release() {
    if (!models_) return;
    if (models_->text) C2D_TextBufDelete(models_->text);
    delete models_;
    models_ = nullptr;
    tried_ = false;
}

void ZoneLoadScreen::drawCard(render::Renderer& renderer, const C3D_FVec& eye, const C3D_FVec& at) {
    ensure();
    if (!models_ || !models_->bg.loaded()) return;
    constexpr float kDist = 40.0f;
    const float fx = at.x - eye.x, fy = at.y - eye.y, fz = at.z - eye.z;
    const float len = sqrtf(fx * fx + fy * fy + fz * fz);
    if (len <= 0.0f) return;
    const float halfH = tanf(render::Renderer::kFovYDeg * 0.5f * kPi / 180.0f) * kDist * 1.08f;
    const float halfW = halfH * (400.0f / 240.0f);
    C3D_Mtx m;
    Mtx_Identity(&m);
    Mtx_Translate(&m, eye.x + fx / len * kDist, eye.y + fy / len * kDist, eye.z + fz / len * kDist, true);
    Mtx_RotateZ(&m, atan2f(-fx, fy), true);
    Mtx_RotateX(&m, atan2f(fz, sqrtf(fx * fx + fy * fy)), true);
    Mtx_Scale(&m, halfW / 1.33333f, 1.0f, halfH);
    renderer.drawModel(models_->bg, m);
}

void ZoneLoadScreen::drawUi(render::Renderer& renderer, C3D_RenderTarget* bottom, bool stereo, u32 zoneId,
                            float progress) {
    ensure();
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    const char* label = caption(zoneId);
    char tip[192];
    std::snprintf(tip, sizeof(tip), "%s\n%s", tt::game::kTipTitle,
                  tt::game::kLoadingTips[tip_ % tt::game::kLoadingTipCount]);
    const LoadingArtRect& scroll = kLoadingArtRects[int(LoadingArt::Scroll)];
    auto overlay = [&](C3D_RenderTarget* target, float screenW) {
        if (!target || !models_ || !models_->text) return;
        C2D_SceneBegin(target);
        const float x = screenW * 0.5f - scroll.w * 0.5f;
        const float y = screenW > 360.0f ? 80.0f : 36.0f;
        if (C3D_Tex* tex = models_->gui.loaded() ? models_->gui.texture(0) : nullptr) {
            const float u0 = scroll.x / kLoadingAtlasW, u1 = (scroll.x + scroll.w) / kLoadingAtlasW;
            const float v0 = 1.0f - scroll.y / kLoadingAtlasH;
            const float v1 = 1.0f - (scroll.y + scroll.h) / kLoadingAtlasH;
            Tex3DS_SubTexture sub{uint16_t(scroll.w + 0.5f), uint16_t(scroll.h + 0.5f), u0, v0, u1, v1};
            C2D_DrawImageAt(C2D_Image{tex, &sub}, x, y, 0.5f, nullptr, 1.0f, 1.0f);
        } else {
            theme::paper(x, y, scroll.w, scroll.h, 6.0f);
        }
        const u32 brown = C2D_Color32(0x66, 0x4D, 0x33, 0xFF);
        C2D_Text t;
        models_->cache.parse(&t, models_->text, tip, 512);
        C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, x + 18, y + 16, 0.5f, 0.42f, 0.42f, brown, scroll.w - 36);
        if (models_->logo.loaded() && models_->logo.texture(0) && screenW > 360.0f) {
            Tex3DS_SubTexture sub{256, 128, 0.0f, 1.0f, 1.0f, 0.0f};
            const float lw = 205.0f, lh = 72.0f;
            C2D_DrawImageAt(C2D_Image{models_->logo.texture(0), &sub}, (screenW - lw) * 0.5f, 6.0f, 0.5f, nullptr,
                            lw / 256.0f, lh / 128.0f);
        }
        C2D_Text cap;
        models_->cache.parse(&cap, models_->text, label, 512);
        C2D_DrawText(&cap, C2D_WithColor, screenW > 360.0f ? 73.0f : 16.0f, screenW > 360.0f ? 212.0f : 200.0f, 0.5f,
                     0.5f, 0.5f, C2D_Color32(0, 0, 0x80, 0xFF));
        const float barX = 24.0f, barW = screenW - 48.0f, barY = screenW > 360.0f ? 228.0f : 220.0f;
        C2D_DrawRectSolid(barX, barY, 0.6f, barW, 8.0f, theme::kBinder);
        C2D_DrawRectSolid(barX, barY, 0.7f, barW * progress, 8.0f, theme::kYellow);
    };
    overlay(renderer.topLeft(), 400.0f);
    if (stereo) overlay(renderer.topRight(), 400.0f);
    if (bottom && models_) {
        C2D_TargetClear(bottom, theme::kSkyTop);
        C2D_SceneBegin(bottom);
        // The progress card's texture, stretched to the touch screen, plus the same caption.
        if (C3D_Tex* bg = models_->bg.loaded() ? models_->bg.texture(0) : nullptr) {
            Tex3DS_SubTexture sub{bg->width, bg->height, 0.0f, 1.0f, 1.0f, 0.0f};
            C2D_DrawImageAt(C2D_Image{bg, &sub}, 0.0f, 0.0f, 0.0f, nullptr, 320.0f / bg->width, 240.0f / bg->height);
        } else {
            C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, theme::kSkyTop);
        }
        overlay(bottom, 320.0f);
    }
}

} // namespace tt::ui
