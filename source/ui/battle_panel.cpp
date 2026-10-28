#include "ui/panel_geometry.h"
#include "ui/battle_panel.h"

#include <cstdarg>
#include <cstdio>

#include "ui/gag_data.h"
#include "ui/pad_tables.h"
#include "core/log.h"
#include "world/battle_fx.h"
#include "ui/theme.h"

namespace tt::ui {

namespace {

constexpr float kW = 320.0f;
// The layout is shared with the focus table (ui/pad_tables.h) so touch, D-Pad and drawing agree.
constexpr float kCogY = pad::battle::kCogY, kCogH = pad::battle::kCogH, kCogW = pad::battle::kCogW;
constexpr float kGridY = pad::battle::kGridY, kRowH = pad::battle::kRowH;
constexpr float kGridX = pad::battle::kGridX, kCellW = pad::battle::kCellW;
constexpr float kBtnY = pad::battle::kBtnY, kBtnH = pad::battle::kBtnH;
static_assert(pad::battle::kTracks == gags::kTracks && pad::battle::kLevels == gags::kLevels,
              "the battle focus table assumes the 7 x 7 gag grid");

const u32 kInk = theme::kInk;
const u32 kInkSoft = theme::kInkSoft;
// Suit.healthColors by Suit.updateHealthBar's conditions (green > 95%, yellow > 70%, orange
// > 30%, red > 5%, else the blinking red / grey).
u32 healthColor(float f) {
    if (f > 0.95f) return theme::rgb(0x20, 0xE0, 0x20);
    if (f > 0.7f) return theme::rgb(0xF0, 0xE0, 0x10);
    if (f > 0.3f) return theme::rgb(0xFF, 0x80, 0x00);
    if (f > 0.0f) return theme::rgb(0xF0, 0x10, 0x10);
    return theme::rgb(0x4C, 0x4C, 0x4C);
}

} // namespace

bool BattlePanel::init() {
    textCache_.clear();
    buf_ = C2D_TextBufNew(1024);
    return buf_ != nullptr;
}

void BattlePanel::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    buf_ = nullptr;
}

void BattlePanel::text(float x, float y, float scale, u32 color, const char* fmt, ...) {
    char line[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    C2D_Text t;
    textCache_.parse(&t, buf_, line, 1024);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.6f, scale, scale, color);
}

// A toon-up on one toon needs another toon to heal (toons never heal themselves in battle).
uint32_t BattlePanel::targetFor(const world::BattleView& v, int track) const {
    if (track == world::kTrackHeal) {
        for (int i = 0; i < v.numToons; i++)
            if (v.toons[i].active && !v.toons[i].local) return v.toons[i].doId;
        return 0;
    }
    if (target_ < v.numCogs && v.cogs[target_].active) return v.cogs[target_].doId;
    for (int i = 0; i < v.numCogs; i++)
        if (v.cogs[i].active) return v.cogs[i].doId;
    return 0;
}

bool BattlePanel::usable(const world::BattleView& v, const BattleGags& g, int track, int level) const {
    if (!g.count || !g.access[track] || g.count[track][level] == 0) return false;
    if (level >= gags::levelsUnlocked(track, g.exp[track])) return false;
    // TownBattle.checkHealTrapLure: no Toon-Up alone, no Trap once every Cog is trapped or lured,
    // no Lure (or Trap) once every Cog is lured.
    int lured = 0;
    int trappedOrLured = 0;
    for (int i = 0; i < v.numCogs; i++) {
        lured += v.cogs[i].lured;
        trappedOrLured += v.cogs[i].lured || v.cogs[i].trapped;
    }
    if (track == world::kTrackHeal && v.numToons == 1) return false;
    if (track == world::kTrackTrap && (trappedOrLured == v.numCogs || lured == v.numCogs)) return false;
    if (track == world::kTrackLure && lured == v.numCogs) return false;
    return world::attackAffectsGroup(track, level) || targetFor(v, track) != 0;
}

