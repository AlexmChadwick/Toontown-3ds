// Host tests for the pad focus model (source/ui/focus.*) and every panel's focus table
// (source/ui/pad_tables.*): grid moves, wrapping, disabled entries, touch, rebuilds that keep
// the focus without log spam, and where the D-Pad goes on each real panel layout.
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "ui/focus.h"
#include "ui/pad_tables.h"

using namespace tt::ui;

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++gChecks;                                                           \
        if (!(cond)) {                                                       \
            ++gFailures;                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

const touchPosition kNoTouch{0, 0};

// Presses one button and returns the activated id (-1 for none).
int press(FocusGroup& group, u32 key) {
    int activated = -1;
    group.handleInput(key, kNoTouch, &activated);
    return activated;
}

int tap(FocusGroup& group, int x, int y) {
    int activated = -1;
    const touchPosition touch{static_cast<uint16_t>(x), static_cast<uint16_t>(y)};
    group.handleInput(KEY_TOUCH, touch, &activated);
    return activated;
}

bool lastLogIs(const char* line) {
    return std::strcmp(tt::log::recent(0), line) == 0;
}

void testList() {
    FocusGroup group("list");
    group.addItem(10, 10, 20, 100, 25);
    group.addItem(20, 10, 50, 100, 25);
    group.addItem(30, 10, 80, 100, 25);
    CHECK(!group.hasFocus());
    CHECK(press(group, KEY_A) == -1); // nothing focused: A is not used
    group.move(0, 1);
    CHECK(group.focusedId() == 10); // the first press focuses the first item
    CHECK(lastLogIs("padnav: list focus 0"));
    group.move(0, 1);
    CHECK(group.focusedId() == 20);
    group.move(0, 1);
    CHECK(group.focusedId() == 30);
    group.move(0, 1);
    CHECK(group.focusedId() == 10); // wraps to the top
    group.move(0, -1);
    CHECK(group.focusedId() == 30); // and back to the bottom
    CHECK(press(group, KEY_A) == 30);
    CHECK(lastLogIs("padnav: list activate 30"));
    group.moveLinear(1);
    CHECK(group.focusedId() == 10);
    group.blur();
    CHECK(!group.hasFocus());
    CHECK(lastLogIs("padnav: list blur"));
}

void testDisabledSkipped() {
    FocusGroup group("disabled");
    group.addItem(1, 10, 20, 50, 20);
    group.addItem(2, 70, 20, 50, 20, false);
    group.addItem(3, 130, 20, 50, 20);
    group.setFocus(0);
    group.move(1, 0);
    CHECK(group.focusedId() == 3);
    group.move(-1, 0);
    CHECK(group.focusedId() == 1);
    group.move(-1, 0);
    CHECK(group.focusedId() == 3); // wraps past the disabled one
    CHECK(!group.setFocus(1));
    CHECK(tap(group, 80, 25) == -1); // a disabled control ignores touch
    group.setItemEnabled(2, false);
    CHECK(!group.hasFocus()); // disabling the focused item drops the focus
}

void testGridAndWrap() {
    FocusGroup group("grid");
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) group.addItem(r * 10 + c, 10.0f + c * 40.0f, 10.0f + r * 30.0f, 35.0f, 25.0f);
    }
    group.setFocusById(11);
    group.move(1, 0);
    CHECK(group.focusedId() == 12);
    group.move(1, 0);
    CHECK(group.focusedId() == 10); // a row wraps within itself
    group.move(0, 1);
    CHECK(group.focusedId() == 20);
    group.move(0, 1);
    CHECK(group.focusedId() == 0); // a column wraps within itself
    group.move(-1, 0);
    CHECK(group.focusedId() == 2);
}

