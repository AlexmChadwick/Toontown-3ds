#include "ui/book_pages/page_options.h"
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
    focus_.clear();
    focus_.addItem(1, 15, 30, 135, 20);   // Stereo 3D
    focus_.addItem(2, 15, 54, 135, 20);   // Debug HUD
    focus_.addItem(3, 15, 78, 135, 20);   // LOD Bias
    focus_.addItem(4, 80, 102, 22, 20);   // Music -
    focus_.addItem(5, 126, 102, 22, 20);  // Music +
    focus_.addItem(6, 80, 126, 22, 20);   // SFX -
    focus_.addItem(7, 126, 126, 22, 20);  // SFX +
    focus_.addItem(8, 80, 150, 22, 20);   // Stress -
    focus_.addItem(9, 126, 150, 22, 20);  // Stress +
    focus_.addItem(10, 15, 176, 135, 20); // Quit Game
}

void PageOptions::shutdown() {
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
    focus_.clear();
}

u32 PageOptions::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    if (!bdc.state) return 0;
    if (focus_.count() == 0) init();

    int activated = -1;
    u32 consumed = focus_.handleInput(keysDown, touch, &activated);

    auto adjust = [&](int id) {
        switch (id) {
        case 1:
            bdc.state->stereo = !bdc.state->stereo;
            break;
        case 2:
            bdc.state->debugOverlay = !bdc.state->debugOverlay;
            break;
        case 3:
            bdc.state->lodBias = (bdc.state->lodBias == 0) ? 1 : 0;
            break;
        case 4:
            bdc.state->musicVol = std::max(0, bdc.state->musicVol - 10);
            audio::setMusicVolume(bdc.state->musicVol / 100.0f);
            break;
        case 5:
            bdc.state->musicVol = std::min(100, bdc.state->musicVol + 10);
            audio::setMusicVolume(bdc.state->musicVol / 100.0f);
            break;
        case 6:
            bdc.state->sfxVol = std::max(0, bdc.state->sfxVol - 10);
            audio::setMasterVolume(bdc.state->sfxVol / 100.0f);
            break;
        case 7:
            bdc.state->sfxVol = std::min(100, bdc.state->sfxVol + 10);
            audio::setMasterVolume(bdc.state->sfxVol / 100.0f);
            break;
        case 8:
            bdc.state->stressToons = std::max(0, bdc.state->stressToons - 8);
            break;
        case 9:
            bdc.state->stressToons = std::min(64, bdc.state->stressToons + 8);
            break;
        case 10:
            bdc.state->quitRequested = true;
            break;
        default:
            break;
        }
    };

    if (activated > 0) {
        adjust(activated);
    }
    return consumed;
}

void PageOptions::rebuildText(const BookDrawContext& bdc) {
    if (!textBuf_) return;
    C2D_TextBufClear(textBuf_);

    if (bdc.state) {
        lastMusicVol_ = bdc.state->musicVol;
        lastSfxVol_ = bdc.state->sfxVol;
        lastStress_ = bdc.state->stressToons;
        lastStereo_ = bdc.state->stereo;
        lastDebug_ = bdc.state->debugOverlay;
        lastLod_ = bdc.state->lodBias;
    }
    lastNumDistricts_ = bdc.numDistricts;
}

void PageOptions::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)x; (void)y; (void)w; (void)h;
    if (!textBuf_) init();

    if (bdc.state) {
        if (bdc.state->musicVol != lastMusicVol_ || bdc.state->sfxVol != lastSfxVol_ ||
            bdc.state->stressToons != lastStress_ || bdc.state->stereo != lastStereo_ ||
            bdc.state->debugOverlay != lastDebug_ || bdc.state->lodBias != lastLod_ ||
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
    const bool debug = bdc.state ? bdc.state->debugOverlay : false;
    const int lod = bdc.state ? bdc.state->lodBias : 0;
    const int musicVol = bdc.state ? bdc.state->musicVol : 80;
    const int sfxVol = bdc.state ? bdc.state->sfxVol : 100;
    const int stress = bdc.state ? bdc.state->stressToons : 0;

    // 1. Stereo 3D button
    C2D_DrawRectSolid(15, 30, 0.5f, 135, 20, stereo ? kActiveBtn : kBtnBg);
    drawText(20, 33, 0.42f, stereo ? kWhite : kInk, "Stereo 3D: %s", stereo ? "On" : "Off");

    // 2. Debug Overlay button
    C2D_DrawRectSolid(15, 54, 0.5f, 135, 20, debug ? kActiveBtn : kBtnBg);
    drawText(20, 57, 0.42f, debug ? kWhite : kInk, "Debug HUD: %s", debug ? "On" : "Off");

    // 3. LOD Bias button
    C2D_DrawRectSolid(15, 78, 0.5f, 135, 20, (lod > 0) ? kActiveBtn : kBtnBg);
    drawText(20, 81, 0.42f, (lod > 0) ? kWhite : kInk, "LOD Bias: %s", (lod > 0) ? "Low (Fast)" : "Normal");

    // 4. Music Volume
    drawText(16, 106, 0.40f, kInk, "Music:");
    C2D_DrawRectSolid(80, 102, 0.5f, 22, 20, kBtnBg);
    drawText(88, 104, 0.50f, kInk, "-");
    drawText(104, 106, 0.38f, kInk, "%d%%", musicVol);
    C2D_DrawRectSolid(126, 102, 0.5f, 22, 20, kBtnBg);
    drawText(134, 104, 0.50f, kInk, "+");

    // 5. SFX Volume
    drawText(16, 130, 0.40f, kInk, "Sound:");
    C2D_DrawRectSolid(80, 126, 0.5f, 22, 20, kBtnBg);
    drawText(88, 128, 0.50f, kInk, "-");
    drawText(104, 130, 0.38f, kInk, "%d%%", sfxVol);
    C2D_DrawRectSolid(126, 126, 0.5f, 22, 20, kBtnBg);
    drawText(134, 128, 0.50f, kInk, "+");

    // 6. Stress Toons
    drawText(16, 154, 0.40f, kInk, "Stress:");
    C2D_DrawRectSolid(80, 150, 0.5f, 22, 20, kBtnBg);
    drawText(88, 152, 0.50f, kInk, "-");
    drawText(106, 154, 0.38f, kInk, "%d", stress);
    C2D_DrawRectSolid(126, 150, 0.5f, 22, 20, kBtnBg);
    drawText(134, 152, 0.50f, kInk, "+");

    // 7. Quit Game
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
    focus_.drawHighlight();
}

} // namespace tt::ui
