#include "render/ui_font.h"
#include "ui/quest_panel.h"

#include <cstdio>
#include <cstring>

#include "ui/gag_data.h"
#include "ui/pad_tables.h"
#include "ui/theme.h"

namespace tt::ui {

namespace {

constexpr float kW = 320.0f;
const u32 kPaper = theme::kPaper; // ToonTask poster
const u32 kInk = theme::kInk;
const u32 kInkSoft = theme::kInkSoft;
const u32 kReward = C2D_Color32(0x20, 0x70, 0x20, 0xFF);

constexpr float kCardY = pad::quest::kCardY, kCardH = pad::quest::kCardH, kCancelY = pad::quest::kCancelY;

} // namespace

bool QuestPanel::init() {
    buf_ = C2D_TextBufNew(4096);
    return buf_ != nullptr && timer_.init();
}

void QuestPanel::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    buf_ = nullptr;
    timer_.shutdown();
    focus_.clear();
}

void QuestPanel::text(float x, float y, float scale, u32 color, float wrapWidth, const char* s) {
    C2D_Text t;
    tt::render::textParse(&t, buf_, s);
    if (wrapWidth > 0) {
        C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, x, y, 0.6f, scale, scale, color, wrapWidth);
    } else {
        C2D_DrawText(&t, C2D_WithColor, x, y, 0.6f, scale, scale, color);
    }
}

int QuestPanel::wrappedLines(const char* s, float scale, float wrapWidth) {
    C2D_TextBuf tmp = C2D_TextBufNew(256);
    if (!tmp) return 1;
    char line[256];
    int lines = 1;
    size_t len = 0;
    for (const char* p = s; *p;) {
        if (*p == '\n') {
            lines++, len = 0, p++;
            continue;
        }
        const char* end = p; // the next word, with its leading spaces
        while (*end == ' ') end++;
        while (*end && *end != ' ' && *end != '\n') end++;
        const size_t wl = size_t(end - p);
        if (len + wl < sizeof line) {
            memcpy(line + len, p, wl);
            line[len + wl] = 0;
            C2D_Text t;
            C2D_TextBufClear(tmp);
            render::textParse(&t, tmp, line);
            float w = 0, h = 0;
            C2D_TextGetDimensions(&t, scale, scale, &w, &h);
            if (w > wrapWidth && len > 0) {
                lines++;
                while (*p == ' ') p++; // the word starts the new line
                len = size_t(end - p);
                memcpy(line, p, len);
                line[len] = 0;
            } else {
                len += wl;
            }
        } else {
            lines++, len = 0;
        }
        p = end;
    }
    C2D_TextBufDelete(tmp);
    return lines;
}

u32 QuestPanel::handleInput(u32 keysDown, const touchPosition& touch, quest::Talk& talk) {
    // ---- padnav: focus tables (ui/pad_tables.h); the rectangles are the old touch areas ----
    int activated = -1;
    switch (talk.phase()) {
    case quest::Talk::Phase::Pages:
        // Next / Done: A or a tap anywhere advances, as before.
        pad::buildQuestPages(focus_);
        focus_.setFocusById(pad::quest::kIdNext);
        if (keysDown & (KEY_A | KEY_TOUCH)) {
            focus_.logActivate(pad::quest::kIdNext);
            talk.advance();
        }
        break;
    case quest::Talk::Phase::QuestChoice:
        // No card is focused until the D-Pad is used, so mashing A through the dialog
        // pages never accepts a quest by accident.
        pad::buildQuestChoices(focus_, talk.numChoices());
        focus_.handleInput(keysDown, touch, &activated);
        if (activated >= 0 && activated < talk.numChoices()) {
            talk.chooseQuest(activated);
        } else if (activated == pad::quest::kIdCancel || (keysDown & KEY_B)) {
            talk.chooseQuest(-1);
        }
        break;
    case quest::Talk::Phase::TrackChoice:
        pad::buildTrackChoices(focus_, talk.numTracks());
        focus_.handleInput(keysDown, touch, &activated);
        if (activated >= 0 && activated < talk.numTracks()) {
            talk.chooseTrack(activated);
        } else if (activated == pad::quest::kIdCancel || (keysDown & KEY_B)) {
            talk.chooseTrack(-1);
        }
        break;
    default:
        focus_.clear();
        break;
    }
    // ---- end padnav ----
    return keysDown;
}

