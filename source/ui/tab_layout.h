// Bottom-screen tab bar layout and the Toontown GUI palette shared by the tab panels.
// Host-safe (no citro2d): core/autotest resolves tab taps through the same layout.
#pragma once

#include <cstdint>

namespace tt::ui {

enum Tab : int { kTabMap = 0, kTabChat, kTabToon, kTabGags, kTabSettings, kTabDebug, kTabCount };

// The bar shows Map Chat Toon Gags, then Setup and Debug only while the debug HUD is on
// (SELECT, or Options > Debug HUD), then the Shtiker Book button in the bottom-right corner,
// where upstream's book button sits. kTabBook is the book button's id; it is never a tab.
constexpr int kTabBook = kTabCount;
constexpr float kTabBarY = 208.0f, kTabBarH = 32.0f;
constexpr int tabSlots(bool debug) { return debug ? 7 : 5; }
constexpr float tabSlotW(bool debug) { return 320.0f / tabSlots(debug); }
constexpr int tabSlot(int tab, bool debug) { return tab == kTabBook ? tabSlots(debug) - 1 : tab; }
constexpr float tabCenterX(int tab, bool debug) { return (tabSlot(tab, debug) + 0.5f) * tabSlotW(debug); }
constexpr int tabAt(float x, bool debug) {
    const int s = x < 0 ? 0 : int(x / tabSlotW(debug));
    return s >= tabSlots(debug) - 1 ? kTabBook : s;
}
// D-Left / D-Right cycle through tabs [0, tabCycleCount).
constexpr int tabCycleCount(bool debug) { return debug ? kTabCount : kTabSettings; }
constexpr bool tabVisible(int tab, bool debug) { return tab >= 0 && tab < tabCycleCount(debug); }

// Toontown GUI palette (RGBA as citro2d's C2D_Color32 packs it: 0xAABBGGRR).
namespace theme {
constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | 0xFF000000u;
}
// Shtiker Book paper and cover (ui/book.cpp uses the same values).
constexpr uint32_t kPaper = rgb(0xF6, 0xEE, 0xD8);
constexpr uint32_t kPaperEdge = rgb(0xC8, 0xB4, 0x8C);
constexpr uint32_t kCard = rgb(0xFF, 0xFA, 0xEC);
constexpr uint32_t kInk = rgb(0x2A, 0x1E, 0x12);
constexpr uint32_t kInkSoft = rgb(0x6E, 0x5C, 0x46);
constexpr uint32_t kInkFaint = rgb(0xA8, 0x9A, 0x84);
constexpr uint32_t kCover = rgb(0x6E, 0x12, 0x12);
constexpr uint32_t kCoverDark = rgb(0x50, 0x0C, 0x0C);
constexpr uint32_t kTabOff = rgb(0x82, 0x1C, 0x1C);
constexpr uint32_t kGold = rgb(0xF5, 0xB8, 0x1A);
constexpr uint32_t kGoldText = rgb(0xFF, 0xEE, 0xAA);
// Toontown's yellow GUI buttons.
constexpr uint32_t kButton = rgb(0xFF, 0xE3, 0x8A);
constexpr uint32_t kButtonEdge = rgb(0xB8, 0x86, 0x2B);
constexpr uint32_t kButtonOff = rgb(0xE4, 0xDC, 0xC8);
constexpr uint32_t kBean = rgb(0x80, 0x50, 0x00);
// Gag track colours (Toon-Up .. Drop), as the Shtiker Book's inventory page.
constexpr uint32_t kTrack[7] = {rgb(0xD3, 0x94, 0xFF), rgb(0xE0, 0xE0, 0x20), rgb(0x20, 0xC0, 0x20),
                                rgb(0x40, 0x80, 0xFF), rgb(0xFF, 0xA0, 0x20), rgb(0xFF, 0x40, 0xC0),
                                rgb(0x20, 0xD0, 0xD0)};
// otp/speedchat/SCColorScheme defaults: arrow (0.5, 0.5, 1), rollover (0.53, 0.9, 0.53), the
// frame derived from the arrow colour (h, 0.1 s, v) -> near-white blue, black text.
constexpr uint32_t kScFrame = rgb(0xF2, 0xF2, 0xFF);
constexpr uint32_t kScFrameEdge = rgb(0xA6, 0xA6, 0xD8);
constexpr uint32_t kScArrow = rgb(0x80, 0x80, 0xFF);
constexpr uint32_t kScRollover = rgb(0x87, 0xE6, 0x87);
constexpr uint32_t kScPressed = rgb(0x7C, 0xD4, 0x7C);
constexpr uint32_t kScText = rgb(0x00, 0x00, 0x00);
constexpr uint32_t kScDisabled = rgb(0x66, 0x66, 0x66);
} // namespace theme

} // namespace tt::ui
