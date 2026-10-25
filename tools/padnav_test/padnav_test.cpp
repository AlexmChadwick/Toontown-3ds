#include <cassert>
#include <cstdio>
#include <cstring>

#include "ui/focus.h"

using namespace tt::ui;

void test1DList() {
    printf("Testing 1D List Navigation...\n");
    FocusGroup fg("test_list");
    // 3 items vertically
    fg.addItem(10, 10, 20, 100, 25);
    fg.addItem(20, 10, 50, 100, 25);
    fg.addItem(30, 10, 80, 100, 25);

    // Initial state: unfocused
    assert(!fg.hasFocus());
    assert(fg.focusIndex() == -1);

    // D-Down from unfocused focuses item 0
    fg.move(0, 1);
    assert(fg.focusIndex() == 0);
    assert(fg.focusedId() == 10);

    // D-Down moves to item 1
    fg.move(0, 1);
    assert(fg.focusIndex() == 1);
    assert(fg.focusedId() == 20);

    // D-Down moves to item 2
    fg.move(0, 1);
    assert(fg.focusIndex() == 2);
    assert(fg.focusedId() == 30);

    // D-Down from item 2 wraps to item 0
    fg.move(0, 1);
    assert(fg.focusIndex() == 0);
    assert(fg.focusedId() == 10);

    // D-Up from item 0 wraps to item 2
    fg.move(0, -1);
    assert(fg.focusIndex() == 2);
    assert(fg.focusedId() == 30);

    // D-Up from item 2 moves to item 1
    fg.move(0, -1);
    assert(fg.focusIndex() == 1);

    printf("  1D List OK.\n");
}

void testDisabledSkipping() {
    printf("Testing Disabled Item Skipping...\n");
    FocusGroup fg("test_disabled");
    fg.addItem(1, 10, 20, 50, 20, true);
    fg.addItem(2, 70, 20, 50, 20, false); // disabled!
    fg.addItem(3, 130, 20, 50, 20, true);

    fg.setFocus(0);
    assert(fg.focusIndex() == 0);

    // D-Right skips item 1 (disabled) and lands on item 2 (id 3)
    fg.move(1, 0);
    assert(fg.focusIndex() == 2);
    assert(fg.focusedId() == 3);

    // D-Left skips item 1 and lands on item 0
    fg.move(-1, 0);
    assert(fg.focusIndex() == 0);
    assert(fg.focusedId() == 1);

    // D-Left wraps to item 2
    fg.move(-1, 0);
    assert(fg.focusIndex() == 2);

    printf("  Disabled Skipping OK.\n");
}

void test2DGrid() {
    printf("Testing 2D Grid Navigation...\n");
    FocusGroup fg("test_grid");
    // 3 rows x 3 cols
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            fg.addItem(r * 10 + c, 10.0f + c * 40.0f, 10.0f + r * 30.0f, 35.0f, 25.0f);
        }
    }

    fg.setFocus(0); // (0,0) id=0
    assert(fg.focusedId() == 0);

    // Move right -> (0,1) id=1
    fg.move(1, 0);
    assert(fg.focusedId() == 1);

    // Move right -> (0,2) id=2
    fg.move(1, 0);
    assert(fg.focusedId() == 2);

    // Move right wraps to (0,0) id=0
    fg.move(1, 0);
    assert(fg.focusedId() == 0);

    // Move down -> (1,0) id=10
    fg.move(0, 1);
    assert(fg.focusedId() == 10);

    // Move down -> (2,0) id=20
    fg.move(0, 1);
    assert(fg.focusedId() == 20);

    // Move down wraps to (0,0) id=0
    fg.move(0, 1);
    assert(fg.focusedId() == 0);

    // Move up wraps to (2,0) id=20
    fg.move(0, -1);
    assert(fg.focusedId() == 20);

    printf("  2D Grid OK.\n");
}

void testTouchAndActivate() {
    printf("Testing Touch and Button Activation...\n");
    FocusGroup fg("test_input");
    fg.addItem(100, 20, 20, 60, 30);
    fg.addItem(200, 100, 20, 60, 30);

    // Tap on item 1 (x=120, y=30)
    touchPosition touch{120, 30};
    int activated = -1;
    u32 consumed = fg.handleInput(KEY_TOUCH, touch, &activated);
    assert(consumed & KEY_TOUCH);
    assert(fg.focusIndex() == 1);
    assert(activated == 200);

    // Press A on focused item
    touch = touchPosition{0, 0};
    activated = -1;
    consumed = fg.handleInput(KEY_A, touch, &activated);
    assert(consumed & KEY_A);
    assert(activated == 200);

    // Move to item 0 with D-Left
    consumed = fg.handleInput(KEY_DLEFT, touch, &activated);
    assert(consumed & KEY_DLEFT);
    assert(fg.focusIndex() == 0);
    assert(activated == -1);

    // Activate item 0 with KEY_A
    consumed = fg.handleInput(KEY_A, touch, &activated);
    assert(consumed & KEY_A);
    assert(activated == 100);

    printf("  Touch and Activate OK.\n");
}