u32 BattlePanel::handleInput(u32 keysDown, const touchPosition& touch, const world::BattleView& v,
                             const BattleGags& g) {
    // The battle owns every button while its view is shown (main.cpp also freezes the toon).
    constexpr u32 kAllButtons = 0xFFFFFFFFu;
    if (v.phase == world::BattlePhase::Reward) {
        // Continue: touch it, or A / START (the AI ignores a second rewardDone; Battle guards it).
        pad::buildBattleReward(focus_);
        if (!focus_.hasFocus()) focus_.setFocusById(pad::battle::kIdContinue);
        int activated = -1;
        focus_.handleInput(keysDown & ~KEY_A, touch, &activated);
        if (activated < 0 && (keysDown & (KEY_A | KEY_START))) {
            activated = pad::battle::kIdContinue;
            focus_.logActivate(activated);
        }
        if (activated == pad::battle::kIdContinue) pending_.kind = world::BattleAction::Continue;
        return kAllButtons;
    }
    if (v.phase != world::BattlePhase::Input) {
        focus_.clear(); // the next round starts on the first usable gag again
        return kAllButtons;
    }

    // ---- padnav: one focus table for the cogs, the gag grid and Pass / Run ----
    bool cogActive[pad::battle::kMaxCogs] = {};
    for (int i = 0; i < v.numCogs && i < pad::battle::kMaxCogs; i++) cogActive[i] = v.cogs[i].active;
    bool usableGag[gags::kTracks * gags::kLevels];
    for (int t = 0; t < gags::kTracks; t++) {
        for (int l = 0; l < gags::kLevels; l++) usableGag[t * gags::kLevels + l] = usable(v, g, t, l);
    }
    pad::buildBattleInput(focus_, v.numCogs, cogActive, usableGag, v.canRun);
    if (!focus_.hasFocus()) {
        // Like TownBattle's attack panel: start on the first gag we can use, else Pass.
        for (int i = 0; i < focus_.count(); i++) {
            if (focus_.item(i).enabled && pad::battle::isGagId(focus_.item(i).id)) {
                focus_.setFocus(i);
                break;
            }
        }
        if (!focus_.hasFocus()) focus_.setFocusById(pad::battle::kIdPass);
    }

    // L / R cycle the target cog without leaving the gag grid.
    const int step = (keysDown & KEY_R) ? 1 : (keysDown & KEY_L) ? -1 : 0;
    if (step != 0 && v.numCogs > 1) {
        for (int k = 1; k <= v.numCogs; k++) {
            const int next = ((target_ + step * k) % v.numCogs + v.numCogs) % v.numCogs;
            if (v.cogs[next].active) {
                target_ = next;
                TT_LOG("padnav: battle target %d", target_);
                break;
            }
        }
    }

    int activated = -1;
    focus_.handleInput(keysDown, touch, &activated);
    if (activated >= pad::battle::kIdCog0 && activated < pad::battle::kIdCog0 + pad::battle::kMaxCogs) {
        target_ = activated - pad::battle::kIdCog0;
        TT_LOG("padnav: battle target %d", target_);
    } else if (pad::battle::isGagId(activated)) {
        const int t = activated / 10;
        const int l = activated % 10;
        if (usable(v, g, t, l)) {
            pending_.kind = world::BattleAction::Attack;
            pending_.track = t;
            pending_.level = l;
            pending_.target = world::attackAffectsGroup(t, l) ? 0 : targetFor(v, t);
        }
    } else if (activated == pad::battle::kIdPass) {
        pending_.kind = world::BattleAction::Pass;
    } else if (activated == pad::battle::kIdRun && v.canRun) {
        pending_.kind = world::BattleAction::Run;
    }
    // ---- end padnav ----
    return kAllButtons;
}

