#include "ui/book_pages/page_options.h"
#include "ui/pad_tables.h"
#include "audio/audio.h"
#include "render/ui.h"
#include "net/client.h"
#include <cstdio>
#include <cstdarg>
#include <algorithm>

namespace tt::ui {

namespace {

constexpr u32 kCardBg = C2D_Color32(0xF2, 0xEE, 0xE2, 0xFF);
constexpr u32 kCardBorder = C2D_Color32(0xD0, 0xC8, 0xB8, 0xFF);
constexpr u32 kInk = C2D_Color32(0x20, 0x18, 0x10, 0xFF);
constexpr u32 kDim = C2D_Color32(0x78, 0x70, 0x68, 0xFF);
constexpr u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
constexpr u32 kBtnBg = C2D_Color32(0xE0, 0xD8, 0xC8, 0xFF);
constexpr u32 kActiveBtn = C2D_Color32(0x38, 0x78, 0xC8, 0xFF);
constexpr u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
constexpr u32 kGreen = C2D_Color32(0x40, 0xA8, 0x40, 0xFF);
constexpr u32 kRed = C2D_Color32(0xC8, 0x40, 0x40, 0xFF);

} // namespace

void PageOptions::init() {
    textBuf_ = C2D_TextBufNew(1024);
    pad::buildBookOptions(focus_); // padnav
}

void PageOptions::shutdown() {
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
}

u32 PageOptions::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    if (!bdc.state) return 0;
    // ---- padnav: one table (pad::buildBookOptions) for touch, the D-Pad and A ----
    int activated = -1;
    const u32 used = focus_.handleInput(keysDown, touch, &activated);
    applyOption(activated, *bdc.state);
    // The page keeps the whole D-Pad (L / R turn pages), even where a row has one control.
    return used | (keysDown & kDpadKeys);
    // ---- end padnav ----
}

void PageOptions::applyOption(int id, State& s) {
    switch (id) {
    case pad::book::kIdStereo:
        s.stereo = !s.stereo;
        break;
    case pad::book::kIdLodBias:
        s.lodBias = (s.lodBias == 0) ? 1 : 0;
        break;
    case pad::book::kIdMusicMinus:
        s.musicVol = std::max(0, s.musicVol - 10);
        audio::setMusicVolume(s.musicVol / 100.0f);
        break;
    case pad::book::kIdMusicPlus:
        s.musicVol = std::min(100, s.musicVol + 10);
        audio::setMusicVolume(s.musicVol / 100.0f);
        break;
    case pad::book::kIdSfxMinus:
        s.sfxVol = std::max(0, s.sfxVol - 10);
        audio::setMasterVolume(s.sfxVol / 100.0f);
        break;
    case pad::book::kIdSfxPlus:
        s.sfxVol = std::min(100, s.sfxVol + 10);
        audio::setMasterVolume(s.sfxVol / 100.0f);
        break;
    case pad::book::kIdQuit:
        s.quitRequested = true;
        break;
    default:
        break;
    }
}

void PageOptions::rebuildText(const BookDrawContext& bdc) {
    if (!textBuf_) return;
    C2D_TextBufClear(textBuf_);

    if (bdc.state) {
        lastMusicVol_ = bdc.state->musicVol;
        lastSfxVol_ = bdc.state->sfxVol;
        lastStereo_ = bdc.state->stereo;
        lastLod_ = bdc.state->lodBias;
    }
    lastNumDistricts_ = bdc.numDistricts;
}

