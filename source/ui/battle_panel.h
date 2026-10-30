#include "render/text_cache.h"
#include "ui/focus.h"
// Bottom-screen battle panel (replaces the tabs while the local toon is in a street battle):
// the cogs with their HP (tap one to target it), the gag grid from our inventory, Pass / Run,
// and the reward screen with Continue. Upstream: TownBattle's attack / target panels.
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "ui/laff_meter.h"
#include "world/battle.h"

namespace tt::ui {

// Our toon's gags (net::OwnStats), as the panel needs them.
struct BattleGags {
    const uint8_t (*count)[7] = nullptr; // [track][level]
    const uint16_t* exp = nullptr;       // [7]
    const uint8_t* access = nullptr;     // [7]
};

class BattlePanel {
public:
    bool init();
    void shutdown();
    // Returns the buttons consumed (all of them while a battle view is shown).
    u32 handleInput(u32 keysDown, const touchPosition& touch, const world::BattleView& v, const BattleGags& g);
    void draw(const world::BattleView& v, const BattleGags& g);
    // The player's choice, once.
    bool popAction(world::BattleAction& out) {
        if (pending_.kind == world::BattleAction::None) return false;
        out = pending_;
        pending_ = world::BattleAction{};
        return true;
    }

private:
    bool usable(const world::BattleView& v, const BattleGags& g, int track, int level) const;
    uint32_t targetFor(const world::BattleView& v, int track) const;
    void text(float x, float y, float scale, u32 color, const char* fmt, ...) __attribute__((format(printf, 6, 7)));
    void drawToons(const world::BattleView& v, float x0, float w);
    // InventoryNew's battle buttons (Fire / Pass / Run / SOS) in its own blue.
    void drawButtons(const world::BattleView& v);
    void drawSos(const world::BattleView& v);
    void drawFire(const world::BattleView& v);
    void smallButton(float x, float y, float w, bool enabled, const char* label, bool hover = false);
    void rightText(float right, float y, float scale, u32 color, const char* str);
    bool sosCallable(const world::BattleView& v, int card) const;
    bool fireable(const world::BattleView& v, int cog) const;
    void centered(float cx, float y, float scale, u32 color, float maxW, const char* str);

    render::TextCache textCache_;
    C2D_TextBuf buf_ = nullptr;
    int target_ = 0; // index into BattleView::cogs
    // TownBattle's sub-panels while choosing: the attack panel, TownBattleSOSPanel, FireCogPanel.
    enum class Mode : uint8_t { Attack, Sos, Fire } mode_ = Mode::Attack;
    int sosView_ = 0; // which eight Rescued Toons the SOS panel shows
    int returnFocus_ = -1; // the attack panel's focus when SOS / Fire opened
    int wantFocus_ = -1;   // focus to set once the next table is built
    // Last health logged per card ("battle: cog <id> hp a/b"), so tests can follow the meter.
    uint32_t loggedCog_[4] = {};
    int loggedHp_[4] = {}, loggedMax_[4] = {};
    world::BattleAction pending_{};
    // Each toon card's laff pulse (LaffMeter.animatedEffect), by the doId it last showed.
    uint32_t pulseId_[world::kBattleToonCards] = {};
    LaffPulse pulse_[world::kBattleToonCards];
    FocusGroup focus_{"battle"};
};

} // namespace tt::ui
