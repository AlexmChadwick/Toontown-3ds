#include "ui/book_pages/page_quests.h"
#include "net/quest_net.h"
#include "quest/quest_text.h"
#include "quest/quest_data.h"
#include <cstdio>

namespace tt::ui {

namespace {

constexpr u32 kInk = C2D_Color32(0x20, 0x18, 0x10, 0xFF);
constexpr u32 kCardBg = C2D_Color32(0xFF, 0xFA, 0xEA, 0xFF);
constexpr u32 kCardBorder = C2D_Color32(0xB0, 0x90, 0x60, 0xFF);
constexpr u32 kCompleteBg = C2D_Color32(0xEA, 0xFF, 0xEA, 0xFF);
constexpr u32 kCompleteBorder = C2D_Color32(0x30, 0xA0, 0x30, 0xFF);
constexpr u32 kCompleteText = C2D_Color32(0x18, 0x80, 0x18, 0xFF);
constexpr u32 kRewardText = C2D_Color32(0x00, 0x40, 0x90, 0xFF);

} // namespace

void PageQuests::init() {
    textBuf_ = C2D_TextBufNew(4096);
}

void PageQuests::shutdown() {
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
}

void PageQuests::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)h;
    if (!textBuf_) return;

    C2D_TextBufClear(textBuf_);

    const int count = bdc.quests ? bdc.quests->count : 0;
    const int carryLimit = bdc.quests ? bdc.quests->carryLimit : 1;

    // Header line
    char line[256];
    snprintf(line, sizeof(line), "ToonTasks  %d / %d", count, carryLimit);
    C2D_Text hdrText;
    C2D_TextParse(&hdrText, textBuf_, line);
    C2D_TextOptimize(&hdrText);
    C2D_DrawText(&hdrText, C2D_WithColor, x + 6.0f, y + 2.0f, 0.5f, 0.45f, 0.45f, kInk);

    if (count == 0) {
        C2D_Text emptyText;
        C2D_TextParse(&emptyText, textBuf_, "You have no ToonTasks!\n\nVisit a Toon HQ Officer on any street\nor playground to take on a task.");
        C2D_TextOptimize(&emptyText);
        C2D_DrawText(&emptyText, C2D_WithColor, x + 16.0f, y + 40.0f, 0.5f, 0.38f, 0.38f,
                     C2D_Color32(0x60, 0x58, 0x50, 0xFF));
        return;
    }

    // Stack up to 4 task cards
    const float cardAreaY = y + 18.0f;
    const float cardAreaH = 155.0f;
    const float cardH = (cardAreaH - (count - 1) * 4.0f) / float(count < 4 ? (count < 1 ? 1 : count) : 4);
    const float cardW = w - 12.0f;

    char scratch[quest::kMaxText];

    for (int i = 0; i < count && i < 4; i++) {
        const net::QuestEntry& qe = bdc.quests->quests[i];
        const float cy = cardAreaY + i * (cardH + 4.0f);

        const quest::QuestDef* def = quest::findQuest(static_cast<uint16_t>(qe.questId));
        const bool complete = def ? quest::isComplete(*def, qe.progress) : false;

        // Card box
        C2D_DrawRectSolid(x + 6.0f, cy, 0.5f, cardW, cardH, complete ? kCompleteBorder : kCardBorder);
        C2D_DrawRectSolid(x + 7.0f, cy + 1.0f, 0.5f, cardW - 2.0f, cardH - 2.0f, complete ? kCompleteBg : kCardBg);

        if (!def) {
            snprintf(line, sizeof(line), "ToonTask %u", unsigned(qe.questId));
            C2D_Text t;
            C2D_TextParse(&t, textBuf_, line);
            C2D_TextOptimize(&t);
            C2D_DrawText(&t, C2D_WithColor, x + 12.0f, cy + 4.0f, 0.5f, 0.38f, 0.38f, kInk);
            continue;
        }

        // Headline
        quest::headline(*def, complete, scratch, sizeof(scratch));
        C2D_Text headTxt;
        C2D_TextParse(&headTxt, textBuf_, scratch);
        C2D_TextOptimize(&headTxt);
        C2D_DrawText(&headTxt, C2D_WithColor, x + 12.0f, cy + 3.0f, 0.5f, 0.36f, 0.36f,
                     complete ? kCompleteText : kInk);

        // Objective / Progress
        if (complete) {
            quest::posterInfo(*def, static_cast<uint16_t>(qe.toNpc), true, scratch, sizeof(scratch));
        } else {
            quest::objective(*def, scratch, sizeof(scratch));
        }
        C2D_Text objTxt;
        C2D_TextParse(&objTxt, textBuf_, scratch);
        C2D_TextOptimize(&objTxt);
        C2D_DrawText(&objTxt, C2D_WithColor, x + 12.0f, cy + 14.0f, 0.5f, 0.32f, 0.32f, kInk);

        // Progress counter (if not complete)
        if (!complete && quest::progressString(*def, qe.progress, scratch, sizeof(scratch))) {
            C2D_Text progTxt;
            C2D_TextParse(&progTxt, textBuf_, scratch);
            C2D_TextOptimize(&progTxt);
            C2D_DrawText(&progTxt, C2D_WithColor, x + cardW - 80.0f, cy + 3.0f, 0.5f, 0.32f, 0.32f,
                         C2D_Color32(0x50, 0x40, 0x20, 0xFF));
        }

        // Reward (if card is high enough)
        if (cardH >= 40.0f) {
            scratch[0] = '\0';
            if (quest::rewardText(static_cast<uint16_t>(qe.reward), true, scratch, sizeof(scratch)) && scratch[0]) {
                snprintf(line, sizeof(line), "Reward: %s", scratch);
                C2D_Text rwdTxt;
                C2D_TextParse(&rwdTxt, textBuf_, line);
                C2D_TextOptimize(&rwdTxt);
                C2D_DrawText(&rwdTxt, C2D_WithColor, x + 12.0f, cy + cardH - 12.0f, 0.5f, 0.30f, 0.30f, kRewardText);
            }
        }
    }
}

} // namespace tt::ui
