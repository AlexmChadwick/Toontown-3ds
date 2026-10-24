#include "ui/hud.h"

#include <cstdio>

namespace tt::ui {

namespace {

constexpr float kLaffX = 34.0f, kLaffY = 206.0f, kLaffR = 24.0f;
constexpr float kJarX = 352.0f, kJarY = 190.0f;

} // namespace

bool Hud::init() {
    buf_ = C2D_TextBufNew(64);
    toastBuf_ = C2D_TextBufNew(160);
    return buf_ != nullptr && toastBuf_ != nullptr;
}

void Hud::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    if (toastBuf_) C2D_TextBufDelete(toastBuf_);
    buf_ = toastBuf_ = nullptr;
}

void Hud::draw(C3D_RenderTarget* top, const HudValues& v) {
    if (v.toast) {
        if (v.toast != lastToast_) {
            C2D_TextBufClear(toastBuf_);
            C2D_TextParse(&toastText_, toastBuf_, v.toast);
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
    if (!v.online) return; // stats come from the server
    // Re-parse text only when the numbers change (citro2d parsing is expensive).
    if (v.hp != lastHp_ || v.maxHp != lastMaxHp_ || v.money != lastMoney_) {
        C2D_TextBufClear(buf_);
        char s[32];
        snprintf(s, sizeof(s), "%d", v.hp > 0 ? v.hp : 0); // LaffMeter never shows a negative laff
        C2D_TextParse(&hpText_, buf_, s);
        C2D_TextOptimize(&hpText_);
        snprintf(s, sizeof(s), "%d", v.money);
        C2D_TextParse(&moneyText_, buf_, s);
        C2D_TextOptimize(&moneyText_);
        lastHp_ = v.hp;
        lastMaxHp_ = v.maxHp;
        lastMoney_ = v.money;
    }
    C2D_SceneBegin(top);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR); // overlay: never hidden by the world

    // Laff meter: a face in the toon's head colour that empties (darkens) as laff drops.
    const float frac = v.maxHp > 0 ? static_cast<float>(v.hp) / static_cast<float>(v.maxHp) : 0.0f;
    const u32 outline = C2D_Color32(0x20, 0x20, 0x30, 0xFF);
    const u32 face = C2D_Color32f(v.headRgb[0], v.headRgb[1], v.headRgb[2], 1.0f);
    const u32 empty = C2D_Color32f(v.headRgb[0] * 0.35f, v.headRgb[1] * 0.35f, v.headRgb[2] * 0.35f, 1.0f);
    C2D_DrawRectSolid(kLaffX - kLaffR - 2, kLaffY - kLaffR - 2, 0.5f, 2 * kLaffR + 4, 2 * kLaffR + 4, outline);
    C2D_DrawRectSolid(kLaffX - kLaffR, kLaffY - kLaffR, 0.5f, 2 * kLaffR, 2 * kLaffR, empty);
    const float filled = 2 * kLaffR * (frac < 0 ? 0 : frac > 1 ? 1 : frac);
    C2D_DrawRectSolid(kLaffX - kLaffR, kLaffY + kLaffR - filled, 0.5f, 2 * kLaffR, filled, face);
    // Eyes and smile (frown when low), drawn as rects so they survive any GPU state.
    const u32 ink = C2D_Color32(0x10, 0x10, 0x10, 0xFF);
    C2D_DrawRectSolid(kLaffX - 10, kLaffY - 12, 0.5f, 5, 8, ink);
    C2D_DrawRectSolid(kLaffX + 5, kLaffY - 12, 0.5f, 5, 8, ink);
    float w, h;
    C2D_TextGetDimensions(&hpText_, 0.6f, 0.6f, &w, &h);
    C2D_DrawText(&hpText_, C2D_WithColor, kLaffX - w * 0.5f, kLaffY - 2, 0.5f, 0.6f, 0.6f, ink);

    // Jellybean jar.
    const u32 glass = C2D_Color32(0xD0, 0xE8, 0xFF, 0xC0), bean = C2D_Color32(0xF0, 0x50, 0x80, 0xFF);
    C2D_DrawRectSolid(kJarX - 2, kJarY - 2, 0.5f, 42, 46, outline);
    C2D_DrawRectSolid(kJarX, kJarY, 0.5f, 38, 42, glass);
    const float beans = v.maxMoney > 0 ? 34.0f * v.money / v.maxMoney : 0.0f;
    C2D_DrawRectSolid(kJarX + 2, kJarY + 40 - (beans > 34 ? 34 : beans), 0.5f, 34, beans > 34 ? 34 : beans, bean);
    C2D_TextGetDimensions(&moneyText_, 0.55f, 0.55f, &w, &h);
    C2D_DrawText(&moneyText_, C2D_WithColor, kJarX + 19 - w * 0.5f, kJarY + 12, 0.5f, 0.55f, 0.55f, ink);
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL); // citro2d's default
}

} // namespace tt::ui
