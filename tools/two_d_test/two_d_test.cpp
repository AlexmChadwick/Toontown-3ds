#include <cstdio>
#include "game/two_d_logic.h"

int main() {
    struct Case {
        int raw;
        float tl;
        bool comp;
    };
    Case cases[] = {
        {0, 0.0f, false},
        {5, 0.0f, false},
        {10, 50.0f, true},
        {25, 80.0f, true},
        {0, 100.0f, true},
        {-5, 0.0f, false},
        {15, 30.5f, true}
    };

    for (const auto& c : cases) {
        int score = c.raw;
        if (c.comp && c.tl > 0.0f) {
            score += tt::twodgame::victoryBonus(2000, c.tl);
        }
        int jb = tt::twodgame::jellybeans(score);
        std::printf("raw=%d tl=%.1f comp=%s -> %d\n", c.raw, c.tl, c.comp ? "True" : "False", jb);
    }
    return 0;
}
