// Groups sheet on the bottom screen. One row, full-width Join / Leave, D-pad and touch.
#include "ui/book_pages/page_groups.h"

#include <cstdio>

#include "core/log.h"
#include "render/ui.h"
#include "ui/theme.h"

namespace tt::ui {

namespace {

constexpr int kIdBoss = 1, kIdBuilding = 2, kIdTask = 3, kIdRow = 10, kIdBack = 40, kIdCell = 20;

const char* actionLabel(const groups::GroupView& g, uint32_t self, bool inGroup) {
    if (g.has(self)) return groups::kLeave;
    if (g.full() || inGroup) return g.full() ? groups::kFull : groups::kJoin;
    return groups::kJoin;
}

bool actionEnabled(const groups::GroupView& g, uint32_t self, bool inGroup) {
    if (g.has(self)) return true;
    if (g.full() || inGroup) return false;
    return true;
}

} // namespace

void PageGroups::shutdown() {
    if (built_) text_.shutdown();
    built_ = false;
}

void PageGroups::enter() {
    picker_ = false;
    row_ = 0;
}

void PageGroups::exit() { picker_ = false; }

void PageGroups::ensure() {
    if (!built_) built_ = text_.init(768);
}

const groups::GroupView* PageGroups::visible(const groups::GroupsState& g) const {
    int order[groups::kMaxGroups];
    const int n = g.rows(order);
    if (n <= 0) return nullptr;
    int row = row_;
    if (row < 0) row = 0;
    if (row >= n) row = n - 1;
    return &g.groups[order[row]];
}

void PageGroups::rebuild(const groups::GroupsState* g) {
    focus_.rebuild();
    if (picker_) {
        const int n = groups::pickCount(pickKind_);
        for (int i = 0; i < n; i++) {
            float x, y;
            groups::pickCell(i, &x, &y);
            focus_.addItem(kIdCell + i, x, y, groups::kPickCellW, groups::kPickCellH);
        }
        focus_.addItem(kIdBack, groups::kPickBackX, groups::kPickBackY, groups::kPickBackW, groups::kPickBackH);
        return;
    }
    const bool canCreate = g && g->managerId != 0 && g->own() == nullptr;
    for (int i = 0; i < 3; i++) {
        const float x = groups::kFilterX + i * (groups::kFilterW + groups::kFilterGap);
        focus_.addItem(kIdBoss + i, x, groups::kFilterY, groups::kFilterW, groups::kFilterH, canCreate);
    }
    if (g && visible(*g)) focus_.addItem(kIdRow, groups::kRowBtnX, groups::kRowBtnY, groups::kRowBtnW, groups::kRowBtnH);
}

void PageGroups::activate(int id, BookDrawContext& bdc) {
    if (!bdc.state || id < 0) return;
    if (id >= kIdBoss && id <= kIdTask) {
        pickKind_ = id - kIdBoss;
        picker_ = true;
        TT_LOG("padnav: groups picker %d", pickKind_);
        return;
    }
    if (id == kIdBack) {
        picker_ = false;
        return;
    }
    if (id >= kIdCell && id < kIdCell + 6) {
        bdc.state->groupsCmd = groups::kCmdCreate;
        bdc.state->groupsKind = uint8_t(pickKind_);
        bdc.state->groupsTarget = uint8_t(id - kIdCell);
        picker_ = false;
        return;
    }
    if (id == kIdRow && bdc.groups) {
        const groups::GroupView* g = visible(*bdc.groups);
        if (!g) return;
        const bool inGroup = bdc.groups->own() != nullptr;
        if (!actionEnabled(*g, bdc.selfId, inGroup)) return;
        if (g->has(bdc.selfId)) bdc.state->groupsCmd = groups::kCmdLeave;
        else {
            bdc.state->groupsCmd = groups::kCmdJoin;
            bdc.state->groupsId = g->id;
        }
    }
}

u32 PageGroups::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    ensure();
    if ((keysDown & KEY_B) && picker_) {
        picker_ = false;
        TT_LOG("padnav: groups activate %d", kIdBack);
        return KEY_B;
    }
    int count = 0;
    if (bdc.groups) {
        int order[groups::kMaxGroups];
        count = bdc.groups->rows(order);
        if (row_ >= count) row_ = count > 0 ? count - 1 : 0;
    }
    if (!picker_ && focus_.focusedId() == kIdRow) {
        if ((keysDown & KEY_DDOWN) && row_ + 1 < count) {
            row_++;
            TT_LOG("padnav: groups focus %d", row_);
            return KEY_DDOWN;
        }
        if ((keysDown & KEY_DUP) && row_ > 0) {
            row_--;
            TT_LOG("padnav: groups focus %d", row_);
            return KEY_DUP;
        }
    }
    rebuild(bdc.groups);
    int activated = -1;
    const u32 used = focus_.handleInput(keysDown, touch, &activated);
    activate(activated, bdc);
    return used | ((keysDown & KEY_B) ? 0 : 0);
}

