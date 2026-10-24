// Gag shop purchase rules, host-testable (no libctru). Mirrors upstream InventoryBase
// (addItem / validatePurchase / validateItemsBasedOnExp) and PurchaseBase.handlePurchase.
// See docs/design/shop.md. Costs: every gag costs exactly 1 jellybean, whatever its level.
#pragma once

#include <cstdint>

#include "ui/gag_data.h"

namespace tt::shop {

constexpr int kRegularLevels = 6; // levels 0..5 are buyable and count against maxCarry;
                                  // level 6 (the uber gag) can never be bought here
constexpr int kInvBytes = gags::kTracks * gags::kLevels; // 49: InventoryBase.makeNetString

// What the AI knows about us (the owner-view fields we mirror from net::OwnStats).
struct Profile {
    uint16_t experience[gags::kTracks] = {};
    uint8_t trackAccess[gags::kTracks] = {};
    uint8_t maxCarry = 20;
};

using Inventory = uint8_t[gags::kTracks][gags::kLevels];

// InventoryBase.calcTotalProps / countPropsInList.
int countProps(const Inventory inv);
// Experience.getExpLevel: highest level index whose threshold `exp` reaches.
int expLevel(int track, uint16_t exp);
// InventoryBase.getMax: carry limit of one gag at our experience.
int carryMax(const Profile& p, int track, int level);

enum class Buy : uint8_t {
    Ok,
    NoMoney,      // PurchaseBase.handlePurchase: money <= 0 does nothing
    TooManyProps, // addItem == -2: total would exceed maxCarry
    TooManyOfGag, // addItem == -1: already at the per-gag limit
    NoSkill,      // addItem == 0: level not unlocked, or track not accessible
    NotForSale,   // uber level (validate rejects any increase)
};

// InventoryBase.addItem + PurchaseBase.handlePurchase for one more gag (inv, money).
Buy canBuy(const Profile& p, const Inventory inv, int money, int track, int level);

// The 49-byte InventoryBase.makeNetString of inv.
void encodeInventory(const Inventory inv, uint8_t out[kInvBytes]);

// InventoryBase.validatePurchase(newInventory, currentMoney, newMoney) as the AI runs it,
// for a full game-access (AccessFull) toon. `oldInv` is the AI's current inventory.
bool validatePurchase(const Profile& p, const Inventory oldInv, const Inventory newInv, int currentMoney,
                      int newMoney);

// Working copy while the panel is open: what we have now and what the cart would make it.
struct Cart {
    Inventory start = {};
    Inventory inv = {};
    int startMoney = 0;
    int money = 0;

    void reset(const Inventory current, int moneyNow);
    // One more of (track, level); false (with `why` set) when not allowed.
    bool add(const Profile& p, int track, int level, Buy* why = nullptr);
    // Take back a gag bought in this visit (refunds its jellybean); never goes below start.
    bool remove(int track, int level);
    int bought(int track, int level) const { return inv[track][level] - start[track][level]; }
    bool changed() const;
};

} // namespace tt::shop
