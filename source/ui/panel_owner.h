#pragma once
#include <cstdint>
namespace tt::ui {

enum class PanelOwner : uint8_t { Tabs, Battle, Purchase, Info, Talk, Clerk, Shop, Tailor, Closet, Fishing, Book };
struct PanelRequests {
    bool battle = false, purchase = false, info = false, talk = false, clerk = false;
    bool shop = false, tailor = false, closet = false, fishing = false, book = false;
};
inline PanelOwner choosePanel(const PanelRequests& r) {
    if (r.battle) return PanelOwner::Battle;
    if (r.purchase) return PanelOwner::Purchase;
    if (r.info) return PanelOwner::Info;
    if (r.talk) return PanelOwner::Talk;
    if (r.clerk) return PanelOwner::Clerk;
    if (r.shop) return PanelOwner::Shop;
    if (r.tailor) return PanelOwner::Tailor;
    if (r.closet) return PanelOwner::Closet;
    if (r.fishing) return PanelOwner::Fishing;
    if (r.book) return PanelOwner::Book;
    return PanelOwner::Tabs;
}
inline const char* panelName(PanelOwner owner) {
    constexpr const char* kNames[] = {"tabs", "battle", "purchase", "trolley/minigame", "quest",
                                    "clerk", "shop", "tailor", "closet", "fishing", "book"};
    return kNames[unsigned(owner)];
}
} // namespace tt::ui
