#include "ui/panel_geometry.h"
#include <cassert>
#include <limits>
int main() {
    tt::ui::MapRect rect{4, 4, 312, 136};
    for (int radius : {3, 11}) {
        for (int x = -20; x <= 340; ++x) {
            for (int y = -20; y <= 240; ++y) {
                float px = float(x), py = float(y);
                if (rect.marker(&px, &py, float(radius))) {
                    assert(px - radius >= 4 && px + radius <= 316);
                    assert(py - radius >= 4 && py + radius <= 140);
                }
            }
        }
    }
    float x = std::numeric_limits<float>::quiet_NaN(), y = 4;
    assert(!rect.marker(&x, &y, 3));
    assert(tt::ui::hpFraction(100, 10) == 1);
    assert(tt::ui::hpFraction(-1, 10) == 0);
    assert(tt::ui::hpFraction(1, 0) == 0);
    assert(tt::ui::hpFraction(5, 10) == .5f);
}