void PageGroups::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    ensure();
    if (!built_) return;
    const groups::GroupsState* board = bdc.groups;
    groups::GroupsState missing;
    if (!board) board = &missing;
    const char* status = "";
    bool empty = false;
    groups::listMessage(*board, &status, &empty);

    if (picker_) {
        theme::paper(4, 26, 312, 210, 8);
        char title[64];
        std::snprintf(title, sizeof(title), "%s: %s", groups::kCreateTitle,
                      pickKind_ >= 0 && pickKind_ < 3 ? groups::kKindNames[pickKind_] : groups::kPageTitle);
        text_.centered(160, groups::kPickTitleY, theme::kTextSmall, theme::kInk, 300, "%s", title);
        const int n = groups::pickCount(pickKind_);
        for (int i = 0; i < n; i++) {
            float cx, cy;
            groups::pickCell(i, &cx, &cy);
            theme::button(cx, cy, groups::kPickCellW, groups::kPickCellH, theme::kBlue, theme::kBlueDark, false, 8);
            const char* name = pickKind_ == groups::kTask ? groups::kTaskNames[i] : groups::kDepartments[i];
            text_.centered(cx + groups::kPickCellW * 0.5f, cy + 12, theme::kTextSmall, theme::kWhite,
                           groups::kPickCellW - 8, "%s", name);
        }
        theme::button(groups::kPickBackX, groups::kPickBackY, groups::kPickBackW, groups::kPickBackH, theme::kRed,
                      theme::kRedDark, false, 8);
        text_.centered(groups::kPickBackX + groups::kPickBackW * 0.5f, groups::kPickBackY + 12, theme::kTextBody,
                       theme::kWhite, groups::kPickBackW - 8, "%s", groups::kBack);
        focus_.drawHighlight();
        return;
    }

    if (status && status[0]) text_.fit(10, groups::kStatusY, theme::kTextTiny, theme::kInk, 300, "%s", status);
    const bool inGroup = board->own() != nullptr;
    const bool canCreate = board->managerId != 0 && !inGroup;
    for (int i = 0; i < 3; i++) {
        const float fx = groups::kFilterX + i * (groups::kFilterW + groups::kFilterGap);
        const bool on = canCreate;
        theme::button(fx, groups::kFilterY, groups::kFilterW, groups::kFilterH, on ? theme::kBlue : theme::kGrey,
                      on ? theme::kBlueDark : theme::kGreyDark, false, 8);
        text_.centered(fx + groups::kFilterW * 0.5f, groups::kFilterY + 14, theme::kTextSmall,
                       on ? theme::kWhite : theme::kInkSoft, groups::kFilterW - 6, "%s", groups::kKindShort[i]);
    }

    if (empty) {
        text_.wrap(16, 112, theme::kTextSmall, theme::kInkSoft, 288, 18, 3, groups::kEmpty);
    } else if (const groups::GroupView* g = visible(*board)) {
        const bool mine = g->has(board->selfId);
        theme::paper(groups::kRowX, groups::kRowY, groups::kRowW, groups::kRowH, 8, mine ? theme::kYellow : theme::kPaper,
                     theme::kPaperEdge);
        char base[80];
        groups::groupTitle(*g, base, sizeof(base));
        char who[24];
        groups::copyPlainName(who, int(sizeof(who)), g->leaderName);
        const char* suffix = nullptr;
        if (g->flags & groups::kFlagInProgress) suffix = groups::kInProgress;
        else if (who[0]) suffix = who;
        else if (mine) suffix = groups::kMine;
        char title[128];
        if (suffix) std::snprintf(title, sizeof(title), "%s (%s)", base, suffix);
        else std::snprintf(title, sizeof(title), "%s", base);
        text_.fit(16, groups::kRowY + 4, theme::kTextSmall, theme::kInk, 220, "%s", title);
        text_.fit(16, groups::kRowY + 22, theme::kTextTiny, theme::kInkSoft, 200, "%s", groups::placeName(*g));
        text_.right(groups::kRowX + groups::kRowW - 8, groups::kRowY + 4, theme::kTextSmall, theme::kInk, 64, "%d/%d",
                    g->memberCount, g->maxSize);
        const bool enabled = actionEnabled(*g, board->selfId, inGroup);
        const bool leave = g->has(board->selfId);
        const u32 face = !enabled ? theme::kGrey : leave ? theme::kRed : theme::kGreen;
        const u32 lip = !enabled ? theme::kGreyDark : leave ? theme::kRedDark : theme::kGreenDark;
        theme::button(groups::kRowBtnX, groups::kRowBtnY, groups::kRowBtnW, groups::kRowBtnH, face, lip, false, 8);
        text_.centered(groups::kRowBtnX + groups::kRowBtnW * 0.5f, groups::kRowBtnY + 14, theme::kTextBody,
                       enabled ? theme::kWhite : theme::kInkSoft, groups::kRowBtnW - 12, "%s",
                       actionLabel(*g, board->selfId, inGroup));
    }
    focus_.drawHighlight();
}

} // namespace tt::ui
