// Host checks for the bottom-screen layout (tools/gui_layout_test/run.sh).
#include "ui/book_tabs_atlas.h"
#include "ui/pad_tables.h"
#include "ui/panel_geometry.h"
#include "ui/speedchat_data.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>

namespace {

int gFails = 0;
int gChecks = 0;

void expect(bool ok, const char* what) {
    gChecks++;
    if (ok) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    gFails++;
}

struct Font {
    std::unordered_map<int, int> advance;
};

int readU16(const std::string& s, size_t o) { return int(uint8_t(s[o])) | int(uint8_t(s[o + 1])) << 8; }
int readU32(const std::string& s, size_t o) {
    return readU16(s, o) | readU16(s, o + 2) << 16;
}

Font loadImpress(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (s.size() < 64 || s.compare(0, 4, "CFNT") != 0) {
        std::fprintf(stderr, "FAIL impress.bcfnt missing at %s\n", path);
        std::exit(1);
    }
    const int finf = readU16(s, 6);
    const int cwdhPtr = readU32(s, finf + 20);
    const int cmapPtr = readU32(s, finf + 24);
    std::unordered_map<int, int> glyphOf;
    for (int p = cmapPtr; p;) {
        const int b = p - 8;
        const int begin = readU16(s, b + 8);
        const int end = readU16(s, b + 10);
        const int method = readU16(s, b + 12);
        const int next = readU32(s, b + 16);
        if (method == 0) {
            const int first = readU16(s, b + 20);
            for (int c = begin; c <= end; c++) glyphOf[c] = first + c - begin;
        } else if (method == 1) {
            for (int k = 0, c = begin; c <= end; k++, c++) {
                const int g = readU16(s, b + 20 + 2 * k);
                if (g != 0xFFFF) glyphOf[c] = g;
            }
        } else {
            const int count = readU16(s, b + 20);
            for (int k = 0; k < count; k++) {
                const int c = readU16(s, b + 22 + 4 * k);
                glyphOf[c] = readU16(s, b + 24 + 4 * k);
            }
        }
        p = next;
    }
    std::unordered_map<int, int> advanceOf;
    for (int p = cwdhPtr; p;) {
        const int b = p - 8;
        const int start = readU16(s, b + 8);
        const int end = readU16(s, b + 10);
        const int next = readU32(s, b + 12);
        for (int i = 0, g = start; g <= end; i++, g++)
            advanceOf[g] = int(int8_t(s[b + 16 + 3 * i + 2]));
        p = next;
    }
    Font font;
    for (const auto& it : glyphOf) {
        const auto w = advanceOf.find(it.second);
        if (w == advanceOf.end()) {
            std::fprintf(stderr, "FAIL glyph %d has no advance\n", it.second);
            std::exit(1);
        }
        font.advance[it.first] = w->second;
    }
    return font;
}

int advanceOf(const Font& font, const char* text) {
    int sum = 0;
    for (const char* p = text; *p; p++) {
        const auto it = font.advance.find(uint8_t(*p));
        if (it == font.advance.end()) {
            std::fprintf(stderr, "FAIL font has no U+%04X\n", uint8_t(*p));
            std::exit(1);
        }
        sum += it->second;
    }
    return sum;
}

} // namespace

