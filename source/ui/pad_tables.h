// Focus tables: the button layout of every bottom-screen panel that the D-Pad can reach
// (docs/design/pad_navigation.md). Each builder fills a FocusGroup with the panel's
// controls, using the same rectangles the panel draws and (where noted) hit-tests for
// touch. Plain C++ with no citro2d, so tools/padnav_test checks every table on the host.
#pragma once

#include "ui/focus.h"

namespace tt::ui::pad {

constexpr float kScreenW = 320.0f;

// ---- Street battle (ui/battle_panel.cpp): cogs, the gag grid, Pass / Run, Continue ----
namespace battle {
constexpr int kMaxCogs = 4;
constexpr int kTracks = 7;
constexpr int kLevels = 7;
constexpr float kCogX = 2, kCogY = 18, kCogH = 44, kCogW = 78, kCogGap = 1;
constexpr float kGridX = 60, kGridY = 66, kRowH = 20, kCellW = 35, kCellGap = 2;
constexpr float kBtnY = 210, kBtnH = 28, kBtnW = 76;
constexpr float kFireX = 2, kSosX = 82, kPassX = 162, kRunX = 242, kContinueX = 218;
// Gag ids are track * 10 + level (0..66); the rest sit above them.
constexpr int kIdCog0 = 100; // + cog index
constexpr int kIdPass = 200;
constexpr int kIdRun = 201;
constexpr int kIdContinue = 202;
constexpr int kIdFire = 203;
constexpr int kIdSos = 204;
constexpr int gagId(int track, int level) { return track * 10 + level; }
constexpr bool isGagId(int id) { return id >= 0 && id < kIdCog0; }
} // namespace battle
// usable[track * kLevels + level]: the gag can be chosen now.
void buildBattleInput(FocusGroup& group, int numCogs, const bool* cogActive, const bool* usable, bool canRun,
                      bool canFire = false, bool canSos = false);
void buildBattleReward(FocusGroup& group);

// ---- NPC quest dialog (ui/quest_panel.cpp): quest cards, track cards, Cancel ----
namespace quest {
constexpr float kCardX = 4, kCardY = 26, kCardH = 58, kCardGap = 4;
constexpr float kTrackX = 4, kTrackColW = 158, kTrackW = 154, kTrackRowH = 40, kTrackH = 36;
constexpr float kCancelX = 110, kCancelY = 210, kCancelW = 100, kCancelH = 26;
constexpr int kIdNext = 98;   // the dialog pages' Next / Done button (same place as Cancel)
constexpr int kIdCancel = 99; // choices are 0..n-1
} // namespace quest
void buildQuestPages(FocusGroup& group);
void buildQuestChoices(FocusGroup& group, int numChoices);
void buildTrackChoices(FocusGroup& group, int numTracks);

// ---- Closet (ui/closet_panel.cpp): Done / Cancel ----
namespace closet {
constexpr float kBtnY = 196, kBtnH = 36, kBtnW = 76, kDoneX = 160, kCancelX = 240;
constexpr int kIdDone = 1;
constexpr int kIdCancel = 2;
} // namespace closet
void buildCloset(FocusGroup& group);

// ---- Tailor (ui/tailor_panel.cpp): shirt row, shorts row, Done / Cancel ----
namespace tailor {
constexpr float kTopY = 38, kBottomY = 94, kRowX = 4, kRowW = 312, kRowH = 48;
constexpr float kBtnY = 196, kBtnH = 36, kBtnW = 76, kDoneX = 160, kCancelX = 240;
constexpr int kIdShirt = 0;
constexpr int kIdShorts = 1;
constexpr int kIdDone = 2;
constexpr int kIdCancel = 3;
} // namespace tailor
void buildTailor(FocusGroup& group);

// ---- Gag Shop and minigame gag purchase (shop_panel.cpp, minigame_purchase_panel.cpp):
// the button row under the gag grid. The grid keeps its own D-Pad selection. ----
namespace counter {
constexpr float kBtnY = 202, kBtnH = 34;
constexpr float kMinusX = 4, kPlusX = 52, kBtnSmallW = 44;
constexpr float kDoneX = 176, kDoneW = 70, kCancelX = 250, kCancelW = 66;
constexpr int kIdMinus = 0;
constexpr int kIdPlus = 1;
constexpr int kIdDone = 2;
constexpr int kIdCancel = 3;
} // namespace counter
void buildCounterButtons(FocusGroup& group);

// ---- Map tab (render/ui.cpp) and the book's Map page: Home and the teleport row ----
namespace hoods {
constexpr int kCount = 8; // render/ui.h kNumTeleportHoods
// Leave the lower-left map corner to the Laff meter. Home remains directly
// above the playground row and keeps its original 76 x 24 touch target.
constexpr float kHomeX = 82, kHomeY = 148, kHomeW = 76, kHomeH = 24;
constexpr float kRowY = 178, kRowH = 26, kHoodW = kScreenW / kCount;
constexpr int kIdHome = 100; // hoods are 0..kCount-1
} // namespace hoods
// Hood buttons span the whole row so a touch anywhere in a column hits it (as before).
void buildHoodRow(FocusGroup& group);

// ---- Toon tab (render/ui.cpp) ----
namespace toon {
constexpr int kNumColors = 27; // render/ui.h kNumToonColors
constexpr int kSwCols = 9;
constexpr float kSwX = 8, kSwY = 44, kSwSize = 30, kSwGap = 4;
constexpr float kTargetX = 8, kTargetY = 8, kTargetStep = 102, kTargetW = 98, kTargetH = 28;
constexpr float kBookX = 60, kBookY = 175, kBookW = 200, kBookH = 24;
constexpr int kIdTarget0 = 300; // + 0 body, 1 shirt, 2 shorts; swatches are 0..26
constexpr int kIdBook = 200;
} // namespace toon
// Online the tab shows ToonTasks and only the book button; offline the colour picker.
void buildToonTab(FocusGroup& group, bool online);

// ---- Setup tab (render/ui.cpp) ----
namespace setup {
constexpr float kBtnX = 12, kBtnW = 296, kBtnH = 36, kStereoY = 30, kBookY = 82;
constexpr int kIdStereo = 0;
constexpr int kIdBook = 1;
} // namespace setup
void buildSetupTab(FocusGroup& group);

// ---- Shtiker Book pages (ui/book_pages/*). The page area starts at (2, 24). ----
namespace book {
// Options page: ids 1..10, matching PageOptions::handleInput.
constexpr int kIdStereo = 1;
constexpr int kIdDebugHud = 2;
constexpr int kIdLodBias = 3;
constexpr int kIdMusicMinus = 4;
constexpr int kIdMusicPlus = 5;
constexpr int kIdSfxMinus = 6;
constexpr int kIdSfxPlus = 7;
constexpr int kIdStressMinus = 8;
constexpr int kIdStressPlus = 9;
constexpr int kIdQuit = 10;
// Cogs / Disguise department tabs as drawn (ids 0..3 = Bossbot, Lawbot, Cashbot, Sellbot).
constexpr int kDepts = 4;
constexpr float kDeptX = 8, kDeptY = 26, kDeptStep = 70, kDeptW = 66, kDeptH = 16;
// Fish page genus arrows as drawn.
constexpr float kGenusY = 44, kGenusW = 16, kGenusH = 14, kGenusPrevX = 12, kGenusNextX = 284;
constexpr int kIdGenusPrev = 0;
constexpr int kIdGenusNext = 1;
// SOS page "[Page n/2]" label as drawn.
constexpr float kSosPageX = 246, kSosPageY = 25, kSosPageW = 66, kSosPageH = 14;
constexpr int kIdSosPage = 0;
} // namespace book
void buildBookOptions(FocusGroup& group);
void buildBookDepartments(FocusGroup& group);
void buildBookFishArrows(FocusGroup& group);
void buildBookSosPage(FocusGroup& group);

} // namespace tt::ui::pad