void PageOptions::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)x; (void)y; (void)w; (void)h;
    if (!textBuf_) init();

    if (bdc.state) {
        if (bdc.state->musicVol != lastMusicVol_ || bdc.state->sfxVol != lastSfxVol_ ||
            bdc.state->stereo != lastStereo_ || bdc.state->lodBias != lastLod_ ||
            bdc.numDistricts != lastNumDistricts_) {
            rebuildText(bdc);
        }
    }

    // The labels are parsed every frame below, so the buffer must start empty every frame
    // (it filled up after a few frames and every label vanished).
    C2D_TextBufClear(textBuf_);

    // Helper to render formatted text
    auto drawText = [&](float tx, float ty, float scale, u32 col, const char* fmt, ...) {
        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        C2D_Text c2d;
        C2D_TextParse(&c2d, textBuf_, buf);
        C2D_DrawText(&c2d, C2D_WithColor, tx, ty, 0.6f, scale, scale, col);
    };

    // Left card: Game Options
    C2D_DrawRectSolid(10, 26, 0.45f, 145, 178, kCardBg);
    C2D_DrawRectSolid(10, 26, 0.46f, 145, 1, kCardBorder);
    C2D_DrawRectSolid(10, 203, 0.46f, 145, 1, kCardBorder);
    C2D_DrawRectSolid(10, 26, 0.46f, 1, 178, kCardBorder);
    C2D_DrawRectSolid(154, 26, 0.46f, 1, 178, kCardBorder);

    // Right card: Districts (Shard)
    C2D_DrawRectSolid(165, 26, 0.45f, 145, 178, kCardBg);
    C2D_DrawRectSolid(165, 26, 0.46f, 145, 1, kCardBorder);
    C2D_DrawRectSolid(165, 203, 0.46f, 145, 1, kCardBorder);
    C2D_DrawRectSolid(165, 26, 0.46f, 1, 178, kCardBorder);
    C2D_DrawRectSolid(309, 26, 0.46f, 1, 178, kCardBorder);

    const bool stereo = bdc.state ? bdc.state->stereo : false;
    const int lod = bdc.state ? bdc.state->lodBias : 0;
    const int musicVol = bdc.state ? bdc.state->musicVol : 80;
    const int sfxVol = bdc.state ? bdc.state->sfxVol : 100;

    // 1. Stereo 3D button
    C2D_DrawRectSolid(15, 32, 0.5f, 135, 20, stereo ? kActiveBtn : kBtnBg);
    drawText(20, 35, 0.42f, stereo ? kWhite : kInk, "Stereo 3D: %s", stereo ? "On" : "Off");

    // 2. Graphics quality. Developer diagnostics remain behind SELECT in the world UI.
    C2D_DrawRectSolid(15, 60, 0.5f, 135, 20, (lod > 0) ? kActiveBtn : kBtnBg);
    drawText(20, 63, 0.42f, (lod > 0) ? kWhite : kInk, "Graphics: %s", (lod > 0) ? "Fast" : "Normal");

    // 4. Music Volume
    drawText(16, 96, 0.40f, kInk, "Music:");
    C2D_DrawRectSolid(80, 92, 0.5f, 22, 20, kBtnBg);
    drawText(88, 94, 0.50f, kInk, "-");
    drawText(104, 96, 0.38f, kInk, "%d%%", musicVol);
    C2D_DrawRectSolid(126, 92, 0.5f, 22, 20, kBtnBg);
    drawText(134, 94, 0.50f, kInk, "+");

    // 5. SFX Volume
    drawText(16, 124, 0.40f, kInk, "Sound:");
    C2D_DrawRectSolid(80, 120, 0.5f, 22, 20, kBtnBg);
    drawText(88, 122, 0.50f, kInk, "-");
    drawText(104, 124, 0.38f, kInk, "%d%%", sfxVol);
    C2D_DrawRectSolid(126, 120, 0.5f, 22, 20, kBtnBg);
    drawText(134, 122, 0.50f, kInk, "+");

    drawText(18, 150, 0.34f, kDim, "SELECT shows performance");
    drawText(18, 162, 0.34f, kDim, "details while playing.");

    // 5. Quit Game
    C2D_DrawRectSolid(15, 176, 0.5f, 135, 20, kRed);
    drawText(44, 179, 0.42f, kWhite, "Quit Game");

    // Right card: District list
    drawText(172, 32, 0.46f, kInk, "Districts (Shards)");
    C2D_DrawRectSolid(170, 46, 0.46f, 135, 1, kCardBorder);

    int drawnDistricts = 0;
    if (bdc.districts && bdc.numDistricts > 0) {
        for (int i = 0; i < bdc.numDistricts && drawnDistricts < 7; i++) {
            const net::District& d = bdc.districts[i];
            if (!d.active) continue;

            const float dy = 52.0f + drawnDistricts * 21.0f;
            C2D_DrawRectSolid(170, dy, 0.48f, 135, 18, kBtnBg);

            // Status indicator
            C2D_DrawRectSolid(174, dy + 5, 0.5f, 8, 8, d.available ? kGreen : kRed);

            // Name
            drawText(186, dy + 3, 0.38f, kInk, "%.14s", d.name[0] ? d.name : "District");

            // Population / Status tag
            drawText(272, dy + 3, 0.35f, d.available ? kGreen : kDim, "%s", d.available ? "Avail" : "Full");

            drawnDistricts++;
        }
    }

    if (drawnDistricts == 0) {
        drawText(175, 60, 0.40f, kDim, "No districts online");
        drawText(175, 78, 0.36f, kDim, "(Playing offline)");
    }
}

} // namespace tt::ui
