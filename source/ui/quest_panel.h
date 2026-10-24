// Bottom-screen ToonTask UI (docs/design/quests.md): the NPC conversation (dialog pages with
// Next, the quest choice cards, the track choice) while tt::quest::Talk is locked, and the
// player's task list (the Shticker Book's ToonTask page) on the Toon tab.
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "net/quest_net.h"
#include "quest/quest_talk.h"

namespace tt::ui {

class QuestPanel {
public:
    bool init();
    void shutdown();
    // Conversation; returns the buttons consumed (all while talking).
    u32 handleInput(u32 keysDown, const touchPosition& touch, quest::Talk& talk);
    void drawTalk(const quest::Talk& talk);
    // The task list in the area (0, y0) .. (320, y1).
    void drawTasks(const net::OwnQuests& q, float y0, float y1);

private:
    void text(float x, float y, float scale, u32 color, float wrapWidth, const char* s);

    C2D_TextBuf buf_ = nullptr;
    char scratch_[quest::kMaxText] = {};
};

} // namespace tt::ui
