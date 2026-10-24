#include <cstdio>
#include <cmath>
#include "game/ice_logic.h"

int main() {
    float testDists[6] = {0.0f, 5.0f, 10.0f, 15.0f, 20.0f, tt::icegame::kFarthestLength};
    int playerCounts[3] = {1, 2, 4};

    for (int np : playerCounts) {
        for (int place = 0; place < np; ++place) {
            for (float d : testDists) {
                int pts = tt::icegame::calculatePoints(d, np, place);
                std::printf("score(n=%d, pl=%d, d=%.2f) = %d\n", np, place, d, pts);
            }
        }
    }

    return 0;
}
