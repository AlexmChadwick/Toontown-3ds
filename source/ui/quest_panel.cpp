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
    return buf_ != nullptr;
}

void QuestPanel::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    buf_ = nullptr;
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
        theme::paper(4, 26, kW - 8, 176, 12.0f, theme::kWhite, theme::kInk);
        // The balloon's tail, pointing up at the speaker's name.
        C2D_DrawTriangle(28, 27, theme::kInk, 52, 27, theme::kInk, 30, 16, theme::kInk, 0.5f);
        C2D_DrawTriangle(31, 29, theme::kWhite, 49, 29, theme::kWhite, 32, 20, theme::kWhite, 0.5f);
        text(14, 34, 0.5f, kInk, kW - 28, talk.pageText());
        char nav[32];
        snprintf(nav, sizeof(nav), "%s  (%d/%d)", talk.onLastPage() ? "Done" : "Next", talk.page() + 1, talk.numPages());
        theme::button(pad::quest::kCancelX, kCancelY, pad::quest::kCancelW, pad::quest::kCancelH + 2, theme::kOrange,
                      theme::shade(theme::kOrange, 0.65f));
        C2D_Text navText;
        tt::render::textParse(&navText, buf_, nav);
        theme::textCentered(navText, pad::quest::kCancelX + pad::quest::kCancelW * 0.5f, kCancelY + 4, 0.46f,
                            theme::kWhite, pad::quest::kCancelW - 8, 0.6f);
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
        theme::button(pad::quest::kCancelX, kCancelY, pad::quest::kCancelW, pad::quest::kCancelH + 2, theme::kGrey,
                      theme::kGreyDark);
        text(pad::quest::kCancelX + 28, kCancelY + 4, 0.46f, kInk, 0, "Cancel");
        snprintf(scratch_, sizeof(scratch_), "%d s", static_cast<int>(talk.choiceTimeLeft()));
        theme::roundRect(268, 2, 48, 20, 6, theme::kYellow);
        text(276, 3, 0.46f, kInk, 0, scratch_);
        break;
    case quest::Talk::Phase::TrackChoice:
        for (int i = 0; i < talk.numTracks(); i++) {
            const float x = 4 + (i % 2) * 158, y = kCardY + (i / 2) * 40;
            const int t = talk.track(i);
            const u32 face = t >= 0 && t < gags::kTracks ? theme::kTrackColors[t] : theme::kGrey;
            theme::button(x, y, 154, 36, face, theme::shade(face, 0.6f));
            text(x + 10, y + 7, 0.52f, kInk, 0, t >= 0 && t < gags::kTracks ? theme::kTrackTitles[t] : "?");
        }
        theme::button(pad::quest::kCancelX, kCancelY, pad::quest::kCancelW, pad::quest::kCancelH + 2, theme::kGrey,
                      theme::kGreyDark);
        text(pad::quest::kCancelX + 32, kCancelY + 4, 0.46f, kInk, 0, "Later");
        break;
    default:
        break;
    }
    if (talk.phase() != quest::Talk::Phase::Waiting) focus_.drawHighlight();
}

void QuestPanel::drawTasks(const net::OwnQuests& q, float y0, float y1) {
    C2D_TextBufClear(buf_);
    char line[160];
    snprintf(line, sizeof(line), "ToonTasks  %d / %u", q.count, unsigned(q.carryLimit));
    text(12, y0, 0.5f, theme::kRedDark, 0, line);
    if (q.count == 0) {
        text(12, y0 + 22, 0.44f, kInkSoft, kW - 24, "No ToonTasks. Visit a Toon HQ to get one!");
        return;
    }
    // Cards share the space between the tasks we carry (up to 64 px each), so a task's text
    // stays on its card.
    float rowH = (y1 - y0 - 20) / q.count;
    if (rowH > 64.0f) rowH = 64.0f;
    for (int i = 0; i < q.count; i++) {
        const net::QuestEntry& e = q.quests[i];
        const float y = y0 + 20 + i * rowH;
        theme::paper(10, y, kW - 20, rowH - 3, 4.0f, theme::kWhite, theme::kPaperShade);
        const quest::QuestDef* def = quest::findQuest(static_cast<uint16_t>(e.questId));
        if (!def) {
            snprintf(line, sizeof(line), "ToonTask %lu", static_cast<unsigned long>(e.questId));
            text(16, y + 2, 0.42f, kInk, 0, line);
            continue;
        }
        const bool done = quest::isComplete(*def, e.progress);
        quest::headline(*def, done, scratch_, sizeof(scratch_));
        text(16, y + 2, 0.4f, done ? C2D_Color32(0x20, 0x80, 0x20, 0xFF) : kInk, 0, scratch_);
        if (done) {
            quest::posterInfo(*def, static_cast<uint16_t>(e.toNpc), true, scratch_, sizeof(scratch_));
        } else {
            quest::objective(*def, scratch_, sizeof(scratch_));
        }
        // The poster's location lines ("Toon HQ\nAny Street\n...") read as one wrapped line here.
        {
            char flat[sizeof(scratch_)];
            size_t n = 0;
            for (const char* c = scratch_; *c && n + 3 < sizeof(flat); ++c) {
                if (*c == '\n') {
                    if (n > 0 && c[1]) {
                        flat[n++] = ',';
                        flat[n++] = ' ';
                    }
                } else {
                    flat[n++] = *c;
                }
            }
            flat[n] = 0;
            memcpy(scratch_, flat, n + 1);
        }
        text(16, y + 14, 0.38f, kInk, kW - 100, scratch_);
        if (!done && quest::progressString(*def, e.progress, line, sizeof(line))) {
            text(kW - 82, y + 14, 0.38f, theme::kBlueDark, 0, line);
        }
    }
}

} // namespace tt::ui
