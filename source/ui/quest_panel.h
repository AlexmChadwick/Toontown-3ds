// Bottom-screen ToonTask UI (docs/design/quests.md): the NPC conversation (dialog pages with
// Next, the quest choice cards, the track choice) while tt::quest::Talk is locked, and the
// player's task list (the Shticker Book's ToonTask page) on the Toon tab.
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "net/quest_net.h"
#include "quest/quest_talk.h"
#include "ui/focus.h"
#include "ui/toontown_timer.h"

namespace tt::ui {

class QuestPanel {
public:
    bool init();
    void shutdown();
    // Conversation; returns the buttons consumed (all while talking).
    u32 handleInput(u32 keysDown, const touchPosition& touch, quest::Talk& talk);
    void drawTalk(const quest::Talk& talk);

private:
    void text(float x, float y, float scale, u32 color, float wrapWidth, const char* s);
    // How many lines `s` takes word-wrapped at wrapWidth (C2D_WordWrap's greedy rule).
    int wrappedLines(const char* s, float scale, float wrapWidth);

    C2D_TextBuf buf_ = nullptr;
    ToontownTimer timer_; // QuestChoiceGui's countdown
    char scratch_[quest::kMaxText] = {};
    // The balloon fits the page: its line count, measured once per page.
    const char* measuredText_ = nullptr;
    int measuredPage_ = -1, measuredLines_ = 1;
    FocusGroup focus_{"quest"};
};

} // namespace tt::ui
