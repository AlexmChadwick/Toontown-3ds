#pragma once
#include <algorithm>
#include <cmath>

namespace tt::ui {

// Content ends before Home/hood controls, irrespective of the baked map's aspect.
struct MapRect {
    float x, y, width, height;
    bool marker(float* px, float* py, float radius) const {
        if (!std::isfinite(*px) || !std::isfinite(*py) || *px < x || *py < y ||
            *px > x + width || *py > y + height || width < radius * 2 || height < radius * 2)
            return false;
        *px = std::clamp(*px, x + radius, x + width - radius);
        *py = std::clamp(*py, y + radius, y + height - radius);
        return true;
    }
};

inline float hpFraction(int hp, int maximum) {
    return maximum > 0 ? std::clamp(float(hp) / maximum, 0.0f, 1.0f) : 0.0f;
}

// Bottom-screen rectangles shared by the panels and tools/gui_layout_test.
// FriendsBox_Open is stretched to the panel rect. The list window is the cream
// paper inside that parchment (measured on the baked atlas), not the carved frame.
namespace layout {

constexpr float kFriendsButtonX = 244.0f, kFriendsButtonY = 2.0f, kFriendsButtonW = 72.0f,
                kFriendsButtonH = 24.0f;
constexpr float kFriendsPanelX = 70.0f, kFriendsPanelY = 1.0f, kFriendsPanelW = 180.0f,
                kFriendsPanelH = 204.0f;
constexpr float kFriendsListX = 102.0f, kFriendsListY = 86.0f, kFriendsListW = 122.0f;
constexpr float kFriendsRowH = 16.0f;
constexpr int kFriendsVisible = 4;
constexpr float kFriendsListH = kFriendsRowH * kFriendsVisible;
constexpr float kFriendsTitleX = 164.0f, kFriendsTitleY = 56.0f, kFriendsTitleMaxW = 96.0f,
                kFriendsTitleScale = 0.42f;
constexpr float kFriendsCloseX = 146.0f, kFriendsCloseY = 174.0f, kFriendsCloseW = 28.0f,
                kFriendsCloseH = 26.0f;
// Visible sky between the SpeedChat frame bottom and the chat-log paper.
// The paper is drawn at the log strip's top (kTabY - kChatLogH), not one pixel below it.
constexpr float kTabY = 208.0f;
constexpr float kChatHeaderH = 28.0f;
constexpr float kChatLogH = 34.0f;
constexpr float kPocketTop = 176.0f;
constexpr float kChatLogGap = 4.0f;
constexpr float kChatMarginX = 6.0f;
constexpr float kChatBackW = 64.0f;
constexpr float kChatTitleBase = 0.5f;
constexpr float kChatTitleMin = 0.32f;
constexpr float kWhisperButtonX = 142.0f;
// Pocket laff. The extra slash fill scales with the meter. It stays at least one
// pixel short of the next glyph so it does not cross that glyph's outline.
constexpr float kLaffLabelMaxW = 0.46f;
constexpr float kLaffLabelGap = 0.040f;
constexpr float kLaffSlashBold = 1.15f;
constexpr float kLaffMeterRef = 72.0f;
constexpr float kLaffPulseChange = 0.333f; // ToontownIntervals.PULSE_GUI_CHANGE
// Battle hint card. The 9-slice corner is capped so two lines sit in the cream.
constexpr float kHintH = 50.0f;
constexpr float kHintTextTop = 13.0f, kHintTextStep = 12.0f, kHintTextScale = 0.36f;

constexpr float hintCorner(float w, float h) {
    float c = 15.0f;
    const float limit = (w < h ? w : h) * 0.22f;
    if (c > limit) c = limit;
    if (c < 4.0f) c = 4.0f;
    return c;
}

constexpr int friendsScroll(int count, int focus, int visible = kFriendsVisible) {
    if (count <= visible || visible <= 0) return 0;
    if (focus < 0) focus = 0;
    if (focus >= count) focus = count - 1;
    int scroll = focus - visible + 1;
    if (scroll < 0) scroll = 0;
    const int maxScroll = count - visible;
    if (scroll > maxScroll) scroll = maxScroll;
    return scroll;
}

constexpr int friendFocusClamp(int count, int focus) {
    if (count <= 0 || focus < 0) return 0;
    if (focus >= count) return count - 1;
    return focus;
}

constexpr int friendVisible(int count, int focus) {
    const int scroll = friendsScroll(count, friendFocusClamp(count, focus));
    int shown = count - scroll;
    if (shown > kFriendsVisible) shown = kFriendsVisible;
    if (shown < 0) shown = 0;
    return shown;
}

// Friend index under a touch, or -1. The hit box is only the rows on screen,
// so a row scrolled off the top and the paper below the last name both miss.
inline int friendAt(int count, int focus, float px, float py) {
    focus = friendFocusClamp(count, focus);
    const int scroll = friendsScroll(count, focus);
    const int visible = friendVisible(count, focus);
    if (px < kFriendsListX || px >= kFriendsListX + kFriendsListW) return -1;
    if (py < kFriendsListY || py >= kFriendsListY + visible * kFriendsRowH) return -1;
    const int row = static_cast<int>((py - kFriendsListY) / kFriendsRowH);
    if (row < 0 || row >= visible) return -1;
    return scroll + row;
}

// Right edge the SpeedChat title must stop short of. The Friends button is always
// on the Chat tab. The whisper target button is only there when someone is nearby.
constexpr float chatHeaderLimit(float friendsX, bool whisper, float whisperX) {
    float right = friendsX - 4.0f;
    if (whisper && whisperX - 4.0f < right) right = whisperX - 4.0f;
    return right;
}

constexpr float chatTitleX(bool nested) {
    return nested ? kChatMarginX + kChatBackW + 8.0f : kChatMarginX + 2.0f;
}

// A depth-2 crumb keeps its parent while it fits at kChatTitleMin or above.
// Below that, the header shows only the current title. If that still exceeds
// the room at the minimum scale, the draw clips it to [titleX, headerRight).
struct TitleFit {
    float scale;
    bool dropPrefix;
    bool scissor;
};

constexpr TitleFit fitChatTitle(float fullAt1, float leafAt1, float titleX, float right) {
    TitleFit fit{kChatTitleBase, false, false};
    const float room = right - titleX;
    if (!(room > 1.0f)) {
        fit.scale = kChatTitleMin;
        fit.dropPrefix = true;
        fit.scissor = true;
        return fit;
    }
    if (fullAt1 * kChatTitleBase <= room) return fit;
    const float fitted = room / fullAt1;
    if (fitted >= kChatTitleMin) {
        fit.scale = fitted;
        return fit;
    }
    fit.dropPrefix = true;
    const float leafFit = leafAt1 > 0.0f ? room / leafAt1 : kChatTitleMin;
    if (leafFit >= kChatTitleMin) {
        fit.scale = leafFit > kChatTitleBase ? kChatTitleBase : leafFit;
        return fit;
    }
    fit.scale = kChatTitleMin;
    fit.scissor = true;
    return fit;
}

constexpr bool chatShowsCrumb(int parent, int grandparent) {
    return parent >= 0 && grandparent >= 0;
}

constexpr float chatLogTop() { return kTabY - kChatLogH; }
constexpr float chatLogPaperY() { return chatLogTop(); }
constexpr float chatBottom(bool logShown) {
    return logShown ? chatLogPaperY() - kChatLogGap : kPocketTop;
}
constexpr bool chatListContains(float y, bool logShown) {
    return y >= kChatHeaderH && y < chatBottom(logShown);
}

constexpr float slashBold(float size) { return kLaffSlashBold * size / kLaffMeterRef; }
constexpr float slashGap(float size) { return kLaffLabelGap * size; }
constexpr float kLaffPulseSize = kLaffMeterRef * (1.0f - kLaffPulseChange);

static_assert(kFriendsListX >= 99.0f && kFriendsListX + kFriendsListW <= 227.0f, "list on the paper");
static_assert(kFriendsListY >= 84.0f && kFriendsListY + kFriendsListH <= 160.0f, "list on the paper");
static_assert(kFriendsListY + kFriendsListH + 8.0f <= kFriendsCloseY, "list above the close button");
static_assert(slashBold(kLaffPulseSize) < slashGap(kLaffPulseSize) - 1.0f,
              "shrunk slash stays clear of the next outline");
static_assert(slashBold(kLaffMeterRef) < slashGap(kLaffMeterRef) - 1.0f,
              "rest slash stays clear of the next outline");
static_assert(chatLogPaperY() - chatBottom(true) == kChatLogGap, "chat sky is the stated gap");
static_assert(kChatLogGap == 4.0f, "four pixels of sky above the chat log");

} // namespace layout

} // namespace tt::ui
