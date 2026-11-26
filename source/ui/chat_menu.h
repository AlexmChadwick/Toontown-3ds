// Bottom-screen SpeedChat tree browser (the Chat tab).
//
// Touch: tap a row (activates on release, if the stylus moved < kTapSlop px), drag the
// list to scroll, tap "< Back" in the header.
// Buttons (only while the Chat tab is open):
//   D-pad Up/Down  move the highlight (the first press shows it on the top visible row)
//   A              activate the highlighted row        -- consumed only while a row is highlighted
//   B              back one level; at the root, hide the highlight -- consumed likewise
// With no highlight, A and B reach the world untouched (jump / walk), so the Chat tab
// can stay open while playing. Saying a phrase returns to the root and hides the
// highlight (upstream closes SpeedChat after a selection, want-sc-auto-hide).
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "net/quest_net.h"
#include "quest/quest_text.h"
#include "ui/speedchat_data.h"

namespace tt::ui {

class ChatMenu {
public:
    static constexpr float kW = 320.0f;
    static constexpr float kHeaderH = 28.0f;
    static constexpr float kRowH = 30.0f; // >= 28 px finger target
    static constexpr float kTapSlop = 8.0f;
    static constexpr int kQueueLen = 4;
    // TOONTASKS > MY TASKS (TTSCToontaskMenu): phrases built from our current ToonTasks.
    static constexpr int kMyTasksMenu = sc::kNumMenus; // a virtual menu after the generated ones
    static constexpr int kMaxTaskPhrases = 16;
    static constexpr int kTaskCode = 0x10000; // popSpeech values >= this are task phrases
    struct TaskPhrase {
        char text[quest::kScLen];
        uint32_t taskId, toNpc, progress;
        uint8_t msgIndex;
        bool staticNeedTask; // "I need to get a ToonTask." (setSC 1299), not a setSCToontask
    };

    bool init();
    void shutdown();
    // panelBottom: y where the tab bar starts. Returns the keys it consumed (subset of down).
    u32 handleInput(u32 down, u32 held, const touchPosition& t, float panelBottom);
    // Call when the Chat tab is left or entered: drops the highlight and any drag.
    void blur();
    void draw(float panelBottom);
    // Oldest queued phrase the player chose (SpeedChat msgIndex for setSC; negative = an emote;
    // >= kTaskCode = a ToonTask phrase, read it with taskPhrase()).
    bool popSpeech(int& msgIndex);
    const TaskPhrase* taskPhrase(int code) const;
    // Rebuild MY TASKS when our quests change (OwnQuests::version); null = offline.
    void setTasks(const net::OwnQuests* quests);
    void setEmoteAccess(const uint8_t* access) {
        for (int i=0;i<25;i++) emoteAccess_[i]=!access || access[i]!=0;
    }
    int menu() const { return menu_; }

private:
    // One row of a menu: generated items, plus MY TASKS on top of TOONTASKS and its phrases.
    struct Row {
        const char* label;
        int value;   // terminal: setSC msgIndex (65535 = emote only); menu: menu index
        int emote;   // -1 = none
        bool isMenu;
        int task;    // index into tasks_, -1 = a generated phrase
    };
    int numRows(int menu) const;
    Row row(int menu, int i) const;
    int parentOf(int menu) const;
    const char* titleOf(int menu) const;
    void enter(int menu, int focusItem);
    void activate(int item);
    void goBack(float listH);
    void ensureVisible(int item, float listH);
    float maxScroll(float listH) const;
    int rowAt(float y, float listH) const;

    C2D_TextBuf menuBuf_ = nullptr;   // labels of the current menu, reparsed on enter()
    C2D_TextBuf staticBuf_ = nullptr; // fixed strings
    static constexpr int kMaxRows = sc::kMaxMenuItems > kMaxTaskPhrases ? sc::kMaxMenuItems : kMaxTaskPhrases;
    C2D_Text labels_[kMaxRows];
    float labelScale_[kMaxRows];
    float labelH_[kMaxRows];
    bool labelWrap_[kMaxRows];
    C2D_Text title_, back_, arrow_, hint_;
    float titleScale_ = 0.5f;

    int menu_ = 0;
    int focus_ = -1; // highlighted item, -1 = none (D-pad mode off)
    float scroll_ = 0.0f;
    // Touch tracking
    bool touching_ = false, dragged_ = false;
    float touchStartY_ = 0, touchStartX_ = 0, scrollStart_ = 0;
    int pressedRow_ = -1;
    bool pressedBack_ = false;

    bool emoteAccess_[25] = {};
    int toontasksMenu_ = -1; // the generated TOONTASKS menu
    TaskPhrase tasks_[kMaxTaskPhrases];
    int numTasks_ = 0;
    uint32_t tasksVersion_ = 0;
    bool tasksKnown_ = false;
    TaskPhrase queuedTasks_[kQueueLen]; // copies, so a quest update cannot change a queued phrase
    int queue_[kQueueLen];
    int qHead_ = 0, qCount_ = 0;
};

} // namespace tt::ui
