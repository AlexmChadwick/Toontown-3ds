#include "ui/pad_tables.h"

namespace tt::ui::pad {

void buildBattleInput(FocusGroup& group, int numCogs, const bool* cogActive, const bool* usable, bool canRun) {
    using namespace battle;
    group.rebuild();
    for (int i = 0; i < numCogs && i < kMaxCogs; i++) {
        group.addItem(kIdCog0 + i, kCogX + i * (kCogW + kCogGap), kCogY, kCogW, kCogH, cogActive[i]);
    }
    for (int t = 0; t < kTracks; t++) {
        for (int l = 0; l < kLevels; l++) {
            group.addItem(gagId(t, l), kGridX + l * (kCellW + kCellGap), kGridY + t * kRowH, kCellW, kRowH - 2,
                          usable[t * kLevels + l]);
        }
    }
    group.addItem(kIdPass, kPassX, kBtnY, kBtnW, kBtnH);
    group.addItem(kIdRun, kRunX, kBtnY, kBtnW, kBtnH, canRun);
}

void buildBattleReward(FocusGroup& group) {
    using namespace battle;
    group.rebuild();
    group.addItem(kIdContinue, kContinueX, kBtnY, kContinueW, kBtnH);
}

void buildQuestPages(FocusGroup& group) {
    using namespace quest;
    group.rebuild();
    group.addItem(kIdNext, kCancelX, kCancelY, kCancelW, kCancelH);
}

void buildQuestChoices(FocusGroup& group, int numChoices) {
    using namespace quest;
    group.rebuild();
    for (int i = 0; i < numChoices; i++) {
        group.addItem(i, kCardX, kCardY + i * (kCardH + kCardGap), kScreenW - 2 * kCardX, kCardH);
    }
    group.addItem(kIdCancel, kCancelX, kCancelY, kCancelW, kCancelH);
}

void buildTrackChoices(FocusGroup& group, int numTracks) {
    using namespace quest;
    group.rebuild();
    for (int i = 0; i < numTracks; i++) {
        group.addItem(i, kTrackX + (i % 2) * kTrackColW, kCardY + (i / 2) * kTrackRowH, kTrackW, kTrackH);
    }
    group.addItem(kIdCancel, kCancelX, kCancelY, kCancelW, kCancelH);
}

void buildCloset(FocusGroup& group) {
    using namespace closet;
    group.clear();
    group.addItem(kIdDone, kDoneX, kBtnY, kBtnW, kBtnH);
    group.addItem(kIdCancel, kCancelX, kBtnY, kBtnW, kBtnH);
}

void buildTailor(FocusGroup& group) {
    using namespace tailor;
    group.clear();
    group.addItem(kIdShirt, kRowX, kTopY, kRowW, kRowH);
    group.addItem(kIdShorts, kRowX, kBottomY, kRowW, kRowH);
    group.addItem(kIdDone, kDoneX, kBtnY, kBtnW, kBtnH);
    group.addItem(kIdCancel, kCancelX, kBtnY, kBtnW, kBtnH);
}

void buildCounterButtons(FocusGroup& group) {
    using namespace counter;
    group.clear();
    group.addItem(kIdMinus, kMinusX, kBtnY, kBtnSmallW, kBtnH);
    group.addItem(kIdPlus, kPlusX, kBtnY, kBtnSmallW, kBtnH);
    group.addItem(kIdDone, kDoneX, kBtnY, kDoneW, kBtnH);
    group.addItem(kIdCancel, kCancelX, kBtnY, kCancelW, kBtnH);
}

void buildHoodRow(FocusGroup& group, float homeX) {
    using namespace hoods;
    group.rebuild();
    // The row first: the first D-Down lands on a hood (Home is the row below).
    for (int i = 0; i < kCount; i++) {
        group.addItem(i, i * kHoodW, kRowY, kHoodW, kRowH);
    }
    group.addItem(kIdHome, homeX, kHomeY, kHomeW, kHomeH);
}

void buildToonTab(FocusGroup& group, bool online) {
    using namespace toon;
    group.rebuild();
    if (!online) {
        for (int i = 0; i < 3; i++) {
            group.addItem(kIdTarget0 + i, kTargetX + i * kTargetStep, kTargetY, kTargetW, kTargetH);
        }
        for (int i = 0; i < kNumColors; i++) {
            const float x = kSwX + (i % kSwCols) * (kSwSize + kSwGap);
            const float y = kSwY + (i / kSwCols) * (kSwSize + kSwGap);
            group.addItem(i, x, y, kSwSize, kSwSize);
        }
        return;
    }
    group.addItem(kIdBook, kBookX, kBookY, kBookW, kBookH);
}

void buildBookTab(FocusGroup& group) {
    using namespace booktab;
    group.rebuild();
    for (int i = 0; i < kCount; i++) {
        group.addItem(i, kX0 + (i % kCols) * kStepX, kY0 + (i / kCols) * kStepY, kBtnW, kBtnH);
    }
}

void buildBookOptions(FocusGroup& group) {
    using namespace book;
    // The rectangles PageOptions has always hit-tested for touch.
    constexpr float kToggleX = 15, kToggleW = 135, kRowH = 20;
    constexpr float kMinusX = 80, kPlusX = 126, kStepW = 24;
    group.clear();
    group.addItem(kIdStereo, kToggleX, 30, kToggleW, kRowH);
    group.addItem(kIdLodBias, kToggleX, 54, kToggleW, kRowH);
    group.addItem(kIdMusicMinus, kMinusX, 78, kStepW, kRowH);
    group.addItem(kIdMusicPlus, kPlusX, 78, kStepW, kRowH);
    group.addItem(kIdSfxMinus, kMinusX, 102, kStepW, kRowH);
    group.addItem(kIdSfxPlus, kPlusX, 102, kStepW, kRowH);
    // Developer rows: PageOptions enables them only while the Debug tab is on (SELECT).
    group.addItem(kIdDebugHud, kToggleX, 126, kToggleW, kRowH);
    group.addItem(kIdStressMinus, kMinusX, 150, kStepW, kRowH);
    group.addItem(kIdStressPlus, kPlusX, 150, kStepW, kRowH);
    group.addItem(kIdQuit, kToggleX, 176, kToggleW, kRowH);
}

void buildBookDepartments(FocusGroup& group) {
    using namespace book;
    group.clear();
    for (int d = 0; d < kDepts; d++) {
        group.addItem(d, kDeptX + d * kDeptStep, kDeptY, kDeptW, kDeptH);
    }
}

void buildBookFishArrows(FocusGroup& group) {
    using namespace book;
    group.clear();
    group.addItem(kIdGenusPrev, kGenusPrevX, kGenusY, kGenusW, kGenusH);
    group.addItem(kIdGenusNext, kGenusNextX, kGenusY, kGenusW, kGenusH);
}

void buildBookSosPage(FocusGroup& group) {
    using namespace book;
    group.clear();
    group.addItem(kIdSosPage, kSosPageX, kSosPageY, kSosPageW, kSosPageH);
}

} // namespace tt::ui::pad
