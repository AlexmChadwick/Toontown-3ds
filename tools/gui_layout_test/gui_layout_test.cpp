// Host checks for the bottom-screen layout constants (tools/gui_layout_test/run.sh).
#include "ui/book_tabs_atlas.h"
#include "ui/pad_tables.h"
#include "ui/panel_geometry.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int gFails = 0;

void expect(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    gFails++;
}

float fittedEnd(float titleX, float widthAtHalf, float right) {
    const float avail = right - titleX;
    float scale = 0.5f;
    if (widthAtHalf > avail && avail > 1.0f) scale *= avail / widthAtHalf;
    return titleX + widthAtHalf * (scale / 0.5f);
}

} // namespace

int main() {
    using namespace tt::ui;
    expect(layout::friendsScroll(8, 0) == 0, "scroll starts at the top");
    expect(layout::friendsScroll(8, 3) == 0, "focus stays on screen");
    expect(layout::friendsScroll(8, 4) == 1, "focus 4 scrolls one row");
    expect(layout::friendsScroll(8, 7) == 4, "last friend is the last visible row");
    expect(layout::friendsScroll(3, 2) == 0, "a short list does not scroll");
    expect(layout::friendsScroll(0, 5) == 0, "an empty list does not scroll");
    const int last = layout::friendsScroll(8, 7);
    expect(last + layout::kFriendsVisible - 1 == 7, "window includes the focused friend");
    expect(layout::kFriendsListY + layout::kFriendsListH <= layout::kFriendsCloseY - 8.0f,
           "friend rows end above the close button");
    expect(layout::kFriendsListX > layout::kFriendsPanelX &&
               layout::kFriendsListX + layout::kFriendsListW <
                   layout::kFriendsPanelX + layout::kFriendsPanelW,
           "friend rows stay inside the panel");

    const float titleX = 6.0f + 64.0f + 8.0f;
    const float friendsRight = layout::chatHeaderLimit(layout::kFriendsButtonX, false, 0.0f);
    expect(fittedEnd(titleX, 219.5f, friendsRight) <= friendsRight + 0.01f,
           "depth-2 crumb stops before Friends");
    const float whisperRight = layout::chatHeaderLimit(layout::kFriendsButtonX, true, 142.0f);
    expect(fittedEnd(titleX, 219.5f, whisperRight) <= whisperRight + 0.01f,
           "crumb stops before the whisper button");
    expect(whisperRight < friendsRight, "whisper button is left of Friends");

    const float corner = layout::hintCorner(pad::battle::kHintW, pad::battle::kHintH);
    const float textBottom = pad::battle::kHintTextTop + pad::battle::kHintTextStep + 12.0f;
    expect(pad::battle::kHintTextTop >= corner, "hint text starts below the corner");
    expect(textBottom < pad::battle::kHintH - corner, "hint text ends in the cream");
    expect(pad::battle::kHintTextMaxW <= pad::battle::kHintW - 2.0f * corner,
           "hint text width is the cream");
    expect(pad::battle::kHintY + pad::battle::kHintH <= pad::battle::kGridY,
           "hint card stays above the gag grid");
    expect(corner < 15.0f, "short card does not use the full 15 px corner");

    expect(layout::kLaffSlashBold < layout::kLaffLabelGap * layout::kLaffMeterRef,
           "pocket slash ink stays inside the gap");
    expect(layout::kLaffSlashBold < layout::kLaffLabelGap * 88.0f, "HUD slash ink stays inside the gap");
    expect(layout::kChatLogGap >= 3.0f && layout::kChatLogGap <= 8.0f, "chat log gap is a few pixels");

    expect(kBookTabIcons == 9, "nine book tab icons");
    for (int i = 0; i < kBookTabIcons; i++) {
        const BookTabRect& r = kBookTabRects[i];
        expect(r.x >= 0.0f && r.y >= 0.0f && r.x + r.w <= kBookTabAtlasW && r.y + r.h <= kBookTabAtlasH,
               "book tab rect inside the atlas");
        expect(r.w >= 20.0f && r.h >= 20.0f, "book tab icon is large enough to draw");
        for (int j = 0; j < i; j++) {
            const BookTabRect& o = kBookTabRects[j];
            const bool apart = r.x + r.w <= o.x || o.x + o.w <= r.x || r.y + r.h <= o.y || o.y + o.h <= r.y;
            expect(apart, "book tab rects do not overlap");
        }
    }
    const BookTabRect& map = kBookTabRects[kBookTabIcons - 1];
    expect(map.x >= 144.0f && map.y >= 48.0f, "map icon is the ninth cell");

    if (gFails) {
        std::fprintf(stderr, "%d layout checks failed\n", gFails);
        return 1;
    }
    std::printf("gui_layout_test: ok\n");
    return 0;
}