void testTouch() {
    FocusGroup group("touch");
    group.addItem(100, 20, 20, 60, 30);
    group.addItem(200, 100, 20, 60, 30);
    CHECK(tap(group, 120, 30) == 200);
    CHECK(group.focusedId() == 200); // a touch moves the focus there
    CHECK(tap(group, 80, 30) == -1); // half-open: x = 80 is past the first button
    CHECK(tap(group, 100, 20) == 200);
    int activated = -1;
    const touchPosition touch{30, 30};
    group.handleInput(KEY_TOUCH, touch, &activated, false);
    CHECK(activated == -1);
    CHECK(group.focusedId() == 100); // focus only
}

void testRebuildKeepsFocusQuietly() {
    FocusGroup group("rebuild");
    group.addItem(5, 0, 0, 10, 10);
    group.addItem(6, 20, 0, 10, 10);
    group.setFocusById(6);
    tt::log::write("marker");
    for (int frame = 0; frame < 3; ++frame) {
        group.rebuild();
        group.addItem(5, 0, 0, 10, 10);
        group.addItem(6, 20, 0, 10, 10);
    }
    CHECK(group.focusedId() == 6);
    CHECK(lastLogIs("marker")); // no "focus" line every frame
    group.rebuild();
    group.addItem(5, 0, 0, 10, 10);
    group.addItem(6, 20, 0, 10, 10, false);
    CHECK(!group.hasFocus()); // the focused control went away
    group.clear();
    CHECK(group.count() == 0);
}

void testBattleTable() {
    namespace b = pad::battle;
    FocusGroup group("battle");
    const bool cogs[b::kMaxCogs] = {true, false, true, true};
    bool usable[b::kTracks * b::kLevels] = {};
    usable[4 * b::kLevels + 0] = true; // throw 1
    usable[4 * b::kLevels + 1] = true; // throw 2
    usable[5 * b::kLevels + 0] = true; // squirt 1
    pad::buildBattleInput(group, 4, cogs, usable, false);
    CHECK(group.count() == 4 + 49 + 2);
    group.setFocusById(b::gagId(4, 0));
    group.move(1, 0);
    CHECK(group.focusedId() == b::gagId(4, 1));
    group.move(1, 0);
    CHECK(group.focusedId() == b::gagId(4, 0)); // locked levels are skipped
    group.move(0, 1);
    CHECK(group.focusedId() == b::gagId(5, 0));
    group.move(0, 1);
    CHECK(group.focusedId() == b::kIdPass); // Run is disabled: Pass sits under the grid
    group.move(1, 0);
    CHECK(group.focusedId() == b::kIdPass); // nothing else on that row
    group.setFocusById(b::gagId(4, 0));
    group.move(0, -1);
    CHECK(group.focusedId() >= b::kIdCog0 && group.focusedId() < b::kIdCog0 + b::kMaxCogs);
    CHECK(group.focusedId() != b::kIdCog0 + 1); // the defeated cog is skipped
    // A tap on the old touch rectangles still lands on the same controls.
    CHECK(tap(group, int(b::kCogX + 2 * (b::kCogW + b::kCogGap)) + 5, int(b::kCogY) + 5) == b::kIdCog0 + 2);
    CHECK(tap(group, int(b::kPassX) + 5, int(b::kBtnY) + 5) == b::kIdPass);
    // Every frame rebuilds the table; the focus stays.
    group.setFocusById(b::gagId(5, 0));
    pad::buildBattleInput(group, 4, cogs, usable, true);
    CHECK(group.focusedId() == b::gagId(5, 0));
    group.setFocusById(b::kIdPass);
    group.move(1, 0);
    CHECK(group.focusedId() == b::kIdRun);
    pad::buildBattleReward(group);
    CHECK(group.count() == 1 && group.item(0).id == b::kIdContinue);
}

