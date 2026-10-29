#include "ui/book_pages/page_options.h"
#include "ui/pad_tables.h"
#include "audio/audio.h"
#include "render/ui.h"
#include "net/client.h"
#include "ui/theme.h"
#include <algorithm>

namespace tt::ui {

namespace {

constexpr float kToggleX = 15, kToggleW = 135, kRowH = 20, kMinusX = 80, kPlusX = 126, kStepW = 24;

} // namespace

void PageOptions::init() {
    text_.init(1024);
    pad::buildBookOptions(focus_); // padnav
}

void PageOptions::shutdown() { text_.shutdown(); }

u32 PageOptions::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    if (!bdc.state) return 0;
    // The Debug tab toggle and the stress-test steppers are developer controls: they appear only
    // once SELECT has turned the Debug tab on (a player never sees them).
    for (int id : {pad::book::kIdDebugHud, pad::book::kIdStressMinus, pad::book::kIdStressPlus})
        focus_.setItemEnabled(focus_.indexOf(id), bdc.state->debugOverlay);
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
    case pad::book::kIdDebugHud:
        s.debugOverlay = !s.debugOverlay;
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
    case pad::book::kIdStressMinus:
        s.stressToons = std::max(0, s.stressToons - 8);
        break;
    case pad::book::kIdStressPlus:
        s.stressToons = std::min(64, s.stressToons + 8);
        break;
    case pad::book::kIdQuit:
        s.quitRequested = true;
        break;
    default:
        break;
    }
}

// OptionsPage: settings on the left card, the district list (ShardPage) on the right.
void PageOptions::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)x; (void)y; (void)w; (void)h;
    theme::paper(10, 26, 145, 178, 7.0f, theme::kPaper, theme::kPaperEdge);
    theme::paper(165, 26, 145, 178, 7.0f, theme::kPaper, theme::kPaperEdge);
    const bool stereo = bdc.state && bdc.state->stereo;
    const bool debug = bdc.state && bdc.state->debugOverlay;
    const int lod = bdc.state ? bdc.state->lodBias : 0;
    const int musicVol = bdc.state ? bdc.state->musicVol : 80;
    const int sfxVol = bdc.state ? bdc.state->sfxVol : 100;
    const int stress = bdc.state ? bdc.state->stressToons : 0;

    // On / off toggles: green when on, like upstream's check buttons.
    auto toggle = [&](float ty, bool on, const char* label, const char* value) {
        theme::button(kToggleX, ty, kToggleW, kRowH + 2, on ? theme::kGreen : theme::kPaperShade,
                      on ? theme::kGreenDark : theme::kGreyDark, false, 6.0f);
        text_.fit(kToggleX + 7, ty + 2, theme::kTextSmall, on ? theme::kWhite : theme::kInk, 74, "%s", label);
        text_.right(kToggleX + kToggleW - 7, ty + 2, theme::kTextSmall, on ? theme::kWhite : theme::kInkSoft, 54, "%s",
                    value);
    };
    toggle(30, stereo, "Stereo 3D", stereo ? "On" : "Off");
    toggle(54, lod > 0, "Detail", lod > 0 ? "Fast" : "Normal");

    // - value + steppers.
    auto stepper = [&](float ty, const char* label, int value, const char* fmt, bool enabled) {
        text_.draw(16, ty + 2, theme::kTextSmall, enabled ? theme::kInk : theme::kGreyDark, "%s", label);
        // OptionsPage's quit_button pills.
        const theme::Pill style = enabled ? theme::Pill::Yellow : theme::Pill::Grey;
        theme::pill(kMinusX, ty, kStepW - 2, kRowH, style);
        theme::pill(kPlusX, ty, kStepW - 2, kRowH, style);
        text_.centered(kMinusX + (kStepW - 2) * 0.5f, ty - 1, theme::kTextTitle, theme::pillInk(style), 0, "-");
        text_.centered(kPlusX + (kStepW - 2) * 0.5f, ty - 1, theme::kTextTitle, theme::pillInk(style), 0, "+");
        text_.centered((kMinusX + kStepW + kPlusX) * 0.5f - 1, ty + 3, theme::kTextTiny,
                       enabled ? theme::kInk : theme::kGreyDark, kPlusX - kMinusX - kStepW + 2, fmt, value);
    };
    stepper(78, "Music", musicVol, "%d%%", true);
    stepper(102, "Sound", sfxVol, "%d%%", true);
    if (debug) {
        toggle(126, true, "Debug tab", "On");
        stepper(150, "Test toons", stress, "%d", true);
    }

    theme::pill(kToggleX, 176, kToggleW, kRowH + 2, theme::Pill::Red);
    text_.centered(kToggleX + kToggleW * 0.5f, 178, theme::kTextBody, theme::pillInk(theme::Pill::Red), 0, "Quit Toontown");

    // Districts: name, and whether it takes more Toons.
    text_.draw(172, 31, theme::kTextBody, theme::kRedDark, "Districts");
    C2D_DrawRectSolid(170, 48, 0.5f, 135, 1, theme::kPaperShade);
    int shown = 0;
    for (int i = 0; bdc.districts && i < bdc.numDistricts && shown < 7; i++) {
        const net::District& d = bdc.districts[i];
        if (!d.active) continue;
        const float dy = 53.0f + shown++ * 21.0f;
        theme::roundRect(170, dy, 135, 19, 5, theme::kPaperShade);
        theme::roundRect(175, dy + 5, 9, 9, 4, d.available ? theme::kGreen : theme::kRed);
        text_.fit(189, dy + 2, theme::kTextSmall, theme::kInk, 78, "%s", d.name[0] ? d.name : "District");
        text_.right(301, dy + 3, theme::kTextTiny, d.available ? theme::kGreenDark : theme::kInkSoft, 0, "%s",
                    d.available ? "Open" : "Full");
    }
    if (shown == 0) {
        text_.draw(175, 58, theme::kTextSmall, theme::kInkSoft, "No districts online");
        text_.draw(175, 75, theme::kTextTiny, theme::kInkSoft, "(Playing offline)");
    }
    focus_.drawHighlight(); // padnav
}

} // namespace tt::ui