void BattlePanel::draw(const world::BattleView& v, const BattleGags& g) {
    // Upstream's TownBattle: the cream gag panel with a row per track in its colour, Cog panels
    // with the suit's health light colours, and round Pass / Run buttons.
    theme::sky();
    theme::paper(1, 0, kW - 2, 17, 5.0f);
    text(6, 0, 0.45f, kInk, "%s", v.status ? v.status : "");
    if (v.secondsLeft >= 0) {
        theme::roundRect(282, 0, 36, 17, 6, v.secondsLeft <= 5 ? theme::kRed : theme::kYellow);
        text(292, 0, 0.5f, v.secondsLeft <= 5 ? theme::kWhite : kInk, "%d", v.secondsLeft);
    }

    // Cogs: name, level, health. The target wears a red frame.
    for (int i = 0; i < v.numCogs && i < 4; i++) {
        const world::BattleCogView& c = v.cogs[i];
        const float x = 2 + i * (kCogW + 1);
        const bool target = i == target_ && v.phase == world::BattlePhase::Input;
        theme::paper(x, kCogY, kCogW, kCogH, 5.0f, c.active ? theme::kPaper : theme::kGrey,
                     target ? theme::kRed : theme::kGreyDark);
        if (target) theme::roundRect(x + 1, kCogY + 1, kCogW - 2, 3, 1, theme::kRed);
        text(x + 5, kCogY + 3, 0.38f, c.active ? kInk : kInkSoft, "%s", c.name);
        text(x + 5, kCogY + 16, 0.36f, kInkSoft, "Level %d%s", c.level, c.lured ? "  Lured" : "");
        const float f = hpFraction(c.hp, c.maxHp);
        theme::roundRect(x + 5, kCogY + 32, kCogW - 10, 7, 3, theme::kGreyDark, 0.55f);
        if (f > 0.0f) theme::roundRect(x + 5, kCogY + 32, (kCogW - 10) * f, 7, 3, healthColor(f), 0.56f);
    }

    if (v.phase == world::BattlePhase::Reward && !v.floorReward) {
        theme::paper(2, kGridY - 2, kW - 4, kBtnY - kGridY - 2, 6.0f);
        text(10, kGridY, 0.5f, theme::kRedDark, "Cogs defeated: %d", v.cogsDefeated);
        // RewardPanel: per track, the experience toward the next gag level and what this
        // battle added.
        for (int i = 0; i < v.numRewardRows; i++) {
            const world::BattleRewardRow& r = v.reward[i];
            const float y = kGridY + 20 + i * 18;
            const int total = r.orig + r.earned;
            int prev = 0;
            int next = -1;
            for (int l = 0; l < gags::kLevels; l++) {
                if (total < gags::kLevelExp[r.track][l]) {
                    next = gags::kLevelExp[r.track][l];
                    break;
                }
                prev = gags::kLevelExp[r.track][l];
            }
            const float f = next < 0 ? 1.0f : float(total - prev) / float(next - prev);
            text(10, y, 0.42f, kInk, "%s", theme::kTrackTitles[r.track]);
            theme::roundRect(80, y + 3, 120, 10, 4, theme::kGrey);
            theme::roundRect(80, y + 3, 120 * (f < 0 ? 0 : f > 1 ? 1 : f), 10, 4, theme::kTrackColors[r.track], 0.55f);
            if (next < 0) text(206, y, 0.38f, kInkSoft, "Max");
            else text(206, y, 0.38f, kInkSoft, "%d/%d", total, next);
            if (r.earned > 0) text(272, y, 0.44f, theme::kGreenDark, "+%d", r.earned);
        }
        theme::button(pad::battle::kContinueX, kBtnY, pad::battle::kBtnW, kBtnH, v.rewardSent ? theme::kGrey : theme::kGreen,
                      v.rewardSent ? theme::kGreyDark : theme::kGreenDark);
        text(pad::battle::kContinueX + 22, kBtnY + 4, 0.5f, v.rewardSent ? kInkSoft : theme::kWhite, "Continue");
        focus_.drawHighlight();
        return;
    }

    // Gag grid: one row per track, a cell per level with our count.
    const bool choosing = v.phase == world::BattlePhase::Input;
    theme::paper(1, kGridY - 3, kW - 2, gags::kTracks * kRowH + 4, 5.0f);
    for (int t = 0; t < gags::kTracks; t++) {
        const float y = kGridY + t * kRowH;
        const bool have = g.access && g.access[t];
        theme::roundRect(4, y, kGridX - 6, kRowH - 2, 4, have ? theme::kTrackColors[t] : theme::kGrey);
        text(8, y + 1, 0.38f, have ? kInk : kInkSoft, "%s", theme::kTrackTitles[t]);
        if (!have || !g.count) continue;
        const int levels = gags::levelsUnlocked(t, g.exp[t]);
        for (int l = 0; l < levels; l++) {
            const float x = kGridX + l * (kCellW + 2);
            const bool ok = choosing && usable(v, g, t, l);
            const u32 face = ok ? theme::mix(theme::kTrackColors[t], theme::kWhite, 0.55f) : theme::kPaperShade;
            theme::roundRect(x, y, kCellW, kRowH - 2, 4, ok ? theme::shade(theme::kTrackColors[t], 0.7f) : theme::kGrey);
            theme::roundRect(x + 1, y + 1, kCellW - 2, kRowH - 4, 3, face);
            world::GagAtlas::instance().drawIcon(t, l, x + 2, y + 1, 16.0f, 0.55f, ok ? 1.0f : 0.4f);
            text(x + 20, y + 2, 0.38f, ok ? kInk : kInkSoft, "%u", unsigned(g.count[t][l]));
        }
    }

    theme::button(pad::battle::kPassX, kBtnY, pad::battle::kBtnW, kBtnH, choosing ? theme::kBlue : theme::kGrey,
                  choosing ? theme::kBlueDark : theme::kGreyDark);
    text(pad::battle::kPassX + 36, kBtnY + 4, 0.5f, choosing ? theme::kWhite : kInkSoft, "Pass");
    const bool canRun = choosing && v.canRun;
    theme::button(pad::battle::kRunX, kBtnY, pad::battle::kBtnW, kBtnH, canRun ? theme::kRed : theme::kGrey,
                  canRun ? theme::kRedDark : theme::kGreyDark);
    text(pad::battle::kRunX + 38, kBtnY + 4, 0.5f, canRun ? theme::kWhite : kInkSoft, "Run");
    if (choosing) focus_.drawHighlight();
}

} // namespace tt::ui
