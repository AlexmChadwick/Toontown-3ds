// Shared button focus model for bottom-screen panels (docs/design/pad_navigation.md).
// A FocusGroup is a fixed array of rectangles with ids: the D-Pad moves the focus to the
// nearest enabled item in that direction (wrapping at the edges), A activates the focused
// item, and a touch inside an item focuses and activates it. No allocation, ever.
// Every focus change and activation is logged as "padnav: <panel> focus <index>" /
// "padnav: <panel> activate <id>" so autotest runs can be checked from log.txt.
#pragma once

#include <cstdint>
#include <cstddef>

#ifdef __3DS__
#include <3ds.h>
#else
// Host tests (tools/padnav_test): the libctru key bits the focus model uses.
#ifndef KEY_A
#define KEY_A       (1u << 0)
#define KEY_B       (1u << 1)
#define KEY_SELECT  (1u << 2)
#define KEY_START   (1u << 3)
#define KEY_DRIGHT  (1u << 4)
#define KEY_DLEFT   (1u << 5)
#define KEY_DUP     (1u << 6)
#define KEY_DDOWN   (1u << 7)
#define KEY_R       (1u << 8)
#define KEY_L       (1u << 9)
#define KEY_X       (1u << 10)
#define KEY_Y       (1u << 11)
#define KEY_TOUCH   (1u << 20)
#endif
typedef uint32_t u32;
typedef uint8_t u8;
struct touchPosition {
    uint16_t px;
    uint16_t py;
};
#endif

namespace tt::ui {

// All four D-Pad directions.
constexpr u32 kDpadKeys = KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT;

struct FocusItem {
    int id = -1;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool enabled = true;
};

class FocusGroup {
public:
    static constexpr int kMaxItems = 80;

    FocusGroup() = default;
    explicit FocusGroup(const char* panelName);

    void setPanelName(const char* name);
    const char* panelName() const { return panelName_; }

    // Removes every item and the focus.
    void clear();
    // Starts a new item list but keeps the focus: when the focused item's id is added
    // again (and enabled) it is focused again without a log line. Panels whose controls
    // change from frame to frame (battle, quest choices) rebuild with this every frame.
    void rebuild();
    // Returns the item index, or -1 when the group is full.
    int addItem(int id, float x, float y, float w, float h, bool enabled = true);
    void setItemEnabled(int index, bool enabled);
    void setItemRect(int index, float x, float y, float w, float h);

    int count() const { return itemCount_; }
    const FocusItem& item(int index) const { return items_[index]; }

    int focusIndex() const { return focusIndex_; }
    int focusedId() const;
    bool hasFocus() const;
    // Index of the enabled item under a touch point (half-open rectangles), or -1.
    int itemAt(float px, float py) const;
    // Index of the item with this id, or -1.
    int indexOf(int id) const;

    bool setFocus(int index);
    bool setFocusById(int id);
    void blur();

    // Directional navigation: dx, dy in {-1, 0, +1}. Skips disabled items and wraps.
    // With nothing focused, focuses the first enabled item.
    bool move(int dx, int dy);

    // Moves focus to next/prev in linear index order, skipping disabled items.
    bool moveLinear(int step);

    // D-Pad moves, A activates the focused item, a touch focuses and (when
    // activateOnTouch) activates the item under it. *outActivatedId gets the activated id
    // or -1. Returns the buttons it used.
    u32 handleInput(u32 down, const touchPosition& touch, int* outActivatedId = nullptr, bool activateOnTouch = true);

    // Logs an activation that a panel performs itself (e.g. a shortcut button).
    void logActivate(int id) const;

    // Draws the focus highlight (3DS only; a no-op on the host).
    void drawHighlight(float z = 0.65f) const;

private:
    char panelName_[24] = "panel";
    FocusItem items_[kMaxItems]{};
    int itemCount_ = 0;
    int focusIndex_ = -1;
    int keepId_ = -1; // rebuild(): the id to focus again when it is re-added
};

} // namespace tt::ui