void testQuestTables() {
    namespace q = pad::quest;
    FocusGroup group("quest");
    pad::buildQuestChoices(group, 3);
    CHECK(group.count() == 4);
    CHECK(!group.hasFocus()); // no card until the D-Pad is used
    group.move(0, 1);
    CHECK(group.focusedId() == 0);
    group.move(0, 1);
    group.move(0, 1);
    CHECK(group.focusedId() == 2);
    group.move(0, 1);
    CHECK(group.focusedId() == q::kIdCancel);
    CHECK(tap(group, 20, int(q::kCardY + q::kCardH + q::kCardGap) + 5) == 1);
    pad::buildTrackChoices(group, 4);
    group.setFocusById(0);
    group.move(1, 0);
    CHECK(group.focusedId() == 1);
    group.move(0, 1);
    CHECK(group.focusedId() == 3);
    group.move(0, 1);
    CHECK(group.focusedId() == q::kIdCancel);
    pad::buildQuestPages(group);
    CHECK(group.count() == 1 && group.item(0).id == q::kIdNext);
}

void testClosetAndTailor() {
    FocusGroup closet("closet");
    pad::buildCloset(closet);
    closet.setFocusById(pad::closet::kIdDone);
    CHECK(press(closet, KEY_A) == pad::closet::kIdDone);
    closet.move(1, 0);
    CHECK(closet.focusedId() == pad::closet::kIdCancel);
    CHECK(tap(closet, 170, 200) == pad::closet::kIdDone);

    FocusGroup tailor("tailor");
    pad::buildTailor(tailor);
    tailor.setFocusById(pad::tailor::kIdShorts);
    tailor.move(0, 1);
    CHECK(tailor.focusedId() == pad::tailor::kIdDone);
    tailor.move(1, 0);
    CHECK(tailor.focusedId() == pad::tailor::kIdCancel);
    tailor.move(0, -1);
    CHECK(tailor.focusedId() == pad::tailor::kIdShorts);
}

void testCounterButtons() {
    namespace c = pad::counter;
    FocusGroup group("shop");
    pad::buildCounterButtons(group);
    CHECK(group.count() == 4);
    group.setFocusById(c::kIdDone);
    group.move(1, 0);
    CHECK(group.focusedId() == c::kIdCancel);
    group.move(1, 0);
    CHECK(group.focusedId() == c::kIdMinus); // the row wraps
    group.move(-1, 0);
    CHECK(group.focusedId() == c::kIdCancel);
    CHECK(group.itemAt(c::kPlusX + 1, c::kBtnY + 1) == 1);
}

void testHoodRow() {
    namespace h = pad::hoods;
    FocusGroup group("map");
    pad::buildHoodRow(group);
    CHECK(group.count() == 1 + h::kCount);
    group.move(0, 1);
    CHECK(group.focusedId() == h::kIdHome);
    group.move(0, 1);
    CHECK(group.focusedId() >= 0 && group.focusedId() < 2); // the hood under Home
    group.setFocusById(h::kCount - 1);
    group.move(1, 0);
    CHECK(group.focusedId() == 0);
    group.move(0, -1);
    CHECK(group.focusedId() == h::kIdHome);
    // A touch anywhere in a hood's column hits it, as the old px / width test did.
    for (int i = 0; i < h::kCount; ++i) {
        CHECK(tap(group, int(i * h::kHoodW), int(h::kRowY)) == i);
        CHECK(tap(group, int((i + 1) * h::kHoodW) - 1, int(h::kRowY + h::kRowH) - 1) == i);
    }
}

