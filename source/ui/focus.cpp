#include "ui/focus.h"
#include "core/log.h"

#ifdef __3DS__
#include <citro2d.h>

#include "ui/theme.h"
#endif

#include <cmath>
#include <cstdio>

namespace tt::ui {

namespace {

// Up / Down: candidates that share the current column win over any candidate outside it
// (the band penalty is larger than the screen). Left / Right only consider the current row.
constexpr float kOutOfBandPenalty = 400.0f;
constexpr float kInBandPerpWeight = 0.5f;
constexpr float kOutOfBandPerpWeight = 4.0f;
// Centres closer than this along the pressed axis are not "in that direction".
constexpr float kMinStep = 1.0f;
constexpr float kNoScore = 1e9f;

float directionScore(float mainDelta, float perpDelta, bool sameBand) {
    if (sameBand) return mainDelta + perpDelta * kInBandPerpWeight;
    return kOutOfBandPenalty + mainDelta + perpDelta * kOutOfBandPerpWeight;
}

float centreX(const FocusItem& it) {
    return it.x + it.w * 0.5f;
}

float centreY(const FocusItem& it) {
    return it.y + it.h * 0.5f;
}

} // namespace

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
    keepId_ = -1;
}

void FocusGroup::rebuild() {
    keepId_ = focusedId();
    itemCount_ = 0;
    focusIndex_ = -1;
}

int FocusGroup::addItem(int id, float x, float y, float w, float h, bool enabled) {
    if (itemCount_ >= kMaxItems) return -1;
    const int index = itemCount_;
    itemCount_++;
    FocusItem& it = items_[index];
    it.id = id;
    it.x = x;
    it.y = y;
    it.w = w;
    it.h = h;
    it.enabled = enabled;
    it.radius = 0.0f;
    if (keepId_ >= 0 && id == keepId_ && enabled && focusIndex_ < 0) {
        focusIndex_ = index; // the same control as last frame: no log line
    }
    return index;
}

void FocusGroup::setItemEnabled(int index, bool enabled) {
    if (index < 0 || index >= itemCount_) return;
    items_[index].enabled = enabled;
    if (!enabled && focusIndex_ == index) blur();
}

void FocusGroup::setItemRect(int index, float x, float y, float w, float h) {
    if (index < 0 || index >= itemCount_) return;
    items_[index].x = x;
    items_[index].y = y;
    items_[index].w = w;
    items_[index].h = h;
}

int FocusGroup::focusedId() const {
    return hasFocus() ? items_[focusIndex_].id : -1;
}

bool FocusGroup::hasFocus() const {
    return focusIndex_ >= 0 && focusIndex_ < itemCount_ && items_[focusIndex_].enabled;
}

int FocusGroup::itemAt(float px, float py) const {
    for (int i = 0; i < itemCount_; ++i) {
        const FocusItem& it = items_[i];
        if (!it.enabled) continue;
        if (px >= it.x && px < it.x + it.w && py >= it.y && py < it.y + it.h) return i;
    }
    return -1;
}

int FocusGroup::indexOf(int id) const {
    for (int i = 0; i < itemCount_; ++i) {
        if (items_[i].id == id) return i;
    }
    return -1;
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
    const int index = indexOf(id);
    return index >= 0 && setFocus(index);
}

void FocusGroup::blur() {
    if (focusIndex_ >= 0) TT_LOG("padnav: %s blur", panelName_);
    focusIndex_ = -1;
    keepId_ = -1;
}

bool FocusGroup::move(int dx, int dy) {
    if (itemCount_ <= 0) return false;

    if (!hasFocus()) {
        for (int i = 0; i < itemCount_; ++i) {
            if (items_[i].enabled) return setFocus(i);
        }
        return false;
    }

    const FocusItem& cur = items_[focusIndex_];
    const float curX = centreX(cur);
    const float curY = centreY(cur);
    int bestIndex = -1;
    float bestScore = kNoScore;

    // Pass 0: the nearest item strictly in the pressed direction.
    // Pass 1 (only when pass 0 found none): wrap to the far edge, same row/column first.
    for (int pass = 0; pass < 2 && bestIndex < 0; ++pass) {
        const bool wrap = pass == 1;
        for (int i = 0; i < itemCount_; ++i) {
            if (i == focusIndex_ || !items_[i].enabled) continue;
            const FocusItem& cand = items_[i];
            const float delX = centreX(cand) - curX;
            const float delY = centreY(cand) - curY;
            const bool sameRow = std::fabs(delY) <= (cur.h + cand.h) * 0.5f;
            const bool sameCol = std::fabs(delX) <= (cur.w + cand.w) * 0.5f;
            float score = kNoScore;
            if (dx != 0) {
                // Left / Right stay on the current row: past its end they wrap to its other end.
                const float along = delX * static_cast<float>(dx);
                if (!sameRow) {
                    continue;
                } else if (!wrap && along > kMinStep) {
                    score = directionScore(along, std::fabs(delY), true);
                } else if (wrap) {
                    score = directionScore(centreX(cand) * static_cast<float>(dx), std::fabs(delY), true);
                }
            } else if (dy != 0) {
                // Up / Down prefer the same column, then the nearest control above / below
                // (e.g. from the gag grid down to Pass), and only then wrap.
                const float along = delY * static_cast<float>(dy);
                if (!wrap && along > kMinStep) {
                    score = directionScore(along, std::fabs(delX), sameCol);
                } else if (wrap) {
                    score = directionScore(centreY(cand) * static_cast<float>(dy), std::fabs(delX), sameCol);
                }
            }
            if (score < bestScore) {
                bestScore = score;
                bestIndex = i;
            }
        }
    }

    return bestIndex >= 0 && setFocus(bestIndex);
}

