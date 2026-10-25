#include "ui/focus.h"
#include "core/log.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tt::ui {

FocusGroup::FocusGroup(const char* panelName) {
    setPanelName(panelName);
}

void FocusGroup::setPanelName(const char* name) {
    if (name) {
        snprintf(panelName_, sizeof(panelName_), "%s", name);
    }
}

void FocusGroup::clear() {
    itemCount_ = 0;
    focusIndex_ = -1;
}

int FocusGroup::addItem(int id, float x, float y, float w, float h, bool enabled) {
    if (itemCount_ >= kMaxItems) return -1;
    const int idx = itemCount_++;
    items_[idx].id = id;
    items_[idx].x = x;
    items_[idx].y = y;
    items_[idx].w = w;
    items_[idx].h = h;
    items_[idx].enabled = enabled;
    return idx;
}

void FocusGroup::setItemEnabled(int index, bool enabled) {
    if (index >= 0 && index < itemCount_) {
        items_[index].enabled = enabled;
        if (!enabled && focusIndex_ == index) {
            blur();
        }
    }
}

void FocusGroup::setItemRect(int index, float x, float y, float w, float h) {
    if (index >= 0 && index < itemCount_) {
        items_[index].x = x;
        items_[index].y = y;
        items_[index].w = w;
        items_[index].h = h;
    }
}

int FocusGroup::focusedId() const {
    if (hasFocus()) {
        return items_[focusIndex_].id;
    }
    return -1;
}

bool FocusGroup::hasFocus() const {
    return focusIndex_ >= 0 && focusIndex_ < itemCount_ && items_[focusIndex_].enabled;
}

bool FocusGroup::setFocus(int index) {
    if (index < 0 || index >= itemCount_) return false;
    if (!items_[index].enabled) return false;
    if (focusIndex_ != index) {
        focusIndex_ = index;
        TT_LOG("padnav: %s focus %d", panelName_, focusIndex_);
    }
    return true;
}

bool FocusGroup::setFocusById(int id) {
    for (int i = 0; i < itemCount_; ++i) {
        if (items_[i].id == id && items_[i].enabled) {
            return setFocus(i);
        }
    }
    return false;
}

void FocusGroup::blur() {
    focusIndex_ = -1;
}