void testBattlePanelLayout() {
    printf("Testing Battle Panel Focus Table...\n");
    FocusGroup fg("battle");
    // Target cogs (top row)
    for (int i = 0; i < 4; ++i) {
        fg.addItem(1000 + i, 2.0f + i * 79.0f, 16.0f, 78.0f, 42.0f);
    }
    // Gag grid (7 rows x 7 cols)
    for (int t = 0; t < 7; ++t) {
        for (int l = 0; l < 7; ++l) {
            fg.addItem(t * 10 + l, 58.0f + l * 37.0f, 62.0f + t * 19.0f, 35.0f, 17.0f);
        }
    }
    // Pass & Run
    fg.addItem(9998, 2.0f, 198.0f, 100.0f, 36.0f);  // Pass
    fg.addItem(9999, 110.0f, 198.0f, 100.0f, 36.0f); // Run

    // Set focus on gag (0,0) Throw level 1
    fg.setFocusById(0);
    assert(fg.focusedId() == 0);

    // D-Up from gag row 0 moves to Cog Target row
    fg.move(0, -1);
    assert(fg.focusedId() >= 1000 && fg.focusedId() <= 1003);

    // D-Down moves back to gag grid
    fg.move(0, 1);
    assert(fg.focusedId() < 1000);

    // Move down to bottom gag row (row 6, track 6)
    fg.setFocusById(60);
    // D-Down from bottom row moves to Pass button
    fg.move(0, 1);
    assert(fg.focusedId() == 9998); // Pass

    // D-Right moves from Pass to Run
    fg.move(1, 0);
    assert(fg.focusedId() == 9999); // Run

    // D-Up from Run moves back up into gag grid
    fg.move(0, -1);
    assert(fg.focusedId() < 1000);

    printf("  Battle Panel Layout OK.\n");
}

void testBookOptionsLayout() {
    printf("Testing Book Options Focus Table...\n");
    FocusGroup fg("options");
    fg.addItem(1, 15, 30, 135, 20);  // Stereo 3D
    fg.addItem(2, 15, 54, 135, 20);  // Debug HUD
    fg.addItem(3, 15, 78, 135, 20);  // LOD Bias
    fg.addItem(4, 80, 102, 24, 20);  // Music -
    fg.addItem(5, 126, 102, 24, 20); // Music +
    fg.addItem(6, 80, 126, 24, 20);  // SFX -
    fg.addItem(7, 126, 126, 24, 20); // SFX +
    fg.addItem(8, 80, 150, 24, 20);  // Stress -
    fg.addItem(9, 126, 150, 24, 20); // Stress +
    fg.addItem(10, 15, 176, 135, 20); // Quit

    fg.setFocus(0);
    assert(fg.focusedId() == 1);

    // D-Down moves sequentially down options
    fg.move(0, 1);
    assert(fg.focusedId() == 2);
    fg.move(0, 1);
    assert(fg.focusedId() == 3);
    fg.move(0, 1);
    assert(fg.focusedId() == 4); // Music -
    fg.move(1, 0);
    assert(fg.focusedId() == 5); // Music +
    fg.move(0, 1);
    assert(fg.focusedId() == 7); // SFX +
    fg.move(-1, 0);
    assert(fg.focusedId() == 6); // SFX -
    fg.move(0, 1);
    assert(fg.focusedId() == 8); // Stress -
    fg.move(0, 1);
    assert(fg.focusedId() == 10); // Quit
    fg.move(0, 1);
    assert(fg.focusedId() == 1); // Wrap to Stereo 3D

    printf("  Book Options Layout OK.\n");
}

int main() {
    printf("=== Starting Pad Navigation Host Unit Tests ===\n");
    test1DList();
    testDisabledSkipping();
    test2DGrid();
    testTouchAndActivate();
    testBattlePanelLayout();
    testBookOptionsLayout();
    printf("=== All Pad Navigation Host Tests Passed! ===\n");
    return 0;
}
