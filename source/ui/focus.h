#pragma once

#include <cstdint>
#include <cstddef>

#ifdef __3DS__
#include <3ds.h>
#include <citro2d.h>
#else
// Host testing definitions
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

    void clear();
    int addItem(int id, float x, float y, float w, float h, bool enabled = true);
    void setItemEnabled(int index, bool enabled);
    void setItemRect(int index, float x, float y, float w, float h);

    int count() const { return itemCount_; }
    const FocusItem& item(int index) const { return items_[index]; }
    FocusItem& item(int index) { return items_[index]; }

    int focusIndex() const { return focusIndex_; }
    int focusedId() const;
    bool hasFocus() const;

    bool setFocus(int index);
    bool setFocusById(int id);
    void blur();

    // Directional navigation: dx (-1, 0, +1), dy (-1, 0, +1).
    // Skips disabled items and wraps around.
    bool move(int dx, int dy);

    // Moves focus to next/prev in linear index order, skipping disabled items.
    bool moveLinear(int step);

    // Updates focus based on D-Pad and Touch.
    // If an item was activated (by KEY_A or touch tap), returns its id (or outActivatedId != nullptr).
    // Returns buttons consumed.
    u32 handleInput(u32 down, const touchPosition& touch, int* outActivatedId = nullptr, bool activateOnTouch = true);

    // Draw the high-contrast focus highlight for the current focused item.
    void drawHighlight(float z = 0.65f) const;

private:
    char panelName_[24] = "panel";
    FocusItem items_[kMaxItems]{};
    int itemCount_ = 0;
    int focusIndex_ = -1;
};

} // namespace tt::ui
