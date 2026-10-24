#include "ui/shop_rules.h"

#include <cstring>

namespace tt::shop {

int countProps(const Inventory inv) {
    int n = 0;
    for (int t = 0; t < gags::kTracks; t++)
        for (int l = 0; l < kRegularLevels; l++) n += inv[t][l];
    return n;
}

int expLevel(int track, uint16_t exp) {
    int level = 0;
    for (int l = 0; l < gags::kLevels; l++)
        if (exp >= gags::kLevelExp[track][l]) level = l;
    return level;
}

int carryMax(const Profile& p, int track, int level) {
    return gags::kCarry[track][expLevel(track, p.experience[track])][level];
}

Buy canBuy(const Profile& p, const Inventory inv, int money, int track, int level) {
    if (money <= 0) return Buy::NoMoney;
    if (level > kRegularLevels - 1) return Buy::NotForSale;
    if (expLevel(track, p.experience[track]) < level || !p.trackAccess[track]) return Buy::NoSkill;
    if (inv[track][level] > carryMax(p, track, level) - 1) return Buy::TooManyOfGag;
    if (countProps(inv) + 1 > p.maxCarry) return Buy::TooManyProps;
    return Buy::Ok;
}

void encodeInventory(const Inventory inv, uint8_t out[kInvBytes]) {
    for (int i = 0; i < kInvBytes; i++) out[i] = inv[i / gags::kLevels][i % gags::kLevels];
}

bool validatePurchase(const Profile& p, const Inventory oldInv, const Inventory newInv, int currentMoney,
                      int newMoney) {
    if (newMoney > currentMoney) return false; // "Somebody lied about their money!"
    const int newTotal = countProps(newInv);
    const int oldTotal = countProps(oldInv);
    if (newTotal > oldTotal + currentMoney) return false; // overspent
    if (newTotal - oldTotal > currentMoney - newMoney) return false; // too many items for the money spent
    if (newTotal > p.maxCarry) return false;
    // validateItemsBasedOnExp (note: it checks every count, not only the changed ones).
    for (int t = 0; t < gags::kTracks; t++) {
        for (int l = 0; l < gags::kLevels; l++) {
            if (newInv[t][l] > carryMax(p, t, l)) return false;
            if (newInv[t][l] > 0 && !p.trackAccess[t]) return false;
            if (l > kRegularLevels - 1 && newInv[t][l] > oldInv[t][l]) return false;
        }
    }
    return true;
}

void Cart::reset(const Inventory current, int moneyNow) {
    memcpy(start, current, sizeof(start));
    memcpy(inv, current, sizeof(inv));
    startMoney = moneyNow;
    money = moneyNow;
}

bool Cart::add(const Profile& p, int track, int level, Buy* why) {
    const Buy b = canBuy(p, inv, money, track, level);
    if (why) *why = b;
    if (b != Buy::Ok) return false;
    inv[track][level]++;
    money--;
    return true;
}

bool Cart::remove(int track, int level) {
    if (bought(track, level) <= 0) return false;
    inv[track][level]--;
    money++;
    return true;
}

bool Cart::changed() const { return memcmp(start, inv, sizeof(start)) != 0; }

} // namespace tt::shop
