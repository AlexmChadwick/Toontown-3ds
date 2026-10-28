#include "ui/panel_geometry.h"
#include "ui/battle_panel.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "ui/gag_data.h"
#include "ui/pad_tables.h"
#include "core/log.h"
#include "world/battle_fx.h"
#include "ui/suit_heads_atlas.h"
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
// Suit.updateHealthBar's condition: 0 green (> 95%), 1 yellow (> 70%), 2 orange (> 30%),
// 3 red (> 5%), 4 blinking red (> 0), 5 defeated (fast blink).
int healthCondition(float f) {
    return f > 0.95f ? 0 : f > 0.7f ? 1 : f > 0.3f ? 2 : f > 0.05f ? 3 : f > 0.0f ? 4 : 5;
}

// Suit.healthColors / healthGlowColors. Conditions 4 and 5 blink between red and grey
// (__blinkRed / __blinkGray: red 0.75 s then grey 0.1 s; 0.25 s / 0.1 s once defeated).
void healthColors(int condition, u32* light, u32* glow) {
    static constexpr u32 kLight[5] = {theme::rgb(0x00, 0xFF, 0x00), theme::rgb(0xFF, 0xFF, 0x00),
                                      theme::rgb(0xFF, 0x80, 0x00), theme::rgb(0xFF, 0x00, 0x00),
                                      theme::rgb(0x4C, 0x4C, 0x4C)};
    static constexpr u32 kGlow[5] = {theme::rgb(0x40, 0xFF, 0x40, 0x80), theme::rgb(0xFF, 0xFF, 0x40, 0x80),
                                     theme::rgb(0xFF, 0x80, 0x40, 0x80), theme::rgb(0xFF, 0x40, 0x40, 0x80),
                                     theme::rgb(0x4C, 0x4C, 0x4C, 0x00)};
    int i = condition;
    if (condition >= 4) {
        const u64 period = condition == 4 ? 850 : 350, red = condition == 4 ? 750 : 250;
        i = osGetTime() % period < red ? 3 : 4;
    }
    *light = kLight[i];
    *glow = kGlow[i];
}

// The suit's chest light (Suit.generateHealthBar: a round button with a glow) beside a meter
// in the same colour, so the health reads at a glance on the small screen.
void drawHealthMeter(float x, float y, float w, float f) {
    u32 light, glow;
    healthColors(healthCondition(f), &light, &glow);
    constexpr float kD = 11.0f; // light diameter
    theme::roundRect(x - 1.5f, y - 1.5f, kD + 3, kD + 3, (kD + 3) * 0.5f, glow, 0.55f);
    theme::roundRect(x, y, kD, kD, kD * 0.5f, theme::rgb(0x30, 0x30, 0x30), 0.56f);
    theme::roundRect(x + 1, y + 1, kD - 2, kD - 2, (kD - 2) * 0.5f, light, 0.57f);
    theme::roundRect(x + 3, y + 2.5f, 3, 2, 1, theme::mix(light, theme::kWhite, 0.7f), 0.58f); // shine
    const float bx = x + kD + 3, bw = w - kD - 3, by = y + 2, bh = kD - 4;
    theme::roundRect(bx, by, bw, bh, bh * 0.5f, theme::rgb(0x30, 0x30, 0x30), 0.56f);
    theme::roundRect(bx + 1, by + 1, bw - 2, bh - 2, (bh - 2) * 0.5f, theme::rgb(0x5A, 0x5A, 0x5A), 0.57f);
    if (f > 0.0f) {
        const float fw = (bw - 2) * f < bh - 2 ? bh - 2 : (bw - 2) * f; // never thinner than round
        theme::roundRect(bx + 1, by + 1, fw, bh - 2, (bh - 2) * 0.5f, light, 0.58f);
        theme::roundRect(bx + 2, by + 1.5f, fw - 2 > 0 ? fw - 2 : 0, 1.0f, 0.5f,
                         theme::mix(light, theme::kWhite, 0.6f), 0.59f);
    }
}

