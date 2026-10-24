#include <cstdio>
#include "game/cog_thief_logic.h"

int main() {
    float times[] = {0.0f, 10.0f, 25.5f, 45.0f, 59.9f, 60.0f};
    int stolens[] = {0, 1, 4};
    for (float t : times) {
        for (int stolen : stolens) {
            int res = tt::cogthief::evaluateFinalScore(t, stolen);
            std::printf("t=%.1f stolen=%d -> %d\n", t, stolen, res);
        }
    }
    return 0;
}
