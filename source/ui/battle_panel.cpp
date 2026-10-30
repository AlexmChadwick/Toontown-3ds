#include "ui/panel_geometry.h"
#include "ui/battle_panel.h"

#include <cstdarg>
#include <cstdio>

#include "ui/gag_data.h"
#include "ui/sos_tables.h"
#include "ui/draw_shapes.h"
#include "ui/pad_tables.h"
#include "core/log.h"
#include "world/battle_fx.h"

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

const u32 kBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kPanel = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
const u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kInk = C2D_Color32(0x20, 0x18, 0x10, 0xFF);
const u32 kDim = C2D_Color32(0xA8, 0xB4, 0xCC, 0xFF);
const u32 kHp = C2D_Color32(0x5C, 0xD0, 0x5C, 0xFF);
const u32 kHpMid = C2D_Color32(0xF1, 0xC4, 0x32, 0xFF);
const u32 kHpLow = C2D_Color32(0xE0, 0x50, 0x40, 0xFF);
const u32 kMeterFrame = C2D_Color32(0x45, 0x2B, 0x18, 0xFF);
const u32 kMeterEmpty = C2D_Color32(0x73, 0x69, 0x62, 0xFF);
const u32 kSlotFrame = C2D_Color32(0x70, 0x59, 0x42, 0xFF);
const u32 kSlotPaper = C2D_Color32(0xF4, 0xED, 0xDC, 0xFF);
const u32 kLockedPaper = C2D_Color32(0xC9, 0xC1, 0xB2, 0xFF);
const u32 kLockedInk = C2D_Color32(0x75, 0x6D, 0x62, 0xFF);
const u32 kFirePaper = C2D_Color32(0xF4, 0xC7, 0xAE, 0xFF);
const u32 kSosPaper = C2D_Color32(0xC4, 0xE3, 0xF3, 0xFF);
const u32 kPassPaper = C2D_Color32(0xF6, 0xE3, 0xA8, 0xFF);
const u32 kRunPaper = C2D_Color32(0xE8, 0xD0, 0xC5, 0xFF);
// Track colours, roughly the Shticker Book's gag pages.
const u32 kTrackColor[gags::kTracks] = {
    C2D_Color32(0xD8, 0x6A, 0xC8, 0xFF), C2D_Color32(0xD8, 0xC8, 0x40, 0xFF), C2D_Color32(0x40, 0xB0, 0x40, 0xFF),
    C2D_Color32(0x40, 0x70, 0xD8, 0xFF), C2D_Color32(0xE0, 0x80, 0x30, 0xFF), C2D_Color32(0xE0, 0x50, 0xA0, 0xFF),
    C2D_Color32(0x40, 0xB8, 0xD8, 0xFF),
};

// The stock battle UI reads like the Cog's chest light, not a desktop progress
// control.  Five inset lamps make damage readable at 3DS size while retaining the
// green -> amber -> red warning language of the original Cog health light.
void drawCogHealth(float x, float y, float width, int hp, int maxHp) {
    constexpr int lamps = 5;
    constexpr float gap = 2.0f;
    const float f = hpFraction(hp, maxHp);
    const int lit = hp > 0 ? static_cast<int>(f * lamps + 0.999f) : 0;
    const u32 live = f > 0.5f ? kHp : f > 0.25f ? kHpMid : kHpLow;
    C2D_DrawRectSolid(x, y, 0.55f, width, 10, kMeterFrame);
    const float lampW = (width - 4 - gap * (lamps - 1)) / lamps;
    for (int i = 0; i < lamps; i++) {
        const u32 color = i < lit ? live : kMeterEmpty;
        C2D_DrawRectSolid(x + 2 + i * (lampW + gap), y + 2, 0.56f, lampW, 6, color);
        if (i < lit) C2D_DrawRectSolid(x + 3 + i * (lampW + gap), y + 2, 0.57f, lampW - 2, 1,
                                      C2D_Color32(0xFF, 0xFF, 0xFF, 0x70));
    }
}