void QuestPanel::drawTalk(const quest::Talk& talk) {
    C2D_TextBufClear(buf_);
    theme::sky();
    // The NPC's name as a nametag, then what they say in a white chat balloon (upstream speaks
    // quest dialog through the NPC's balloon).
    const char* who = talk.npcName()[0] ? talk.npcName() : "ToonTask";
    theme::roundRect(4, 2, 200, 20, 6, C2D_Color32(0xFF, 0xFF, 0xFF, 0xC0));
    text(10, 3, 0.52f, theme::kBlueDark, 0, who);
    switch (talk.phase()) {
    case quest::Talk::Phase::Waiting:
        theme::paper(4, 26, kW - 8, 40, 10.0f, theme::kWhite, theme::kInk);
        text(16, 34, 0.5f, kInkSoft, 0, "...");
        break;
    case quest::Talk::Phase::Pages: {
        // A chat balloon sized to the page, as upstream's balloons grow with their text.
        if (talk.pageText() != measuredText_ || talk.page() != measuredPage_) {
            measuredText_ = talk.pageText();
            measuredPage_ = talk.page();
            measuredLines_ = wrappedLines(talk.pageText(), 0.5f, kW - 28);
        }
        constexpr float kLineH = 15.0f; // the UI font's line at scale 0.5
        float h = 18.0f + measuredLines_ * kLineH;
        if (h < 44.0f) h = 44.0f;
        if (h > 176.0f) h = 176.0f;
        theme::paper(4, 26, kW - 8, h, 12.0f, theme::kWhite, theme::kInk);
        // The balloon's tail, pointing up at the speaker's name.
        C2D_DrawTriangle(28, 27, theme::kInk, 52, 27, theme::kInk, 30, 16, theme::kInk, 0.5f);
        C2D_DrawTriangle(31, 29, theme::kWhite, 49, 29, theme::kWhite, 32, 20, theme::kWhite, 0.5f);
        text(14, 34, 0.5f, kInk, kW - 28, talk.pageText());
        char nav[32];
        snprintf(nav, sizeof(nav), "%s  (%d/%d)", talk.onLastPage() ? "Done" : "Next", talk.page() + 1, talk.numPages());
        theme::pill(pad::quest::kCancelX, kCancelY, pad::quest::kCancelW, pad::quest::kCancelH, theme::Pill::Cyan);
        C2D_Text navText;
        tt::render::textParse(&navText, buf_, nav);
        theme::textCentered(navText, pad::quest::kCancelX + pad::quest::kCancelW * 0.5f, kCancelY + 4, 0.46f,
                            theme::pillInk(theme::Pill::Cyan), pad::quest::kCancelW - 14, 0.6f);
        break;
    }
    case quest::Talk::Phase::QuestChoice:
        for (int i = 0; i < talk.numChoices(); i++) {
            const quest::Choice& c = talk.choice(i);
            const float y = kCardY + i * (kCardH + 4);
            theme::paper(4, y, kW - 8, kCardH, 6.0f, kPaper);
            if (const quest::QuestDef* q = quest::findQuest(c.questId)) {
                quest::chooseString(*q, scratch_, sizeof(scratch_));
                text(10, y + 3, 0.42f, kInk, kW - 20, scratch_);
            }
            if (c.rewardId && quest::rewardText(c.rewardId, true, scratch_, sizeof(scratch_))) {
                text(10, y + kCardH - 16, 0.38f, kReward, kW - 20, scratch_);
            }
        }
        // QuestChoiceGui: a quit_button Cancel and its ToontownTimer beside it.
        theme::pill(pad::quest::kCancelX, kCancelY, pad::quest::kCancelW, pad::quest::kCancelH, theme::Pill::Yellow);
        text(pad::quest::kCancelX + 28, kCancelY + 4, 0.46f, kInk, 0, "Cancel");
        timer_.draw(static_cast<int>(talk.choiceTimeLeft() + 0.999f), pad::quest::kCancelX + pad::quest::kCancelW + 40,
                    kCancelY + pad::quest::kCancelH * 0.5f, 40);
        break;
    case quest::Talk::Phase::TrackChoice:
        for (int i = 0; i < talk.numTracks(); i++) {
            const float x = 4 + (i % 2) * 158, y = kCardY + (i / 2) * 40;
            const int t = talk.track(i);
            const u32 face = t >= 0 && t < gags::kTracks ? theme::kTrackColors[t] : theme::kGrey;
            theme::button(x, y, 154, 36, face, theme::shade(face, 0.6f));
            text(x + 10, y + 7, 0.52f, kInk, 0, t >= 0 && t < gags::kTracks ? theme::kTrackTitles[t] : "?");
        }
        theme::pill(pad::quest::kCancelX, kCancelY, pad::quest::kCancelW, pad::quest::kCancelH, theme::Pill::Yellow);
        text(pad::quest::kCancelX + 32, kCancelY + 4, 0.46f, kInk, 0, "Later");
        break;
    default:
        break;
    }
    if (talk.phase() != quest::Talk::Phase::Waiting) focus_.drawHighlight();
}

} // namespace tt::ui