void testToonAndSetupTabs() {
    namespace t = pad::toon;
    FocusGroup group("tab_toon");
    pad::buildToonTab(group, true);
    CHECK(group.count() == 1);
    group.move(0, 1);
    CHECK(group.focusedId() == t::kIdBook);
    pad::buildToonTab(group, false);
    CHECK(group.count() == 3 + t::kNumColors);
    CHECK(!group.hasFocus());
    group.move(0, 1);
    CHECK(group.focusedId() == t::kIdTarget0);
    group.move(0, 1);
    CHECK(group.focusedId() == 0 || group.focusedId() == 1); // the swatch under "Body"
    group.setFocusById(0);
    group.move(0, 1);
    CHECK(group.focusedId() == t::kSwCols);
    group.move(0, 1);
    CHECK(group.focusedId() == 2 * t::kSwCols);
    group.move(1, 0);
    CHECK(group.focusedId() == 2 * t::kSwCols + 1);
    group.setFocusById(t::kSwCols - 1);
    group.move(1, 0);
    CHECK(group.focusedId() == 0); // rows wrap

    namespace s = pad::setup;
    FocusGroup setup("tab_setup");
    pad::buildSetupTab(setup);
    setup.move(0, 1);
    CHECK(setup.focusedId() == s::kIdStereo);
    setup.move(0, 1);
    CHECK(setup.focusedId() == s::kIdStressMinus);
    setup.move(1, 0);
    CHECK(setup.focusedId() == s::kIdStressPlus);
    setup.move(0, -1);
    CHECK(setup.focusedId() == s::kIdStereo);
}

void testBookTables() {
    namespace k = pad::book;
    FocusGroup options("book_options");
    pad::buildBookOptions(options);
    CHECK(options.count() == 10);
    options.setFocusById(k::kIdStereo);
    options.move(0, 1);
    CHECK(options.focusedId() == k::kIdDebugHud);
    options.move(0, 1);
    CHECK(options.focusedId() == k::kIdLodBias);
    options.move(0, 1);
    CHECK(options.focusedId() == k::kIdMusicMinus);
    options.move(1, 0);
    CHECK(options.focusedId() == k::kIdMusicPlus);
    options.move(0, 1);
    CHECK(options.focusedId() == k::kIdSfxPlus);
    options.move(-1, 0);
    CHECK(options.focusedId() == k::kIdSfxMinus);
    options.move(0, 1);
    CHECK(options.focusedId() == k::kIdStressMinus);
    options.move(0, 1);
    CHECK(options.focusedId() == k::kIdQuit);
    options.move(0, 1);
    CHECK(options.focusedId() == k::kIdStereo);
    // The old touch rectangles (24 px wide steppers) still hit.
    CHECK(tap(options, 80 + 23, 102 + 10) == k::kIdMusicMinus);

    FocusGroup depts("book_cogs");
    pad::buildBookDepartments(depts);
    CHECK(depts.count() == k::kDepts);
    // These are screen-space rectangles below the book's 24 px cover bar. The old
    // page-local hit tests accidentally put most of them behind that bar.
    for (int d = 0; d < k::kDepts; ++d) {
        CHECK(tap(depts, int(k::kDeptX + d * k::kDeptStep + 1), int(k::kDeptY + 1)) == d);
    }
    depts.setFocusById(3);
    depts.move(1, 0);
    CHECK(depts.focusedId() == 0);
    CHECK(press(depts, KEY_A) == 0);

    FocusGroup fish("book_fish");
    pad::buildBookFishArrows(fish);
    CHECK(fish.count() == 2 && fish.item(1).id == k::kIdGenusNext);
    CHECK(tap(fish, int(k::kGenusPrevX + 1), int(k::kGenusY + 1)) == k::kIdGenusPrev);
    CHECK(tap(fish, int(k::kGenusNextX + 1), int(k::kGenusY + 1)) == k::kIdGenusNext);
    CHECK(press(fish, KEY_A) == k::kIdGenusNext);

    FocusGroup sos("book_sos");
    pad::buildBookSosPage(sos);
    CHECK(sos.count() == 1 && sos.item(0).id == k::kIdSosPage);
    CHECK(tap(sos, int(k::kSosPageX + 1), int(k::kSosPageY + 1)) == k::kIdSosPage);
    CHECK(press(sos, KEY_A) == k::kIdSosPage);
}

} // namespace

int main() {
    testList();
    testDisabledSkipped();
    testGridAndWrap();
    testTouch();
    testRebuildKeepsFocusQuietly();
    testBattleTable();
    testQuestTables();
    testClosetAndTailor();
    testCounterButtons();
    testHoodRow();
    testToonAndSetupTabs();
    testBookTables();
    std::printf("padnav_test: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