void drawToonLaff(float x, float y, float width, int hp, int maxHp) {
    constexpr int lamps = 8;
    constexpr float gap = 1.0f;
    const float lampW = (width - gap * (lamps - 1)) / lamps;
    const float f = hpFraction(hp, maxHp);
    const int lit = hp > 0 ? static_cast<int>(f * lamps + 0.999f) : 0;
    const u32 color = f > 0.5f ? C2D_Color32(0x53, 0xB8, 0x55, 0xFF)
                    : f > 0.25f ? C2D_Color32(0xF0, 0xC4, 0x3C, 0xFF)
                                : C2D_Color32(0xD8, 0x54, 0x47, 0xFF);
    for (int i = 0; i < lamps; ++i) {
        shape::roundRect(x + i * (lampW + gap), y, 0.58f, lampW, 5, 2,
                         i < lit ? color : C2D_Color32(0x91, 0x89, 0x7B, 0xFF));
    }
}

u32 sosCardColor(const SosCardDef* def) {
    if (!def) return kSlotFrame;
    if (def->track < gags::kTracks) return kTrackColor[def->track];
    if (def->track == 7) return C2D_Color32(0x57, 0x99, 0xB8, 0xFF); // restock
    if (def->track == 8) return C2D_Color32(0xD8, 0x67, 0x58, 0xFF); // Toons hit
    return C2D_Color32(0x8B, 0x75, 0xA8, 0xFF);                    // Cogs miss
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
    if (!g.count || !g.access || !g.exp || !g.access[track] || g.count[track][level] == 0) return false;
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
        choosingSos_ = false;
        choosingToon_ = false;
        pendingHealLevel_ = -1;
        focus_.clear(); // the next round starts on the first usable gag again
        return kAllButtons;
    }

    // A Cog can leave between input frames. Keep the gold target frame and the
    // request target on the same live Cog instead of leaving a defeated card
    // selected until the player presses L/R.
    if (target_ < 0 || target_ >= v.numCogs || !v.cogs[target_].active) {
        const int oldTarget = target_;
        target_ = 0;
        for (int i = 0; i < v.numCogs; ++i) {
            if (v.cogs[i].active) {
                target_ = i;
                break;
            }
        }
        if (target_ != oldTarget) TT_LOG("padnav: battle target %d", target_);
    }

    // ---- padnav: one focus table for the cogs, the gag grid and Pass / Run ----
    bool cogActive[pad::battle::kMaxCogs] = {};
    for (int i = 0; i < v.numCogs && i < pad::battle::kMaxCogs; i++) cogActive[i] = v.cogs[i].active;
    bool usableGag[gags::kTracks * gags::kLevels];
    for (int t = 0; t < gags::kTracks; t++) {
        for (int l = 0; l < gags::kLevels; l++) usableGag[t * gags::kLevels + l] = usable(v, g, t, l);
    }
    int sosCount = 0;
    if (g.book) for (int i = 0; i < g.book->numSosCards; ++i) sosCount += g.book->sosCards[i].count > 0;

    if (choosingSos_) {
        focus_.rebuild();
        for (int i = 0; g.book && i < g.book->numSosCards; ++i) {
            const int col = i % 2, row = i / 2;
            focus_.addItem(300 + i, 4 + col * 158, 28 + row * 21, 154, 19, g.book->sosCards[i].count > 0);
        }
        focus_.addItem(399, 110, 210, 100, 28);
        if (!focus_.hasFocus()) {
            for (int i = 0; g.book && i < g.book->numSosCards; ++i) {
                if (g.book->sosCards[i].count) {
                    focus_.setFocusById(300 + i);
                    break;
                }
            }
            if (!focus_.hasFocus()) focus_.setFocusById(399);
        }
        int activated = -1;
        focus_.handleInput(keysDown, touch, &activated);
        if ((keysDown & KEY_B) || activated == 399) {
            choosingSos_ = false;
            focus_.clear();
        } else if (activated >= 300 && activated < 300 + net::kMaxSosCards && g.book) {
            const net::SosEntry& card = g.book->sosCards[activated - 300];
            if (card.count) {
                pending_.kind = world::BattleAction::Attack;
                pending_.track = world::kAttackNpcSos;
                pending_.level = -1;
                pending_.target = card.npcId;
                choosingSos_ = false;
            }
        }
        return kAllButtons;
    }

    if (choosingToon_) {
        int toonSlots[pad::battle::kMaxToonTargets] = {};
        int toonCount = 0;
        for (int i = 0; i < v.numToons && toonCount < pad::battle::kMaxToonTargets; ++i) {
            if (v.toons[i].active && !v.toons[i].local) toonSlots[toonCount++] = i;
        }
        pad::buildBattleToonTargets(focus_, toonCount);
        if (!focus_.hasFocus()) focus_.setFocusById(toonCount ? pad::battle::kIdToon0
                                                              : pad::battle::kIdTargetBack);
        int activated = -1;
        focus_.handleInput(keysDown, touch, &activated);
        if ((keysDown & KEY_B) || activated == pad::battle::kIdTargetBack || toonCount == 0) {
            choosingToon_ = false;
            pendingHealLevel_ = -1;
            focus_.clear();
        } else if (activated >= pad::battle::kIdToon0 && activated < pad::battle::kIdToon0 + toonCount) {
            pending_.kind = world::BattleAction::Attack;
            pending_.track = world::kTrackHeal;
            pending_.level = pendingHealLevel_;
            pending_.target = v.toons[toonSlots[activated - pad::battle::kIdToon0]].doId;
            choosingToon_ = false;
            pendingHealLevel_ = -1;
        }
        return kAllButtons;
    }

    pad::buildBattleInput(focus_, v.numCogs, cogActive, usableGag, v.canRun, g.pinkSlips > 0, sosCount > 0);
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
            int healTargets = 0;
            if (t == world::kTrackHeal && !world::attackAffectsGroup(t, l)) {
                for (int i = 0; i < v.numToons; ++i)
                    healTargets += v.toons[i].active && !v.toons[i].local;
            }
            if (healTargets > 1) {
                choosingToon_ = true;
                pendingHealLevel_ = l;
                focus_.clear();
            } else {
                pending_.kind = world::BattleAction::Attack;
                pending_.track = t;
                pending_.level = l;
                pending_.target = world::attackAffectsGroup(t, l) ? 0 : targetFor(v, t);
            }
        }
    } else if (activated == pad::battle::kIdPass) {
        pending_.kind = world::BattleAction::Pass;
    } else if (activated == pad::battle::kIdFire && g.pinkSlips && targetFor(v, world::kTrackThrow)) {
        pending_.kind = world::BattleAction::Attack;
        pending_.track = world::kAttackFire;
        pending_.level = -1;
        pending_.target = targetFor(v, world::kTrackThrow);
    } else if (activated == pad::battle::kIdSos && sosCount) {
        choosingSos_ = true;
        focus_.clear();
    } else if (activated == pad::battle::kIdRun && v.canRun) {
        pending_.kind = world::BattleAction::Run;
    }
    // ---- end padnav ----
    return kAllButtons;
}

