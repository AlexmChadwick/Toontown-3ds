// Host driver for tools/shop_test/run_test.py: reads one command per line on stdin and prints
// what source/ui/shop_rules.cpp makes of it. Numbers are decimal, whitespace separated.
//
//   P = e0..e6 (experience) a0..a6 (track access) maxCarry
//   validate P old[49] new[49] currentMoney newMoney   -> "<0|1> <new as hex>"
//   buy      P inv[49] money track level               -> ok|nomoney|toomanyprops|toomanygag|noskill|notforsale
//   cart     P inv[49] money nops (b|s track level)*   -> "<money> <inv hex> <valid 0|1>"
//                    b = Cart::add, s = Cart::remove, valid = validatePurchase(start, end)
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "ui/shop_rules.h"

using namespace tt;

namespace {

bool readProfile(std::istringstream& in, shop::Profile* p) {
    for (int t = 0; t < 7; t++) {
        int v;
        if (!(in >> v)) return false;
        p->experience[t] = uint16_t(v);
    }
    for (int t = 0; t < 7; t++) {
        int v;
        if (!(in >> v)) return false;
        p->trackAccess[t] = uint8_t(v);
    }
    int mc;
    if (!(in >> mc)) return false;
    p->maxCarry = uint8_t(mc);
    return true;
}

bool readInv(std::istringstream& in, shop::Inventory inv) {
    for (int i = 0; i < 49; i++) {
        int v;
        if (!(in >> v)) return false;
        inv[i / 7][i % 7] = uint8_t(v);
    }
    return true;
}

std::string hex(const shop::Inventory inv) {
    uint8_t b[shop::kInvBytes];
    shop::encodeInventory(inv, b);
    static const char* k = "0123456789abcdef";
    std::string s;
    for (uint8_t c : b) {
        s += k[c >> 4];
        s += k[c & 15];
    }
    return s;
}

const char* name(shop::Buy b) {
    switch (b) {
    case shop::Buy::Ok: return "ok";
    case shop::Buy::NoMoney: return "nomoney";
    case shop::Buy::TooManyProps: return "toomanyprops";
    case shop::Buy::TooManyOfGag: return "toomanygag";
    case shop::Buy::NoSkill: return "noskill";
    case shop::Buy::NotForSale: return "notforsale";
    }
    return "?";
}

} // namespace

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        shop::Profile p;
        if (!readProfile(in, &p)) {
            puts("error");
            continue;
        }
        if (cmd == "validate") {
            shop::Inventory oldInv = {}, newInv = {};
            int cur = 0, nw = 0;
            if (!readInv(in, oldInv) || !readInv(in, newInv) || !(in >> cur >> nw)) {
                puts("error");
                continue;
            }
            printf("%d %s\n", shop::validatePurchase(p, oldInv, newInv, cur, nw) ? 1 : 0, hex(newInv).c_str());
        } else if (cmd == "buy") {
            shop::Inventory inv = {};
            int money = 0, t = 0, l = 0;
            if (!readInv(in, inv) || !(in >> money >> t >> l)) {
                puts("error");
                continue;
            }
            puts(name(shop::canBuy(p, inv, money, t, l)));
        } else if (cmd == "cart") {
            shop::Inventory inv = {};
            int money = 0, n = 0;
            if (!readInv(in, inv) || !(in >> money >> n)) {
                puts("error");
                continue;
            }
            shop::Cart c;
            c.reset(inv, money);
            for (int i = 0; i < n; i++) {
                std::string op;
                int t, l;
                in >> op >> t >> l;
                if (op == "b") c.add(p, t, l);
                else c.remove(t, l);
            }
            printf("%d %s %d\n", c.money, hex(c.inv).c_str(),
                   shop::validatePurchase(p, c.start, c.inv, c.startMoney, c.money) ? 1 : 0);
        } else {
            puts("error");
        }
    }
    return 0;
}
