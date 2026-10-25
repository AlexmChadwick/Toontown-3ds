#include "ui/panel_owner.h"
#include <cassert>
#include <cstring>
int main() {
    using namespace tt::ui;
    for (unsigned mask = 0; mask < 1024; ++mask) {
        PanelRequests r;
        r.battle = mask & 1; r.purchase = mask & 2; r.info = mask & 4;
        r.talk = mask & 8; r.clerk = mask & 16; r.shop = mask & 32;
        r.tailor = mask & 64; r.closet = mask & 128; r.fishing = mask & 256; r.book = mask & 512;
        unsigned first = 0;
        while (first < 10 && !(mask & (1u << first))) ++first;
        assert(choosePanel(r) == (first == 10 ? PanelOwner::Tabs : PanelOwner(first + 1)));
        assert(std::strlen(panelName(choosePanel(r))));
    }
    PanelRequests stale;
    stale.fishing = stale.book = stale.info = true;
    assert(choosePanel(stale) == PanelOwner::Info);
    stale.info = false;
    assert(choosePanel(stale) == PanelOwner::Fishing);
    stale.fishing = false;
    assert(choosePanel(stale) == PanelOwner::Book);
}