void BattlePanel::draw(const world::BattleView& v, const BattleGags& g) {
    // Persistent glyph cache: unchanged strings require no layout work.
    C2D_DrawRectSolid(0, 0, 0.5f, kW, 240, kBg);
    shape::roundRect(2, 1, 0.51f, 316, 15, 6, kPanel);
    text(7, 2, 0.42f, kWhite, "%s", v.status ? v.status : "");
    if (v.secondsLeft >= 0) {
        shape::roundRect(282, 2, 0.53f, 33, 13, 6, v.secondsLeft <= 5 ? kHpLow : kAccent);
        text(v.secondsLeft < 10 ? 295 : 291, 2, 0.45f, kBg, "%d", v.secondsLeft);
    }

    // Cogs: name, level, HP bar. The target has a gold frame.
    for (int i = 0; i < v.numCogs && i < 4; i++) {
        const world::BattleCogView& c = v.cogs[i];
        const float x = 2 + i * (kCogW + 1);
        if (i == target_ && v.phase == world::BattlePhase::Input) {
            C2D_DrawRectSolid(x - 1, kCogY - 1, 0.5f, kCogW + 2, kCogH + 2, kAccent);
        }
        shape::roundRect(x, kCogY, 0.51f, kCogW, kCogH, 4, c.active ? kPanel : kBg);
        text(x + 3, kCogY + 2, 0.38f, c.active ? kWhite : kDim, "%s", c.name);
        text(x + 3, kCogY + 15, 0.38f, kDim, "Level %d%s", c.level, c.lured ? " lured" : "");
        drawCogHealth(x + 3, kCogY + 31, kCogW - 6, c.hp, c.maxHp);
    }

    if (v.phase == world::BattlePhase::Reward && !v.floorReward) {
        text(8, kGridY, 0.5f, kWhite, "Cogs defeated: %d", v.cogsDefeated);
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
            text(8, y, 0.42f, kWhite, "%s", gags::kTrackNames[r.track]);
            C2D_DrawRectSolid(80, y + 3, 0.5f, 120, 9, kPanel);
            C2D_DrawRectSolid(80, y + 3, 0.55f, 120 * (f < 0 ? 0 : f > 1 ? 1 : f), 9, kTrackColor[r.track]);
            if (next < 0) text(206, y, 0.38f, kDim, "Max");
            else text(206, y, 0.38f, kDim, "%d/%d", total, next);
            if (r.earned > 0) text(272, y, 0.42f, kAccent, "+%d", r.earned);
        }
        C2D_DrawRectSolid(218, kBtnY, 0.5f, 100, kBtnH, v.rewardSent ? kPanel : kAccent);
        text(236, kBtnY + 6, 0.5f, v.rewardSent ? kDim : kBg, "Continue");
        focus_.drawHighlight();
        return;
    }

    if (choosingSos_ && v.phase == world::BattlePhase::Input) {
        shape::roundRect(2, 18, 0.52f, 316, 188, 8, kSlotPaper);
        text(10, 20, 0.46f, kInk, "Choose an SOS ToonFriend");
        for (int i = 0; g.book && i < g.book->numSosCards; ++i) {
            const net::SosEntry& card = g.book->sosCards[i];
            const SosCardDef* def = findSosCard(card.npcId);
            const int col = i % 2, row = i / 2;
            const float x = 4 + col * 158, y = 28 + row * 21;
            shape::roundFrame(x, y, 0.54f, 154, 19, 5, 1, kSlotFrame,
                              card.count ? kWhite : C2D_Color32(0xC8, 0xC2, 0xB5, 0xFF));
            C2D_DrawRectSolid(x + 3, y + 3, 0.56f, 5, 13,
                              card.count ? sosCardColor(def) : kLockedInk);
            // The old chooser exposed only the NPC name.  At touch-screen size a
            // compact second line is enough to make unlike cards distinguishable
            // without opening another panel or hiding any of the 16 inventory slots.
            text(x + 11, y + 1, 0.29f, card.count ? kInk : kDim,
                 "%s", def ? def->name : "SOS Toon");
            if (def && def->track < gags::kTracks)
                text(x + 11, y + 9, 0.24f, card.count ? kInk : kDim,
                     "%s L%d", sosTrackName(def->track, def->level), int(def->level) + 1);
            else
                text(x + 11, y + 9, 0.24f, card.count ? kInk : kDim,
                     "%s", def ? sosTrackName(def->track, def->level) : "SOS CARD");
            shape::roundRect(x + 132, y + 3, 0.58f, 19, 13, 4,
                             card.count ? sosCardColor(def) : kLockedInk);
            text(x + 135, y + 5, 0.27f, kWhite, "x%u", unsigned(card.count));
        }
        shape::roundFrame(110, 210, 0.54f, 100, 28, 9, 2, kSlotFrame, kSlotPaper);
        text(143, 216, 0.5f, kInk, "Back");
        focus_.drawHighlight();
        return;
    }

    if (choosingToon_ && v.phase == world::BattlePhase::Input) {
        shape::roundRect(2, 18, 0.52f, 316, 188, 8, kSlotPaper);
        text(10, 24, 0.52f, kInk, "Who needs Toon-Up?");
        int shown = 0;
        for (int i = 0; i < v.numToons && shown < pad::battle::kMaxToonTargets; ++i) {
            if (!v.toons[i].active || v.toons[i].local) continue;
            const float y = pad::battle::kTargetY + shown * (pad::battle::kTargetH + pad::battle::kTargetGap);
            shape::roundFrame(pad::battle::kTargetX, y, 0.54f, pad::battle::kTargetW,
                              pad::battle::kTargetH, 10, 2, kSlotFrame, kWhite);
            C2D_DrawRectSolid(pad::battle::kTargetX + 5, y + 5, 0.57f, 6,
                              pad::battle::kTargetH - 10, kTrackColor[world::kTrackHeal]);
            text(pad::battle::kTargetX + 20, y + 5, 0.46f, kInk, "%s", v.toons[i].name);
            if (v.toons[i].maxHp > 0) {
                drawToonLaff(pad::battle::kTargetX + 20, y + 22, 116,
                             v.toons[i].hp, v.toons[i].maxHp);
                text(pad::battle::kTargetX + 146, y + 15, 0.36f, kInk, "%d/%d Laff",
                     v.toons[i].hp > 0 ? v.toons[i].hp : 0, v.toons[i].maxHp);
            } else {
                text(pad::battle::kTargetX + 20, y + 20, 0.34f, kDim, "Laff unavailable");
            }
            ++shown;
        }
        shape::roundFrame(pad::battle::kTargetBackX, pad::battle::kTargetBackY, 0.54f,
                          pad::battle::kTargetBackW, pad::battle::kTargetBackH, 9, 2,
                          kSlotFrame, kSlotPaper);
        text(143, pad::battle::kTargetBackY + 6, 0.5f, kInk, "Back");
        focus_.drawHighlight();
        return;
    }

    // Gag grid: every track and level keeps its place, even while locked.  Stable
    // silhouettes are much easier to scan than the old debug-table gaps.
    const bool choosing = v.phase == world::BattlePhase::Input;
    for (int t = 0; t < gags::kTracks; t++) {
        const float y = kGridY + t * kRowH;
        const bool have = g.access && g.access[t];
        text(4, y + 2, 0.38f, have ? kWhite : kDim, "%s", gags::kTrackNames[t]);
        const int levels = have && g.exp ? gags::levelsUnlocked(t, g.exp[t]) : 0;
        for (int l = 0; l < gags::kLevels; l++) {
            const float x = kGridX + l * (kCellW + 2);
            const bool learned = have && g.count && l < levels;
            const bool ok = learned && choosing && usable(v, g, t, l);
            shape::roundFrame(x, y, 0.51f, kCellW, kRowH - 2, 4, 1,
                              ok ? kAccent : kSlotFrame, learned ? kSlotPaper : kLockedPaper);
            if (learned) {
                world::GagAtlas::instance().drawIcon(t, l, x + 2, y + 1, 16.0f, 0.55f,
                                                     g.count[t][l] ? 1.0f : 0.35f);
                shape::roundRect(x + 20, y + 3, 0.57f, 12, 12, 5,
                                 g.count[t][l] ? kTrackColor[t] : kPanel);
                text(x + (g.count[t][l] >= 10 ? 21 : 24), y + 3, 0.30f,
                     g.count[t][l] ? kBg : kDim, "%u", unsigned(g.count[t][l]));
            } else {
                // Keep the complete seven-level ladder readable before a gag is
                // earned.  Stock presents locked gags as dimmed cards; a coloured
                // track ribbon and level number communicate the same thing at 3DS
                // resolution without pretending the gag is selectable.
                C2D_DrawRectSolid(x + 2, y + 2, 0.55f, 3, kRowH - 6, kTrackColor[t]);
                text(x + 13, y + 2, 0.34f, kLockedInk, "%d", l + 1);
            }
        }
    }

    int sosCount = 0;
    if (g.book) for (int i = 0; i < g.book->numSosCards; ++i) sosCount += g.book->sosCards[i].count > 0;
    shape::roundFrame(pad::battle::kFireX, kBtnY, 0.51f, pad::battle::kBtnW, kBtnH, 9, 2, kSlotFrame,
                      choosing && g.pinkSlips ? kFirePaper : kBg);
    text(17, kBtnY + 6, 0.43f, choosing && g.pinkSlips ? kInk : kDim, "Fire x%u", unsigned(g.pinkSlips));
    shape::roundFrame(pad::battle::kSosX, kBtnY, 0.51f, pad::battle::kBtnW, kBtnH, 9, 2, kSlotFrame,
                      choosing && sosCount ? kSosPaper : kBg);
    text(102, kBtnY + 6, 0.45f, choosing && sosCount ? kInk : kDim, "SOS");
    shape::roundFrame(pad::battle::kPassX, kBtnY, 0.51f, pad::battle::kBtnW, kBtnH, 9, 2, kSlotFrame,
                      choosing ? kPassPaper : kBg);
    text(181, kBtnY + 6, 0.45f, choosing ? kInk : kDim, "Pass");
    shape::roundFrame(pad::battle::kRunX, kBtnY, 0.51f, pad::battle::kBtnW, kBtnH, 9, 2, kSlotFrame,
                      choosing && v.canRun ? kRunPaper : kBg);
    text(265, kBtnY + 6, 0.45f, choosing && v.canRun ? kInk : kDim, "Run");
    if (choosing) focus_.drawHighlight();
}

} // namespace tt::ui