int main(int argc, char** argv) {
    using namespace tt::ui;
    const char* fontPath = argc > 1 ? argv[1] : "romfs/baked/fonts/impress.bcfnt";
    const Font font = loadImpress(fontPath);

    expect(layout::friendsScroll(1, 0) == 0, "one friend does not scroll");
    expect(layout::friendVisible(1, 0) == 1, "one friend is visible");
    expect(layout::friendsScroll(4, 0) == 0, "four friends stay put at the top");
    expect(layout::friendsScroll(4, 3) == 0, "the fourth friend does not scroll");
    expect(layout::friendVisible(4, 3) == 4, "four friends are all visible");
    expect(layout::friendsScroll(5, 0) == 0, "five friends start at the top");
    expect(layout::friendsScroll(5, 3) == 0, "focus 3 of five is still on the first page");
    expect(layout::friendsScroll(5, 4) == 1, "the fifth friend scrolls one row");
    expect(layout::friendVisible(5, 4) == 4, "a scrolled page shows four rows");
    expect(layout::friendsScroll(8, 0) == 0, "scroll starts at the top");
    expect(layout::friendsScroll(8, 7) == 4, "last friend is the last visible row");
    expect(layout::friendsScroll(0, 5) == 0, "an empty list does not scroll");

    const float x = layout::kFriendsListX + 4.0f;
    const float y0 = layout::kFriendsListY;
    expect(layout::friendAt(5, 0, x, y0 + 1.0f) == 0, "unscrolled, friend 0 sits on the first row");
    const int scrolledOff = layout::friendAt(5, 4, x, y0 + 1.0f);
    expect(scrolledOff == 1, "that same in-list row is friend 1 after friend 0 scrolls off");
    expect(layout::friendAt(1, 0, x, y0 + 1.0f) == 0, "the only friend is hittable");
    expect(layout::friendAt(1, 0, x, y0 + layout::kFriendsRowH) == -1,
           "a touch below the last row misses");
    expect(layout::friendAt(1, 0, x, y0 + layout::kFriendsListH - 1.0f) == -1,
           "empty paper under a short list misses");
    const int shrunk = layout::friendFocusClamp(2, 7);
    expect(shrunk == 1, "focus clamps after the list shrinks");
    expect(layout::friendsScroll(2, shrunk) == 0, "a clamped short list does not scroll");
    expect(layout::friendAt(2, shrunk, x, y0 + layout::kFriendsRowH + 1.0f) == 1,
           "the clamped focus still hits the last remaining friend");

    const int menu = 17;
    const int parent = tt::sc::kMenus[menu].parent;
    const int grand = tt::sc::kMenus[parent].parent;
    expect(layout::chatShowsCrumb(parent, grand), "menu 17 is a depth-2 crumb");
    char crumb[96];
    std::snprintf(crumb, sizeof(crumb), "%s > %s", tt::sc::kMenus[parent].title, tt::sc::kMenus[menu].title);
    const int full = advanceOf(font, crumb);
    const int speed = advanceOf(font, "SpeedChat");
    expect(std::strcmp(crumb, "TOONTASKS > I think you should choose...") == 0, "menu 17 crumb text");
    expect(full == 450, "menu 17 crumb advance from impress.bcfnt");
    expect(speed == 110, "SpeedChat advance from impress.bcfnt");

    const float titleX = layout::chatTitleX(true);
    const float whisperRight = layout::chatHeaderLimit(layout::kFriendsButtonX, true, layout::kWhisperButtonX);
    const layout::TitleFit whisper = layout::fitChatTitle(float(full), float(speed), titleX, whisperRight);
    expect(whisper.scale == layout::kChatTitleScale, "whisper header stays at scale 0.5");
    expect(whisper.scale >= layout::kChatTitleMin, "whisper header does not shrink below 0.5");
    expect(whisper.shown == layout::ChatTitle::SpeedChat, "menu 17 drops the crumb while the whisper button is up");
    expect(float(full) * whisper.scale > whisperRight - titleX, "the menu 17 crumb does not fit at 0.5");
    expect(float(speed) * whisper.scale <= whisperRight - titleX + 0.05f, "SpeedChat fits beside the whisper button");

    const float friendsRight = layout::chatHeaderLimit(layout::kFriendsButtonX, false, 0.0f);
    const layout::TitleFit friends = layout::fitChatTitle(float(full), float(speed), titleX, friendsRight);
    expect(friends.scale == layout::kChatTitleScale, "friends-only header stays at scale 0.5");
    expect(friends.shown == layout::ChatTitle::SpeedChat, "menu 17 drops the crumb beside Friends too");
    expect(float(speed) * friends.scale <= friendsRight - titleX + 0.05f, "SpeedChat fits beside Friends");
    const layout::TitleFit shortTitle = layout::fitChatTitle(80.0f, float(speed), titleX, friendsRight);
    expect(shortTitle.shown == layout::ChatTitle::Menu && shortTitle.scale == layout::kChatTitleScale,
           "a title that fits at 0.5 stays");
    const layout::TitleFit hidden = layout::fitChatTitle(float(full), float(speed), titleX, titleX + 2.0f);
    expect(hidden.shown == layout::ChatTitle::Hidden && hidden.scale == layout::kChatTitleScale,
           "SpeedChat is dropped too when the header has no room");

    const char* pepper = "To: Little Pepper";
    const int pepperAdv = advanceOf(font, pepper);
    expect(std::strlen(pepper) == 17, "Little Pepper is the whole name, not a 12-character cut");
    const float pepperW = float(pepperAdv) * layout::kWhisperTextScale;
    expect(pepperW <= layout::kWhisperTextMaxW, "To: Little Pepper fits inside the 84 px button");
    const float pepperScale = layout::fitTextScale(pepperW, layout::kWhisperTextScale, layout::kWhisperTextMaxW);
    expect(pepperScale == layout::kWhisperTextScale, "a label that fits is not scaled down");

    const char* wide = "To: WWWWWWWWWWWW";
    const int wideAdv = advanceOf(font, wide);
    expect(std::strlen(wide) == 16, "the twelve-W name is complete");
    const float wideW = float(wideAdv) * layout::kWhisperTextScale;
    expect(wideW > layout::kWhisperTextMaxW, "twelve W names overflow the button at full scale");
    const float wideScale = layout::fitTextScale(wideW, layout::kWhisperTextScale, layout::kWhisperTextMaxW);
    expect(float(wideAdv) * wideScale <= layout::kWhisperTextMaxW + 0.05f,
           "twelve W names scale down whole into 84 px");

    expect(layout::chatLogPaperY() == 174.0f, "drawChatLog passes 174 to theme::paper");
    expect(layout::chatLogPaperY() == layout::chatBottom(true) + layout::kChatLogGap,
           "the paper y is 4 px below the SpeedChat frame");
    expect(layout::chatListContains(169.0f, true), "py 169 is inside the SpeedChat list");
    expect(!layout::chatListContains(170.0f, true), "py 170 is the first pixel of the gap");

    const float corner = layout::hintCorner(pad::battle::kHintW, pad::battle::kHintH);
    const float textBottom = pad::battle::kHintTextTop + pad::battle::kHintTextStep + 12.0f;
    expect(pad::battle::kHintTextTop >= corner, "hint text starts below the corner");
    expect(textBottom < pad::battle::kHintH - corner, "hint text ends in the cream");
    expect(pad::battle::kHintY + pad::battle::kHintH <= pad::battle::kGridY, "hint card stays above the gag grid");

    expect(kBookTabIcons == 9, "nine book tab icons");
    for (int i = 0; i < kBookTabIcons; i++) {
        const BookTabRect& r = kBookTabRects[i];
        expect(r.x >= 0.0f && r.y >= 0.0f && r.x + r.w <= kBookTabAtlasW && r.y + r.h <= kBookTabAtlasH,
               "book tab rect inside the atlas");
        for (int j = 0; j < i; j++) {
            const BookTabRect& o = kBookTabRects[j];
            const bool apart = r.x + r.w <= o.x || o.x + o.w <= r.x || r.y + r.h <= o.y || o.y + o.h <= r.y;
            expect(apart, "book tab rects do not overlap");
        }
    }

    if (gFails) {
        std::fprintf(stderr, "%d of %d layout checks failed\n", gFails, gChecks);
        return 1;
    }
    std::printf("gui_layout_test: %d checks ok\n", gChecks);
    return 0;
}
