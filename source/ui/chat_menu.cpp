#include "render/ui_font.h"
#include "ui/chat_menu.h"

#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "ui/theme.h"

namespace tt::ui {

namespace {

constexpr float kZ = 0.5f;
constexpr float kMarginX = 6.0f;
constexpr float kRowGap = 2.0f;
constexpr float kArrowW = 18.0f;
constexpr float kScrollBarW = 4.0f;
constexpr float kBackW = 64.0f;
constexpr float kLabelScale = 0.5f, kMinLabelScale = 0.4f;

// SpeedChat's own look (SCColorScheme defaults, ui/theme.h): pale frames, black text, a green
// rollover and blue submenu arrows.
const u32 kRow = theme::kScFrame;
const u32 kRowEdge = theme::kScFrameEdge;
const u32 kFocus = theme::kScRollover;
const u32 kPressed = theme::kScPressed;
const u32 kText = theme::kScText;
const u32 kTextLocked = C2D_Color32(0x66, 0x66, 0x66, 0xFF); // textDisabledColor 0.4
const u32 kTitle = theme::kBlueDark;

float rowTextW(bool isMenu) { return ChatMenu::kW - 2 * kMarginX - 12.0f - kScrollBarW - (isMenu ? kArrowW : 0.0f); }

} // namespace

bool ChatMenu::init() {
    setEmoteAccess(nullptr);
    // Breadcrumb + every label of the largest menu, plus slack for wrapping.
    // (MY TASKS: up to kMaxTaskPhrases sentences of about 60 glyphs.)
    constexpr int kTaskGlyphs = kMaxTaskPhrases * 64;
    menuBuf_ = C2D_TextBufNew((sc::kMaxMenuGlyphs > kTaskGlyphs ? sc::kMaxMenuGlyphs : kTaskGlyphs) + 96);
    staticBuf_ = C2D_TextBufNew(96);
    if (!menuBuf_ || !staticBuf_) return false;
    tt::render::textParse(&back_, staticBuf_, "< Back");
    tt::render::textParse(&arrow_, staticBuf_, ">");
    tt::render::textParse(&hint_, staticBuf_, "Up/Down + A");
    C2D_TextOptimize(&back_);
    C2D_TextOptimize(&arrow_);
    C2D_TextOptimize(&hint_);
    for (int m = 0; m < sc::kNumMenus; m++) {
        if (!strcmp(sc::kMenus[m].title, "TOONTASKS")) toontasksMenu_ = m;
    }
    setTasks(nullptr);
    enter(0, -1);
    return true;
}

void ChatMenu::shutdown() {
    if (menuBuf_) C2D_TextBufDelete(menuBuf_);
    if (staticBuf_) C2D_TextBufDelete(staticBuf_);
    menuBuf_ = staticBuf_ = nullptr;
}

int ChatMenu::numRows(int menu) const {
    if (menu == kMyTasksMenu) return numTasks_;
    return sc::kMenus[menu].numItems + (menu == toontasksMenu_ ? 1 : 0);
}

ChatMenu::Row ChatMenu::row(int menu, int i) const {
    if (menu == kMyTasksMenu) {
        const TaskPhrase& t = tasks_[i];
        return {t.text, t.staticNeedTask ? 1299 : -1, -1, false, i};
    }
    if (menu == toontasksMenu_) {
        if (i == 0) return {"MY TASKS", kMyTasksMenu, -1, true, -1}; // OTPLocalizer.SCMenuToontasksMyTasks
        i--;
    }
    const sc::Item& it = sc::kItems[sc::kMenus[menu].firstItem + i];
    return {it.label, it.value, it.emote, it.isMenu != 0, -1};
}

int ChatMenu::parentOf(int menu) const { return menu == kMyTasksMenu ? toontasksMenu_ : sc::kMenus[menu].parent; }

const char* ChatMenu::titleOf(int menu) const { return menu == kMyTasksMenu ? "MY TASKS" : sc::kMenus[menu].title; }

// TTSCToontaskMenu.__tasksChanged: every phrase of every task we carry (duplicates dropped), then
// "I need to get a ToonTask." while we carry fewer than our limit.
void ChatMenu::setTasks(const net::OwnQuests* quests) {
    if (quests && tasksKnown_ && quests->version == tasksVersion_) return;
    if (!quests && tasksKnown_ == false && numTasks_ > 0) return;
    tasksKnown_ = quests != nullptr;
    tasksVersion_ = quests ? quests->version : 0;
    numTasks_ = 0;
    auto add = [&](const char* text) -> TaskPhrase* {
        for (int i = 0; i < numTasks_; i++) {
            if (!strcmp(tasks_[i].text, text)) return nullptr;
        }
        if (numTasks_ >= kMaxTaskPhrases) return nullptr;
        TaskPhrase& t = tasks_[numTasks_++];
        t = TaskPhrase{};
        snprintf(t.text, sizeof t.text, "%.*s", int(sizeof t.text) - 1, text);
        return &t;
    };
    for (int i = 0; quests && i < quests->count; i++) {
        const net::QuestEntry& e = quests->quests[i];
        const quest::QuestDef* q = e.questId <= 0xFFFF ? quest::findQuest(uint16_t(e.questId)) : nullptr;
        if (!q) continue;
        char msgs[quest::kMaxScStrings][quest::kScLen];
        const int n = quest::scStrings(*q, uint16_t(e.toNpc), e.progress, msgs, quest::kMaxScStrings);
        for (int m = 0; m < n; m++) {
            if (TaskPhrase* t = add(msgs[m])) {
                t->taskId = e.questId;
                t->toNpc = e.toNpc;
                t->progress = e.progress;
                t->msgIndex = uint8_t(m);
            }
        }
    }
    if (!quests || quests->count != quests->carryLimit) {
        if (const char* need = sc::phraseText(1299)) {
            if (TaskPhrase* t = add(need)) t->staticNeedTask = true;
        }
    }
    if (menu_ == kMyTasksMenu && menuBuf_) enter(kMyTasksMenu, focus_ >= numTasks_ ? numTasks_ - 1 : focus_);
}

const ChatMenu::TaskPhrase* ChatMenu::taskPhrase(int code) const {
    const int slot = code - kTaskCode;
    return slot >= 0 && slot < kQueueLen ? &queuedTasks_[slot] : nullptr;
}

void ChatMenu::enter(int menu, int focusItem) {
    menu_ = menu;
    C2D_TextBufClear(menuBuf_);

    // Breadcrumb: "PLACES > COGS" (the root just says SpeedChat).
    char crumb[96];
    const int parent = parentOf(menu);
    if (parent < 0 || parentOf(parent) < 0) {
        snprintf(crumb, sizeof(crumb), "%s", titleOf(menu));
    } else {
        snprintf(crumb, sizeof(crumb), "%s > %s", titleOf(parent), titleOf(menu));
    }
    tt::render::textParse(&title_, menuBuf_, crumb);
    C2D_TextOptimize(&title_);
    float tw, th;
    C2D_TextGetDimensions(&title_, 0.5f, 0.5f, &tw, &th);
    const float titleAvail = kW - kBackW - 2 * kMarginX - 8.0f;
    titleScale_ = tw > titleAvail ? 0.5f * titleAvail / tw : 0.5f;

    const int n = numRows(menu);
    for (int i = 0; i < n && i < kMaxRows; i++) {
        const Row it = row(menu, i);
        tt::render::textParse(&labels_[i], menuBuf_, it.label);
        C2D_TextOptimize(&labels_[i]);
        float w, h;
        C2D_TextGetDimensions(&labels_[i], kLabelScale, kLabelScale, &w, &h);
        const float avail = rowTextW(it.isMenu);
        float s = kLabelScale;
        if (w > avail) s = kLabelScale * avail / w;
        labelWrap_[i] = s < kMinLabelScale;
        if (labelWrap_[i]) s = kMinLabelScale + 0.02f; // two lines at 0.42 fit a 30 px row
        labelScale_[i] = s;
        C2D_TextGetDimensions(&labels_[i], s, s, &w, &h);
        labelH_[i] = labelWrap_[i] ? h * 2.0f : h;
    }
    scroll_ = 0.0f;
    focus_ = focusItem;
    pressedRow_ = -1;
    touching_ = dragged_ = pressedBack_ = false;
}

void ChatMenu::blur() {
    focus_ = -1;
    pressedRow_ = -1;
    touching_ = dragged_ = pressedBack_ = false;
}

float ChatMenu::maxScroll(float listH) const {
    const float content = numRows(menu_) * kRowH;
    return content > listH ? content - listH : 0.0f;
}

void ChatMenu::ensureVisible(int item, float listH) {
    const float top = item * kRowH, bottom = top + kRowH;
    if (top < scroll_) scroll_ = top;
    if (bottom > scroll_ + listH) scroll_ = bottom - listH;
}

int ChatMenu::rowAt(float y, float listH) const {
    if (y < kHeaderH || y >= kHeaderH + listH) return -1;
    const int row = static_cast<int>((y - kHeaderH + scroll_) / kRowH);
    return row < numRows(menu_) ? row : -1;
}

void ChatMenu::activate(int item) {
    if (item < 0 || item >= numRows(menu_)) return;
    const Row it = row(menu_, item);
    if (it.isMenu) {
        enter(it.value, focus_ >= 0 ? 0 : -1);
        return;
    }
    if (qCount_ < kQueueLen) {
        const int slot = (qHead_ + qCount_) % kQueueLen;
        if (it.task >= 0 && !tasks_[it.task].staticNeedTask) {
            queuedTasks_[slot] = tasks_[it.task];
            queue_[slot] = kTaskCode + slot;
            qCount_++;
            enter(0, -1);
            return;
        }
        if (it.value==65535) {
            if (it.emote<0 || it.emote>=25 || !emoteAccess_[it.emote]) return;
            queue_[slot]=-1-it.emote;
        } else queue_[slot] = it.value;
        qCount_++;
        if (it.value!=65535 && it.emote>=0 && it.emote<25 && emoteAccess_[it.emote] && qCount_<kQueueLen) {
            queue_[(qHead_+qCount_)%kQueueLen]=-1-it.emote; ++qCount_;
        }
    }
    enter(0, -1);
}

void ChatMenu::goBack(float listH) {
    const int parent = parentOf(menu_);
    if (parent < 0) {
        focus_ = -1;
        return;
    }
    // Re-highlight the sub-menu we came from (only in D-pad mode).
    int from = -1;
    for (int i = 0; i < numRows(parent); i++) {
        const Row it = row(parent, i);
        if (it.isMenu && it.value == menu_) from = i;
    }
    const bool dpad = focus_ >= 0;
    enter(parent, dpad ? from : -1);
    if (from >= 0) ensureVisible(from, listH);
}

bool ChatMenu::popSpeech(int& msgIndex) {
    if (qCount_ == 0) return false;
    msgIndex = queue_[qHead_];
    qHead_ = (qHead_ + 1) % kQueueLen;
    qCount_--;
    return true;
}

u32 ChatMenu::handleInput(u32 down, u32 held, const touchPosition& t, float panelBottom) {
    const float listH = panelBottom - kHeaderH;
    const int n = numRows(menu_);
    u32 consumed = 0;

    // Buttons.
    if (down & (KEY_DUP | KEY_DDOWN)) {
        if (focus_ < 0) {
            focus_ = rowAt(kHeaderH + 1.0f, listH); // top visible row
            if (focus_ < 0) focus_ = 0;
        } else if (n <= 0) {
            focus_ = -1;
        } else if (down & KEY_DUP) {
            focus_ = focus_ > 0 ? focus_ - 1 : n - 1;
        } else {
            focus_ = focus_ < n - 1 ? focus_ + 1 : 0;
        }
        ensureVisible(focus_, listH);
        TT_LOG("padnav: chat focus %d", focus_);
        consumed |= down & (KEY_DUP | KEY_DDOWN);
    } else if ((down & KEY_A) && focus_ >= 0) {
        TT_LOG("padnav: chat activate %d", focus_);
        activate(focus_);
        consumed |= KEY_A;
    } else if ((down & KEY_B) && focus_ >= 0) {
        TT_LOG("padnav: chat back");
        goBack(listH);
        consumed |= KEY_B;
    }

    // Touch: press -> track -> release. Taps fire on release so drags can scroll.
    const bool touchHeld = (held & KEY_TOUCH) != 0;
    if ((down & KEY_TOUCH) && t.py < panelBottom) {
        touching_ = true;
        dragged_ = false;
        touchStartX_ = t.px;
        touchStartY_ = t.py;
        scrollStart_ = scroll_;
        pressedBack_ = parentOf(menu_) >= 0 && t.py < kHeaderH && t.px < kBackW + kMarginX;
        pressedRow_ = rowAt(t.py, listH);
    } else if (touching_ && touchHeld) {
        const float dy = t.py - touchStartY_, dx = t.px - touchStartX_;
        if (!dragged_ && (dy * dy + dx * dx) > kTapSlop * kTapSlop) {
            dragged_ = true;
            pressedBack_ = false;
            pressedRow_ = -1;
        }
        if (dragged_) {
            scroll_ = scrollStart_ - dy;
            const float ms = maxScroll(listH);
            if (scroll_ < 0) scroll_ = 0;
            if (scroll_ > ms) scroll_ = ms;
        }
    }
    if (touching_ && !touchHeld) { // released (same frame as the press for scripted taps)
        touching_ = false;
        if (!dragged_) {
            if (pressedBack_) {
                focus_ = -1;
                goBack(listH);
            } else if (pressedRow_ >= 0) {
                focus_ = -1; // touch use hides the D-pad highlight
                activate(pressedRow_);
            }
        }
        pressedRow_ = -1;
        pressedBack_ = false;
    }
    return consumed;
}

void ChatMenu::draw(float panelBottom) {
    const float listH = panelBottom - kHeaderH;
    const int numItems = numRows(menu_);

    // Rows (drawn first; the header is drawn over rows scrolled above it, the tab bar
    // over rows below).
    const int firstRow = static_cast<int>(scroll_ / kRowH);
    const int lastRow = static_cast<int>((scroll_ + listH) / kRowH);
    const float rowX = kMarginX, rowW = kW - 2 * kMarginX - kScrollBarW - 4.0f;
    for (int i = firstRow; i <= lastRow && i < numItems && i < kMaxRows; i++) {
        const Row it = row(menu_, i);
        const float y = kHeaderH + i * kRowH - scroll_;
        const bool focused = i == focus_;
        const bool pressed = i == pressedRow_;
        theme::roundRect(rowX, y + kRowGap * 0.5f, rowW, kRowH - kRowGap, 5, kRowEdge, kZ);
        theme::roundRect(rowX + 1, y + kRowGap * 0.5f + 1, rowW - 2, kRowH - kRowGap - 2, 4,
                         focused ? kFocus : pressed ? kPressed : kRow, kZ);
        const bool locked = it.value==65535 && it.emote>=0 && !emoteAccess_[it.emote];
        const u32 fg = locked ? kTextLocked : kText;
        const float s = labelScale_[i];
        const float ty = y + (kRowH - labelH_[i]) * 0.5f;
        if (labelWrap_[i]) {
            C2D_DrawText(&labels_[i], C2D_WithColor | C2D_WordWrap, rowX + 8.0f, ty, kZ, s, s, fg,
                         rowTextW(it.isMenu));
        } else {
            C2D_DrawText(&labels_[i], C2D_WithColor, rowX + 8.0f, ty, kZ, s, s, fg);
        }
        if (it.isMenu) { // SCMenuHolder's arrow: a solid triangle pointing at the submenu
            const float ax = rowX + rowW - kArrowW + 4.0f, ay = y + kRowH * 0.5f;
            C2D_DrawTriangle(ax, ay - 6, theme::kScArrow, ax, ay + 6, theme::kScArrow, ax + 9, ay, theme::kScArrow, kZ);
        }
    }

    // Scroll indicator.
    const float ms = maxScroll(listH);
    if (ms > 0.0f) {
        const float content = numItems * kRowH;
        const float barH = listH * listH / content;
        const float barY = kHeaderH + (listH - barH) * (scroll_ / ms);
        theme::roundRect(kW - kMarginX - kScrollBarW, kHeaderH, kScrollBarW, listH, 2, kRow, kZ);
        theme::roundRect(kW - kMarginX - kScrollBarW, barY, kScrollBarW, barH, 2, theme::kScArrow, kZ);
    }

    // Header.
    theme::skyBand(0, kHeaderH, kW, panelBottom, kZ);
    float titleX = kMarginX + 2.0f;
    if (parentOf(menu_) >= 0) {
        theme::button(kMarginX, 1, kBackW, kHeaderH - 2, pressedBack_ ? kPressed : kRow, kRowEdge, pressedBack_, 6.0f,
                      kZ);
        C2D_DrawText(&back_, C2D_WithColor, kMarginX + 9.0f, pressedBack_ ? 7.0f : 4.0f, kZ, 0.5f, 0.5f, kText);
        titleX = kMarginX + kBackW + 8.0f;
    } else if (focus_ < 0) {
        C2D_DrawText(&hint_, C2D_WithColor, kW - 92.0f, 7.0f, kZ, 0.42f, 0.42f, kTitle);
    }
    C2D_DrawText(&title_, C2D_WithColor, titleX, 4.0f + (0.5f - titleScale_) * 14.0f, kZ, titleScale_, titleScale_,
                 kTitle);
}

} // namespace tt::ui
