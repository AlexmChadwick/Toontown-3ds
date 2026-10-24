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

#include "ui/speedchat_data.h"

namespace tt::ui {

class ChatMenu {
public:
    static constexpr float kW = 320.0f;
    static constexpr float kHeaderH = 28.0f;
    static constexpr float kRowH = 30.0f; // >= 28 px finger target
    static constexpr float kTapSlop = 8.0f;
    static constexpr int kQueueLen = 4;

    bool init();
    void shutdown();
    // panelBottom: y where the tab bar starts. Returns the keys it consumed (subset of down).
    u32 handleInput(u32 down, u32 held, const touchPosition& t, float panelBottom);
    // Call when the Chat tab is left or entered: drops the highlight and any drag.
    void blur();
    void draw(float panelBottom);
    // Oldest queued phrase the player chose (SpeedChat msgIndex for setSC).
    bool popSpeech(int& msgIndex);
    int menu() const { return menu_; }

private:
    void enter(int menu, int focusItem);
    void activate(int item);
    void goBack(float listH);
    void ensureVisible(int item, float listH);
    float maxScroll(float listH) const;
    int rowAt(float y, float listH) const;

    C2D_TextBuf menuBuf_ = nullptr;   // labels of the current menu, reparsed on enter()
    C2D_TextBuf staticBuf_ = nullptr; // fixed strings
    C2D_Text labels_[sc::kMaxMenuItems];
    float labelScale_[sc::kMaxMenuItems];
    float labelH_[sc::kMaxMenuItems];
    bool labelWrap_[sc::kMaxMenuItems];
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

    int queue_[kQueueLen];
    int qHead_ = 0, qCount_ = 0;
};

} // namespace tt::ui
