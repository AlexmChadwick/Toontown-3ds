#include "render/text_cache.h"
// Bottom-screen battle panel (replaces the tabs while the local toon is in a street battle):
// the cogs with their HP (tap one to target it), the gag grid from our inventory, Pass / Run,
// and the reward screen with Continue. Upstream: TownBattle's attack / target panels.
#pragma once

#include <3ds.h>
#include <citro2d.h>

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

    render::TextCache textCache_;
    C2D_TextBuf buf_ = nullptr;
    int target_ = 0; // index into BattleView::cogs
    world::BattleAction pending_{};
};

} // namespace tt::ui
