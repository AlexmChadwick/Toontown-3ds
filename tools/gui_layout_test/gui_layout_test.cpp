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
    int lineFeed = 0;
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
    Font font;
    font.lineFeed = int(uint8_t(s[finf + 9]));
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

int glyphWidths(const Font& font, const char* text, float* out) {
    int n = 0;
    for (const char* p = text; *p; p++, n++) {
        const char one[2] = {*p, 0};
        out[n] = float(advanceOf(font, one));
    }
    return n;
}

tt::ui::layout::WhisperLabel fitName(const Font& font, const char* name, char* line0, char* line1) {
    float glyphs[64];
    const int n = glyphWidths(font, name, glyphs);
    const tt::ui::layout::WhisperLabel fit = tt::ui::layout::fitWhisperName(float(advanceOf(font, "To: ")),
                                                                            float(advanceOf(font, ".")), glyphs, n, name);
    tt::ui::layout::whisperLines(fit, name, line0, 64, line1, 64);
    return fit;
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

    char line0[64], line1[64];
    const layout::WhisperLabel pepper = fitName(font, "Little Pepper", line0, line1);
    expect(std::strcmp(line0, "To: Little Pepper") == 0, "Little Pepper keeps the To: prefix");
    expect(line1[0] == 0, "Little Pepper stays on one line");
    expect(pepper.scale == layout::kWhisperTextScale, "Little Pepper stays at 0.42");
    expect(pepper.kind == layout::WhisperKind::Prefixed, "Little Pepper is not rewritten");

    const char* trip = "Baron Alvin Blubberflinger";
    expect(std::strlen(trip) == 26, "the fixture is a 26-character three-part name");
    const layout::WhisperLabel three = fitName(font, trip, line0, line1);
    expect(three.scale >= layout::kWhisperTextFloor, "the 26-character name stays at or above 0.36");
    expect(three.kind != layout::WhisperKind::Cut, "the 26-character name is not cut");
    expect(std::strcmp(line0, "Baron Alvin") == 0, "the 26-character name wraps after Alvin");
    expect(std::strcmp(line1, "Blubberflinger") == 0, "the 26-character second line is the whole last word");

    const layout::WhisperLabel wide = fitName(font, "WWWWWWWWWWWW", line0, line1);
    expect(wide.scale >= layout::kWhisperTextFloor, "twelve W stays at or above 0.36");
    expect(wide.kind != layout::WhisperKind::Cut, "twelve W wraps without a cut");
    expect(std::strlen(line0) + std::strlen(line1) == 12, "twelve W keeps every glyph");

    const layout::WhisperLabel mid = fitName(font, "MMMMMMMMMMM", line0, line1);
    expect(mid.kind == layout::WhisperKind::Name, "a name that fits alone drops To:");
    expect(mid.scale >= layout::kWhisperTextFloor && mid.scale <= layout::kWhisperTextScale,
           "the dropped-prefix name stays between 0.36 and 0.42");
    expect(line1[0] == 0, "a one-line name is not wrapped");

    const layout::WhisperLabel cut = fitName(font, "WWWWWWWWWWWWWWWWWWWWWWW", line0, line1);
    expect(cut.kind == layout::WhisperKind::Cut, "a second line that still overflows is cut");
    expect(cut.scale >= layout::kWhisperTextFloor, "the cut label stays at or above 0.36");
    const int line1n = int(std::strlen(line1));
    expect(line1n > 1 && line1[line1n - 1] == layout::kWhisperMark, "the cut adds one trailing glyph");
    expect(std::strchr(line1, layout::kWhisperMark) == line1 + line1n - 1, "the mark is a single glyph");
    float kept = 0.0f;
    for (int i = 0; i < line1n - 1; i++) kept += float(font.advance.at(uint8_t(line1[i])));
    const float mark = float(font.advance.at(uint8_t(layout::kWhisperMark)));
    const float next = float(font.advance.at(uint8_t('W')));
    expect((kept + mark) * cut.scale <= layout::kWhisperTextMaxW + 0.05f, "the cut line fits in 84 px");
    expect((kept + next + mark) * cut.scale > layout::kWhisperTextMaxW, "the cut stops on a glyph boundary");

    const layout::WhisperLabel paths[] = {pepper, three, wide, mid, cut};
    for (const layout::WhisperLabel& path : paths)
        expect(path.scale >= layout::kWhisperTextFloor, "every whisper path stays at or above 0.36");

    expect(font.lineFeed == int(layout::kWhisperLineAt1), "impress.bcfnt line feed is 30");
    const float linePx = float(font.lineFeed) * layout::kWhisperTextFloor;
    expect(linePx >= 9.0f && linePx <= 12.0f, "a wrapped line is about 10 px");
    expect(linePx * 2.0f <= layout::kWhisperButtonH, "two lines fit in the 26 px button");

    expect(layout::chatLogPaperY() == 174.0f, "drawChatLog passes 174 to theme::paper");
    expect(layout::chatLogPaperY() == layout::chatBottom(true) + layout::kChatLogGap,
           "the paper y is 4 px below the SpeedChat frame");
    expect(layout::chatListContains(169.0f, true), "py 169 is inside the SpeedChat list");
    expect(!layout::chatListContains(170.0f, true), "py 170 is the first pixel of the gap");
    expect(layout::kChatRowH >= 30.0f, "SpeedChat rows stay at least 30 px");
    expect(layout::chatWholeRows(layout::kChatFrameBottom) == 4, "four whole rows fit above the chat log");
    expect(layout::chatListBottom(layout::kChatFrameBottom) == 170.0f, "those four rows fill the frame");
    expect(layout::chatListBottom(layout::kPocketTop) == 170.0f, "the pocket sliver is not a fifth row");
    expect(!layout::chatListContains(171.0f, false), "py 171 is outside the list when the log is hidden");
    expect(layout::chatListContains(169.0f, false), "py 169 is still inside the list when the log is hidden");
    const int tracks = 7;
    const float menuScroll = layout::chatMaxScroll(tracks, layout::kChatFrameBottom);
    expect(menuScroll == 3.0f * layout::kChatRowH, "seven phrases scroll by three whole rows");
    expect(layout::snapChatScroll(0.0f, menuScroll) == 0.0f, "scroll rests on a row");
    expect(layout::snapChatScroll(10.0f, menuScroll) == 0.0f, "a short drag stays on the current row");
    expect(layout::snapChatScroll(20.0f, menuScroll) == layout::kChatRowH, "a longer drag steps one row");
    expect(layout::snapChatScroll(menuScroll, menuScroll) == menuScroll, "the last page is a whole row");
    expect(!layout::chatHintUp(0.0f) && layout::chatHintDown(0.0f, menuScroll),
           "the first page shows a down arrow on the scrollbar");
    expect(layout::chatHintUp(menuScroll) && !layout::chatHintDown(menuScroll, menuScroll),
           "the last page shows an up arrow");
    const int last = layout::chatWholeRows(layout::kChatFrameBottom) - 1;
    const float clip = layout::chatListBottom(layout::kChatFrameBottom) - layout::kChatClipInset;
    expect(layout::chatRowTop(last) + layout::kChatRowH <= layout::chatListBottom(layout::kChatFrameBottom) + 0.01f,
           "the last visible row ends on the frame");
    expect(layout::chatRowTop(last + 1) >= layout::chatListBottom(layout::kChatFrameBottom),
           "the next row is not partly visible");
    expect(layout::chatRowTextBottom(last, float(font.lineFeed) * 0.5f) <= clip, "one line sits above the clip");
    expect(layout::chatRowTextBottom(last, float(font.lineFeed) * 0.42f * 2.0f) <= clip,
           "two lines sit above the clip");

    const layout::MapCard book = layout::kBookMapCard;
    const layout::MapCard pocket = layout::kPocketMapCard;
    expect(book.legend && pocket.legend, "both maps keep the legend");
    expect(book.legendW >= 66.0f && pocket.legendW >= 66.0f, "legend column stays 66 px");
    expect(book.mapH + 0.1f >= layout::kMapLegendMax * layout::kMapLegendRow, "book legend rows fit");
    expect(pocket.mapH + 0.1f >= layout::kMapLegendMax * layout::kMapLegendRow, "pocket legend rows fit");
    expect(book.legendX >= book.mapX + book.mapW && pocket.legendX >= pocket.mapX + pocket.mapW,
           "the legend sits beside the map");
    expect(book.legendX - (book.mapX + book.mapW) == layout::kMapLegendGap, "book legend gap is 4 px");
    expect(pocket.legendX - (pocket.mapX + pocket.mapW) == layout::kMapLegendGap, "pocket legend gap is 4 px");
    const layout::MapCard wideMap =
        layout::layoutMapCard(layout::kBookMapX, layout::kBookMapY, layout::kBookMapW, layout::kBookMapH, 3.0f, true);
    expect(wideMap.legend, "a wide map still keeps the legend");
    expect(wideMap.x >= 0.0f && wideMap.y >= 0.0f && wideMap.x + wideMap.w + wideMap.shadow <= 320.0f &&
               wideMap.y + wideMap.h + wideMap.shadow <= 240.0f,
           "a wide map card stays inside 320x240");
    const layout::MapCard bare = layout::layoutMapCard(4.0f, 4.0f, 312.0f, 140.0f, 1.0f, false);
    expect(!bare.legend, "a map without a legend does not reserve a column");
    expect(bare.x >= 0.0f && bare.y >= 0.0f && bare.x + bare.w + bare.shadow <= 320.0f &&
               bare.y + bare.h + bare.shadow <= 240.0f,
           "a map without a legend stays inside 320x240");
    expect(book.w <= layout::kBookMapW - 80.0f && pocket.w <= layout::kPocketMapW - 80.0f,
           "the card is narrower than the old full-bleed paper");
    const layout::MapCard cards[] = {book, pocket};
    for (const layout::MapCard& card : cards) {
        expect(card.x >= 0.0f && card.y >= 0.0f, "map card origin is on screen");
        expect(card.x + card.w + card.shadow <= 320.0f, "map card shadow stays inside 320");
        expect(card.y + card.h + card.shadow <= 240.0f, "map card shadow stays inside 240");
        expect(card.y + card.h + card.shadow <= pad::hoods::kRowY, "map card shadow stays above the hood row");
        expect(card.mapX >= card.x + layout::kMapBorder && card.mapY >= card.y + layout::kMapBorder,
               "the map sits inside the card border");
        expect(card.mapX + card.mapW <= card.x + card.w - layout::kMapBorder, "the map stays inside the card");
        expect(card.legendX + card.legendW <= card.x + card.w - layout::kMapBorder, "the legend stays inside the card");
    }
    expect(layout::kBookMapH == pad::hoods::kRowY - 31.0f, "book map rect matches the hood row");
    expect(layout::kPocketMapH == pad::hoods::kRowY - 8.0f, "pocket map rect matches the hood row");

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