int headIndex(const char* code) {
    if (!code[0]) return -1;
    for (int i = 0; i < kNumSuitHeads; i++) {
        if (!strcmp(kSuitHeadSubTex[i].code, code)) return i;
    }
    return -1;
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

    // Cogs: name, head, level and the chest light's health. The target wears a red frame.
    SuitHeadAtlas::instance().init(); // once; the Cog Gallery shares it
    for (int i = 0; i < v.numCogs && i < 4; i++) {
        const world::BattleCogView& c = v.cogs[i];
        const float x = 2 + i * (kCogW + 1);
        const bool target = i == target_ && v.phase == world::BattlePhase::Input;
        theme::paper(x, kCogY, kCogW, kCogH, 5.0f, c.active ? theme::kPaper : theme::kGrey,
                     target ? theme::kRed : theme::kGreyDark);
        if (target) theme::roundRect(x + 1, kCogY + 1, kCogW - 2, 3, 1, theme::kRed);
        C2D_Text name;
        textCache_.parse(&name, buf_, c.name, 1024);
        theme::textFit(name, x + 4, kCogY + 3, 0.38f, c.active ? kInk : kInkSoft, kCogW - 8, 0.6f);
        const int head = c.skelecog ? -1 : headIndex(c.code);
        float rx = x + 4;
        if (head >= 0) {
            theme::roundRect(x + 3, kCogY + 16, 25, 25, 5, theme::kPaperShade, 0.56f);
            SuitHeadAtlas::instance().drawHead(head / 8, head % 8, x + 3.5f, kCogY + 16.5f, 24.0f, 0.58f);
            rx = x + 31;
        }
        if (c.lured) {
            theme::roundRect(rx - 1, kCogY + 16, 30, 12, 5, theme::kGreen, 0.57f);
            text(rx + 3, kCogY + 16, 0.34f, theme::kWhite, "Lured");
        } else if (c.level > 0) {
            text(rx, kCogY + 16, 0.36f, kInkSoft, "Level %d", c.level);
        }
        if (loggedCog_[i] != c.doId || loggedHp_[i] != c.hp || loggedMax_[i] != c.maxHp) {
            loggedCog_[i] = c.doId;
            loggedHp_[i] = c.hp;
            loggedMax_[i] = c.maxHp;
            TT_LOG("battle: cog %u %s hp %d/%d", unsigned(c.doId), c.code, c.hp, c.maxHp);
        }
        // A Cog joins at full health; until its max is known draw it full.
        const float f = c.maxHp > 0 ? hpFraction(c.hp, c.maxHp) : 1.0f;
        drawHealthMeter(rx, kCogY + 29, x + kCogW - 4 - rx, f);
    }

    if (v.phase == world::BattlePhase::Reward && !v.floorReward) {
        // RewardPanel: the defeated Cogs have left, so the panel takes the space under the status.
        const float top = kCogY + 2;
        theme::paper(2, top, kW - 4, kBtnY - top - 4, 6.0f);
        text(10, top + 3, 0.5f, theme::kRedDark, "Cogs defeated: %d", v.cogsDefeated);
        // RewardPanel.incrementExp per track: a bar in the track colour on a darker frame, range =
        // the next gag level's experience, value = our experience ("exp/next" centred), or the
        // uber "N to Go!" past regMaxSkill.
        constexpr int kRegMaxSkill = 10000, kUberSkill = 500;
        constexpr float kBarX = 76, kBarW = 180, kBarH = 13;
        for (int i = 0; i < v.numRewardRows; i++) {
            const world::BattleRewardRow& r = v.reward[i];
            const float y = top + 22 + i * 18;
            int value = r.orig + r.earned;
            if (value > gags::kMaxSkill) value = gags::kMaxSkill;
            int range = kUberSkill;
            char label[24];
            if (value >= kRegMaxSkill) {
                value -= kRegMaxSkill;
                snprintf(label, sizeof label, "%d to Go!", kUberSkill - value);
            } else {
                for (int l = 0; l < gags::kLevels; l++) {
                    if (value < gags::kLevelExp[r.track][l]) {
                        range = gags::kLevelExp[r.track][l];
                        break;
                    }
                }
                snprintf(label, sizeof label, "%d/%d", value, range);
            }
            const float f = range > 0 ? (value >= range ? 1.0f : float(value) / float(range)) : 1.0f;
            const u32 col = theme::kTrackColors[r.track];
            text(10, y, 0.42f, kInk, "%s", theme::kTrackTitles[r.track]);
            theme::roundRect(kBarX, y + 1, kBarW, kBarH, 4, theme::shade(col, 0.7f));
            if (f > 0.0f) theme::roundRect(kBarX + 1, y + 2, (kBarW - 2) * f, kBarH - 2, 3, col, 0.55f);
            C2D_Text t;
            textCache_.parse(&t, buf_, label, 1024);
            theme::textCentered(t, kBarX + kBarW * 0.5f, y + 0.5f, 0.38f, kInk, kBarW - 6, 0.6f);
            if (r.earned > 0) text(kBarX + kBarW + 6, y, 0.44f, theme::kGreenDark, "+%d", r.earned);
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
