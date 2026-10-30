#include "render/toon_portrait.h"

#include "core/log.h"
#include "render/renderer.h"
#include "world/toon_avatar.h"
#include "world/toon_cache.h"

namespace tt::render {

bool ToonPortrait::render(Renderer& renderer, const world::ToonAvatar& toon, const world::ToonLook& look,
                          u32 clearRgba8) {
    if (!look.ready() || !look.head) return false;
    if (!target_) {
        if (!C3D_TexInitVRAM(&tex_, kSize, kSize, GPU_RGBA8)) {
            TT_LOG("portrait: no VRAM for the head texture");
            return false;
        }
        C3D_TexSetFilter(&tex_, GPU_LINEAR, GPU_LINEAR);
        target_ = C3D_RenderTargetCreateFromTex(&tex_, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH16);
        if (!target_) {
            C3D_TexDelete(&tex_);
            tex_ = C3D_Tex{};
            TT_LOG("portrait: no head render target");
            return false;
        }
        TT_LOG("portrait: head target %dx%d", kSize, kSize);
    }
    // fitAndCenterHead: frame the head model's bounds (at the toon's body scale), seen from the
    // front (ToonHead faces +Y), level with the head as the panel shows it.
    const float* lo = look.head->model.boundsMin();
    const float* hi = look.head->model.boundsMax();
    const float s = look.scale;
    const float cx = (lo[0] + hi[0]) * 0.5f * s, cy = (lo[1] + hi[1]) * 0.5f * s, cz = (lo[2] + hi[2]) * 0.5f * s;
    float extent = hi[0] - lo[0];
    if (hi[2] - lo[2] > extent) extent = hi[2] - lo[2];
    extent *= s;
    constexpr float kFovDeg = 30.0f;
    const float dist = extent * 0.5f * 0.98f / 0.2679f + (hi[1] - lo[1]) * 0.5f * s; // tan(15 deg)
    const C3D_FVec at = FVec3_New(cx, cy, cz);
    const C3D_FVec eye = FVec3_New(cx, cy + dist, cz + dist * 0.02f);
    renderer.beginOffscreen(target_, eye, at, kFovDeg, 1.0f, clearRgba8);
    C3D_Mtx world;
    Mtx_Identity(&world);
    toon.draw(renderer, world);
    renderer.endWorld();
    rendered_ = true;
    return true;
}

bool ToonPortrait::draw(float x, float y, float size, float z) const {
    if (!rendered_ || !target_) return false;
    // The target's rows run top first, like the baked textures (top = t 1).
    static const Tex3DS_SubTexture kSub{kSize, kSize, 0.0f, 1.0f, 1.0f, 0.0f};
    C2D_Image img{const_cast<C3D_Tex*>(&tex_), &kSub};
    C2D_DrawImageAt(img, x, y, z, nullptr, size / kSize, size / kSize);
    return true;
}

void ToonPortrait::release() {
    if (target_) {
        C3D_RenderTargetDelete(target_);
        target_ = nullptr;
        C3D_TexDelete(&tex_);
        tex_ = C3D_Tex{};
    }
    rendered_ = false;
}

} // namespace tt::render