bool FocusGroup::move(int dx, int dy) {
    if (itemCount_ <= 0) return false;

    // If currently unfocused, focus the first enabled item
    if (focusIndex_ < 0 || focusIndex_ >= itemCount_ || !items_[focusIndex_].enabled) {
        for (int i = 0; i < itemCount_; ++i) {
            if (items_[i].enabled) {
                return setFocus(i);
            }
        }
        return false;
    }

    const float curCx = items_[focusIndex_].x + items_[focusIndex_].w * 0.5f;
    const float curCy = items_[focusIndex_].y + items_[focusIndex_].h * 0.5f;

    int bestIdx = -1;
    float bestScore = 1e9f;

    // Helper: compute direction score with strong same-band preference
    auto calcScore = [](float mainDelta, float perpDelta, bool sameBand) {
        float score = mainDelta;
        if (sameBand) {
            score += perpDelta * 0.5f;
        } else {
            score += 200.0f + perpDelta * 4.0f;
        }
        return score;
    };

    // 1. Search for items strictly in the given direction
    for (int i = 0; i < itemCount_; ++i) {
        if (i == focusIndex_ || !items_[i].enabled) continue;

        const float candCx = items_[i].x + items_[i].w * 0.5f;
        const float candCy = items_[i].y + items_[i].h * 0.5f;
        const float delX = candCx - curCx;
        const float delY = candCy - curCy;

        float score = 1e9f;
        if (dx > 0 && delX > 1.0f) {
            const bool sameRow = std::fabs(delY) <= (items_[focusIndex_].h + items_[i].h) * 0.5f;
            score = calcScore(delX, std::fabs(delY), sameRow);
        } else if (dx < 0 && delX < -1.0f) {
            const bool sameRow = std::fabs(delY) <= (items_[focusIndex_].h + items_[i].h) * 0.5f;
            score = calcScore(-delX, std::fabs(delY), sameRow);
        } else if (dy > 0 && delY > 1.0f) {
            const bool sameCol = std::fabs(delX) <= (items_[focusIndex_].w + items_[i].w) * 0.5f;
            score = calcScore(delY, std::fabs(delX), sameCol);
        } else if (dy < 0 && delY < -1.0f) {
            const bool sameCol = std::fabs(delX) <= (items_[focusIndex_].w + items_[i].w) * 0.5f;
            score = calcScore(-delY, std::fabs(delX), sameCol);
        }

        if (score < bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }

    // 2. If nothing found in that direction, wrap around
    if (bestIdx < 0) {
        for (int i = 0; i < itemCount_; ++i) {
            if (i == focusIndex_ || !items_[i].enabled) continue;

            const float candCx = items_[i].x + items_[i].w * 0.5f;
            const float candCy = items_[i].y + items_[i].h * 0.5f;
            const float delX = candCx - curCx;
            const float delY = candCy - curCy;

            float score = 1e9f;
            if (dx > 0) {
                // Wrap right -> find leftmost (min candCx)
                const bool sameRow = std::fabs(delY) <= (items_[focusIndex_].h + items_[i].h) * 0.5f;
                score = calcScore(candCx, std::fabs(delY), sameRow);
            } else if (dx < 0) {
                // Wrap left -> find rightmost (max candCx)
                const bool sameRow = std::fabs(delY) <= (items_[focusIndex_].h + items_[i].h) * 0.5f;
                score = calcScore(-candCx, std::fabs(delY), sameRow);
            } else if (dy > 0) {
                // Wrap down -> find topmost (min candCy)
                const bool sameCol = std::fabs(delX) <= (items_[focusIndex_].w + items_[i].w) * 0.5f;
                score = calcScore(candCy, std::fabs(delX), sameCol);
            } else if (dy < 0) {
                // Wrap up -> find bottommost (max candCy)
                const bool sameCol = std::fabs(delX) <= (items_[focusIndex_].w + items_[i].w) * 0.5f;
                score = calcScore(-candCy, std::fabs(delX), sameCol);
            }

            if (score < bestScore) {
                bestScore = score;
                bestIdx = i;
            }
        }
    }

    if (bestIdx >= 0) {
        return setFocus(bestIdx);
    }
    return false;
}

bool FocusGroup::moveLinear(int step) {
    if (itemCount_ <= 0 || step == 0) return false;
    int idx = (focusIndex_ < 0) ? 0 : focusIndex_;
    for (int count = 0; count < itemCount_; ++count) {
        idx = (idx + step) % itemCount_;
        if (idx < 0) idx += itemCount_;
        if (items_[idx].enabled) {
            return setFocus(idx);
        }
    }
    return false;
}

u32 FocusGroup::handleInput(u32 down, const touchPosition& touch, int* outActivatedId, bool activateOnTouch) {
    u32 consumed = 0;
    if (outActivatedId) *outActivatedId = -1;

    if (down & KEY_TOUCH) {
        for (int i = 0; i < itemCount_; ++i) {
            if (!items_[i].enabled) continue;
            const auto& it = items_[i];
            if (touch.px >= it.x && touch.px <= it.x + it.w &&
                touch.py >= it.y && touch.py <= it.y + it.h) {
                setFocus(i);
                consumed |= KEY_TOUCH;
                if (activateOnTouch) {
                    if (outActivatedId) *outActivatedId = it.id;
                    TT_LOG("padnav: %s activate %d", panelName_, it.id);
                }
                return consumed;
            }
        }
    }

    if (down & KEY_DUP) {
        if (move(0, -1)) consumed |= KEY_DUP;
    }
    if (down & KEY_DDOWN) {
        if (move(0, 1)) consumed |= KEY_DDOWN;
    }
    if (down & KEY_DLEFT) {
        if (move(-1, 0)) consumed |= KEY_DLEFT;
    }
    if (down & KEY_DRIGHT) {
        if (move(1, 0)) consumed |= KEY_DRIGHT;
    }

    if ((down & KEY_A) && hasFocus()) {
        const int id = items_[focusIndex_].id;
        if (outActivatedId) *outActivatedId = id;
        TT_LOG("padnav: %s activate %d", panelName_, id);
        consumed |= KEY_A;
    }

    return consumed;
}

void FocusGroup::drawHighlight(float z) const {
#ifdef __3DS__
    if (!hasFocus()) return;
    const FocusItem& it = items_[focusIndex_];
    const float x = it.x;
    const float y = it.y;
    const float w = it.w;
    const float h = it.h;

    // High contrast highlight:
    // Outer dark frame (1px) provides contrast on light/paper backgrounds.
    // Inner bright gold frame (2px) and warm translucent tint pop on dark backgrounds.
    constexpr u32 kDarkFrame = C2D_Color32(0x10, 0x14, 0x20, 0xFF);
    constexpr u32 kGoldFrame = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
    constexpr u32 kGoldWash  = C2D_Color32(0xF5, 0xB8, 0x1A, 0x38); // ~22% alpha

    // Translucent wash inside
    C2D_DrawRectSolid(x, y, z - 0.01f, w, h, kGoldWash);

    // Outer dark frame (1px)
    C2D_DrawRectSolid(x - 2.0f, y - 2.0f, z, w + 4.0f, 1.0f, kDarkFrame);
    C2D_DrawRectSolid(x - 2.0f, y + h + 1.0f, z, w + 4.0f, 1.0f, kDarkFrame);
    C2D_DrawRectSolid(x - 2.0f, y - 1.0f, z, 1.0f, h + 2.0f, kDarkFrame);
    C2D_DrawRectSolid(x + w + 1.0f, y - 1.0f, z, 1.0f, h + 2.0f, kDarkFrame);

    // Inner gold border (2px)
    C2D_DrawRectSolid(x - 1.0f, y - 1.0f, z + 0.01f, w + 2.0f, 2.0f, kGoldFrame);
    C2D_DrawRectSolid(x - 1.0f, y + h - 1.0f, z + 0.01f, w + 2.0f, 2.0f, kGoldFrame);
    C2D_DrawRectSolid(x - 1.0f, y + 1.0f, z + 0.01f, 2.0f, h - 2.0f, kGoldFrame);
    C2D_DrawRectSolid(x + w - 1.0f, y + 1.0f, z + 0.01f, 2.0f, h - 2.0f, kGoldFrame);
#else
    (void)z;
#endif
}

} // namespace tt::ui