bool FocusGroup::moveLinear(int step) {
    if (itemCount_ <= 0 || step == 0) return false;
    int index = focusIndex_ < 0 ? (step > 0 ? -1 : 0) : focusIndex_;
    for (int tries = 0; tries < itemCount_; ++tries) {
        index = (index + step) % itemCount_;
        if (index < 0) index += itemCount_;
        if (items_[index].enabled) return setFocus(index);
    }
    return false;
}

u32 FocusGroup::handleInput(u32 down, const touchPosition& touch, int* outActivatedId, bool activateOnTouch) {
    u32 consumed = 0;
    if (outActivatedId) *outActivatedId = -1;

    if (down & KEY_TOUCH) {
        const int index = itemAt(touch.px, touch.py);
        if (index >= 0) {
            setFocus(index);
            consumed |= KEY_TOUCH;
            if (activateOnTouch) {
                if (outActivatedId) *outActivatedId = items_[index].id;
                logActivate(items_[index].id);
            }
            return consumed;
        }
    }

    if ((down & KEY_DUP) && move(0, -1)) consumed |= KEY_DUP;
    if ((down & KEY_DDOWN) && move(0, 1)) consumed |= KEY_DDOWN;
    if ((down & KEY_DLEFT) && move(-1, 0)) consumed |= KEY_DLEFT;
    if ((down & KEY_DRIGHT) && move(1, 0)) consumed |= KEY_DRIGHT;

    if ((down & KEY_A) && hasFocus()) {
        const int id = items_[focusIndex_].id;
        if (outActivatedId) *outActivatedId = id;
        logActivate(id);
        consumed |= KEY_A;
    }
    return consumed;
}

void FocusGroup::setRadius(int index, float radius) {
    if (index >= 0 && index < itemCount_) items_[index].radius = radius;
}

void FocusGroup::logActivate(int id) const {
    TT_LOG("padnav: %s activate %d", panelName_, id);
}

void FocusGroup::drawHighlight(float z) const {
#ifdef __3DS__
    if (!hasFocus()) return;
    const FocusItem& it = items_[focusIndex_];
    const float x = it.x;
    const float y = it.y;
    const float w = it.w;
    const float h = it.h;

    // A dark 1 px outer frame shows on the light book paper; the 2 px gold inner frame
    // and the translucent wash show on the dark navy panels.
    constexpr u32 kDarkFrame = C2D_Color32(0x10, 0x14, 0x20, 0xFF);
    constexpr u32 kGoldFrame = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
    constexpr u32 kGoldWash = C2D_Color32(0xF5, 0xB8, 0x1A, 0x38); // about 22% alpha
    constexpr float kLayerStep = 0.01f;

    if (it.radius > 0.0f) { // the same frames, following the control's rounded outline
        theme::roundRect(x, y, w, h, it.radius, kGoldWash, z - kLayerStep);
        theme::roundRing(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, it.radius + 2.0f, 1.0f, kDarkFrame, z);
        theme::roundRing(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, it.radius + 1.0f, 2.0f, kGoldFrame, z + kLayerStep);
        return;
    }
    C2D_DrawRectSolid(x, y, z - kLayerStep, w, h, kGoldWash);

    C2D_DrawRectSolid(x - 2.0f, y - 2.0f, z, w + 4.0f, 1.0f, kDarkFrame);
    C2D_DrawRectSolid(x - 2.0f, y + h + 1.0f, z, w + 4.0f, 1.0f, kDarkFrame);
    C2D_DrawRectSolid(x - 2.0f, y - 1.0f, z, 1.0f, h + 2.0f, kDarkFrame);
    C2D_DrawRectSolid(x + w + 1.0f, y - 1.0f, z, 1.0f, h + 2.0f, kDarkFrame);

    C2D_DrawRectSolid(x - 1.0f, y - 1.0f, z + kLayerStep, w + 2.0f, 2.0f, kGoldFrame);
    C2D_DrawRectSolid(x - 1.0f, y + h - 1.0f, z + kLayerStep, w + 2.0f, 2.0f, kGoldFrame);
    C2D_DrawRectSolid(x - 1.0f, y + 1.0f, z + kLayerStep, 2.0f, h - 2.0f, kGoldFrame);
    C2D_DrawRectSolid(x + w - 1.0f, y + 1.0f, z + kLayerStep, 2.0f, h - 2.0f, kGoldFrame);
#else
    (void)z;
#endif
}

} // namespace tt::ui
