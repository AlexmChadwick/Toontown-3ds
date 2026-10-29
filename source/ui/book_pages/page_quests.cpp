#include "ui/book_pages/page_quests.h"

#include <cstdio>

#include "net/quest_net.h"
#include "quest/quest_data.h"
#include "quest/quest_text.h"
#include "ui/theme.h"

namespace tt::ui {

namespace {

// QuestPoster colours: light green once complete, otherwise the poster's cream.
constexpr u32 kDoneFace = theme::rgb(0xD8, 0xF4, 0xC8);
constexpr u32 kDoneEdge = theme::rgb(0x3C, 0x8C, 0x2C);
constexpr u32 kDoneInk = theme::rgb(0x00, 0x4C, 0x00); // textColor (0, 0.3, 0)
constexpr u32 kRewardRed = theme::rgb(0xB0, 0x20, 0x20);

// "Building\nStreet\nHood" as one line ("Building, Street, Hood") into out.
void flatten(const char* in, char* out, size_t cap) {
    size_t n = 0;
    for (const char* c = in; *c && n + 3 < cap; ++c) {
        if (*c != '\n') {
            out[n++] = *c;
        } else if (n > 0 && c[1]) {
            out[n++] = ',';
            out[n++] = ' ';
        }
    }
    out[n] = 0;
}

// QuestPoster's toNpcName (ToonHQ and the tailor have generic names).
const char* returnToName(uint32_t npc) {
    if (npc == quest::kToonHQ) return "HQ Officer";
    if (npc == quest::kToonTailor) return "Tailor";
    const char* n = quest::npcName(static_cast<uint16_t>(npc));
    return n ? n : "a Toon";
}

} // namespace

// QuestPoster cards for the ToonTasks we carry, stacked in (x, y, w, h); shared by the book's
// ToonTasks page and the Toon tab.
int drawQuestPosters(PageText& text, const net::OwnQuests& q, float x, float y, float w, float h, int maxShown) {
    const int shown = q.count < maxShown ? q.count : maxShown;
    if (shown <= 0) return 0;
    const float gap = 4.0f;
    float cardH = (h - (shown - 1) * gap) / shown;
    if (cardH > 82.0f) cardH = 82.0f;
    char line[quest::kMaxText];
    for (int i = 0; i < shown; i++) {
        const net::QuestEntry& e = q.quests[i];
        const float cy = y + i * (cardH + gap), cx = x + 6, cw = w - 12;
        const quest::QuestDef* def = quest::findQuest(static_cast<uint16_t>(e.questId));
        const bool done = def && quest::isComplete(*def, e.progress);
        theme::roundRect(cx, cy, cw, cardH, 6, done ? kDoneEdge : theme::kPaperEdge);
        theme::roundRect(cx + 2, cy + 2, cw - 4, cardH - 4, 5, done ? kDoneFace : theme::kPaper);
        if (!def) {
            text.draw(cx + 8, cy + 5, theme::kTextBody, theme::kInk, "ToonTask %u", unsigned(e.questId));
            continue;
        }
        float ly = cy + 4;
        const float bottom = cy + cardH - 3;
        quest::headline(*def, done, line, sizeof line);
        text.fit(cx + 8, ly, theme::kTextBody, done ? kDoneInk : theme::kRedDark, cw - 96, "%s", line);
        if (!done && quest::progressString(*def, e.progress, line, sizeof line) > 0)
            text.right(cx + cw - 8, ly + 1, theme::kTextSmall, theme::kBlueDark, 90, "%s", line);
        ly += 15;
        // The objective, then "Return to: <NPC>" once complete (QuestPosterAuxReturnTo).
        const int room = int((bottom - ly) / 12.5f);
        quest::objective(*def, line, sizeof line);
        const int objLines = room >= 4 ? 2 : 1;
        ly += 12.5f * text.wrap(cx + 8, ly, theme::kTextSmall, theme::kInk, cw - 16, 12.5f, objLines, line);
        if (done && ly + 12 <= bottom) {
            text.fit(cx + 8, ly, theme::kTextSmall, kDoneInk, cw - 16, "Return to: %s", returnToName(e.toNpc));
            ly += 12.5f;
        }
        if (ly + 12 <= bottom && quest::posterInfo(*def, static_cast<uint16_t>(e.toNpc), done, line, sizeof line) > 0) {
            char flat[160];
            flatten(line, flat, sizeof flat);
            text.fit(cx + 8, ly, theme::kTextTiny, theme::kInkSoft, cw - 16, "%s", flat);
            ly += 12.5f;
        }
        if (ly + 12 <= bottom && quest::rewardText(static_cast<uint16_t>(e.reward), true, line, sizeof line) > 0 &&
            line[0]) {
            text.fit(cx + 8, ly, theme::kTextTiny, kRewardRed, cw - 16, "Reward: %s", line);
        }
    }
    return shown;
}

void PageQuests::init() { text_.init(2048); }

void PageQuests::shutdown() { text_.shutdown(); }

// QuestPage: one poster per carried ToonTask (headline, objective, where to go, progress and
// the reward), as many as the carry limit allows.
void PageQuests::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    const int count = bdc.quests ? bdc.quests->count : 0;
    const int limit = bdc.quests ? bdc.quests->carryLimit : 1;
    text_.draw(x + 8, y + 3, theme::kTextBody, theme::kInk, "ToonTasks  %d / %d", count, limit);
    if (bdc.quests) {
        text_.right(x + w - 8, y + 4, theme::kTextSmall, theme::kInkSoft, 0, "Tasks done: %u",
                    unsigned(bdc.quests->historyCount));
    }
    if (count == 0) {
        theme::paper(x + 20, y + 40, w - 40, 84, 8.0f);
        text_.centered(x + w * 0.5f, y + 54, theme::kTextTitle, theme::kRedDark, w - 60, "No ToonTasks yet!");
        text_.centered(x + w * 0.5f, y + 80, theme::kTextSmall, theme::kInk, w - 56,
                       "Visit an HQ Officer in any Toon HQ");
        text_.centered(x + w * 0.5f, y + 96, theme::kTextSmall, theme::kInk, w - 56, "to take on a ToonTask.");
        return;
    }
    const int shown = drawQuestPosters(text_, *bdc.quests, x, y + 22, w, h - 24, 4);
    if (count > shown) {
        text_.right(x + w - 8, y + h - 12, theme::kTextTiny, theme::kInkSoft, 0, "+%d more", count - shown);
    }
}


} // namespace tt::ui
