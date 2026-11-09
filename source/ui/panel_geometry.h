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
// Sky between the SpeedChat frame and the recent-chat strip.
constexpr float kChatLogGap = 4.0f;
// Pocket laff. Extra slash ink stays inside the gap at the 72 px meter.
constexpr float kLaffLabelMaxW = 0.46f;
constexpr float kLaffLabelGap = 0.020f;
constexpr float kLaffSlashBold = 1.15f;
constexpr float kLaffMeterRef = 72.0f;
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

// Right edge the SpeedChat title must stop short of. The Friends button is always
// on the Chat tab. The whisper target button is only there when someone is nearby.
constexpr float chatHeaderLimit(float friendsX, bool whisper, float whisperX) {
    float right = friendsX - 4.0f;
    if (whisper && whisperX - 4.0f < right) right = whisperX - 4.0f;
    return right;
}

static_assert(kFriendsListX >= 99.0f && kFriendsListX + kFriendsListW <= 227.0f, "list on the paper");
static_assert(kFriendsListY >= 84.0f && kFriendsListY + kFriendsListH <= 160.0f, "list on the paper");
static_assert(kFriendsListY + kFriendsListH + 8.0f <= kFriendsCloseY, "list above the close button");
static_assert(kLaffSlashBold < kLaffLabelGap * kLaffMeterRef, "slash ink inside the label gap");
static_assert(kChatLogGap >= 3.0f && kChatLogGap <= 8.0f, "a few pixels above the chat log");

} // namespace layout

} // namespace tt::ui
