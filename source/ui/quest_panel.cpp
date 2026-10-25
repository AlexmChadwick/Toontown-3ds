#include "ui/quest_panel.h"

#include <cstdio>
#include <cstring>

#include "ui/gag_data.h"
#include "ui/pad_tables.h"

namespace tt::ui {

namespace {

constexpr float kW = 320.0f;
const u32 kBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kPanel = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
const u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kDim = C2D_Color32(0xA8, 0xB4, 0xCC, 0xFF);
const u32 kPaper = C2D_Color32(0xF4, 0xEE, 0xD8, 0xFF); // ToonTask poster
const u32 kInk = C2D_Color32(0x30, 0x28, 0x20, 0xFF);

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
    C2D_TextParse(&t, buf_, s);
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
    C2D_DrawRectSolid(0, 0, 0.5f, kW, 240, kBg);
    text(8, 4, 0.55f, kAccent, 0, talk.npcName()[0] ? talk.npcName() : "ToonTask");
    switch (talk.phase()) {
    case quest::Talk::Phase::Waiting:
        text(8, 40, 0.5f, kDim, 0, "...");
        break;
    case quest::Talk::Phase::Pages: {
        C2D_DrawRectSolid(4, 24, 0.5f, kW - 8, 176, kPaper);
        text(12, 30, 0.5f, kInk, kW - 24, talk.pageText());
        char nav[32];
        snprintf(nav, sizeof(nav), "%s  (%d/%d)", talk.onLastPage() ? "Done" : "Next", talk.page() + 1, talk.numPages());
        C2D_DrawRectSolid(110, kCancelY, 0.5f, 100, 26, kAccent);
        text(122, kCancelY + 5, 0.45f, kBg, 0, nav);
        break;
    }
    case quest::Talk::Phase::QuestChoice:
        for (int i = 0; i < talk.numChoices(); i++) {
            const quest::Choice& c = talk.choice(i);
            const float y = kCardY + i * (kCardH + 4);
            C2D_DrawRectSolid(4, y, 0.5f, kW - 8, kCardH, kPaper);
            if (const quest::QuestDef* q = quest::findQuest(c.questId)) {
                quest::chooseString(*q, scratch_, sizeof(scratch_));
                text(10, y + 3, 0.42f, kInk, kW - 20, scratch_);
            }
            if (c.rewardId && quest::rewardText(c.rewardId, true, scratch_, sizeof(scratch_))) {
                text(10, y + kCardH - 16, 0.38f, C2D_Color32(0x20, 0x70, 0x20, 0xFF), kW - 20, scratch_);
            }
        }
        C2D_DrawRectSolid(110, kCancelY, 0.5f, 100, 26, kPanel);
        text(136, kCancelY + 5, 0.45f, kWhite, 0, "Cancel");
        snprintf(scratch_, sizeof(scratch_), "%d s", static_cast<int>(talk.choiceTimeLeft()));
        text(280, 4, 0.45f, kDim, 0, scratch_);
        break;
    case quest::Talk::Phase::TrackChoice:
        for (int i = 0; i < talk.numTracks(); i++) {
            const float x = 4 + (i % 2) * 158, y = kCardY + (i / 2) * 40;
            C2D_DrawRectSolid(x, y, 0.5f, 154, 36, kPanel);
            const int t = talk.track(i);
            text(x + 10, y + 9, 0.5f, kWhite, 0, t >= 0 && t < gags::kTracks ? gags::kTrackNames[t] : "?");
        }
        C2D_DrawRectSolid(110, kCancelY, 0.5f, 100, 26, kPanel);
        text(136, kCancelY + 5, 0.45f, kWhite, 0, "Later");
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
    text(8, y0, 0.5f, kAccent, 0, line);
    if (q.count == 0) {
        text(8, y0 + 22, 0.42f, kDim, kW - 16, "No ToonTasks. Visit a Toon HQ to get one!");
        return;
    }
    // Cards share the space between the tasks we carry (up to 64 px each), so a task's text
    // stays on its card.
    float rowH = (y1 - y0 - 20) / q.count;
    if (rowH > 64.0f) rowH = 64.0f;
    for (int i = 0; i < q.count; i++) {
        const net::QuestEntry& e = q.quests[i];
        const float y = y0 + 20 + i * rowH;
        C2D_DrawRectSolid(4, y, 0.5f, kW - 8, rowH - 3, kPaper);
        const quest::QuestDef* def = quest::findQuest(static_cast<uint16_t>(e.questId));
        if (!def) {
            snprintf(line, sizeof(line), "ToonTask %lu", static_cast<unsigned long>(e.questId));
            text(10, y + 2, 0.42f, kInk, 0, line);
            continue;
        }
        const bool done = quest::isComplete(*def, e.progress);
        quest::headline(*def, done, scratch_, sizeof(scratch_));
        text(10, y + 1, 0.4f, done ? C2D_Color32(0x20, 0x80, 0x20, 0xFF) : kInk, 0, scratch_);
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
        text(10, y + 13, 0.38f, kInk, kW - 90, scratch_);
        if (!done && quest::progressString(*def, e.progress, line, sizeof(line))) {
            text(kW - 78, y + 13, 0.38f, kInk, 0, line);
        }
    }
}

} // namespace tt::ui
