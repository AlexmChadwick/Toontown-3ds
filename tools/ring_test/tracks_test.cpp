// Host test: ring_logic RingTrackGroups / RingGroup positions vs the stock modules (tracks_ref.py).
#include <cstdio>
#include "game/ring_logic.h"
using namespace tt::game;
int main() {
    const unsigned hoods[] = {2000, 1000, 5000, 4000, 3000, 9000};
    const unsigned seeds[] = {12345, 67890, 1000123, 77};
    const double ts[] = {0.0, 1.3, 2.7, 4.1, 9.9};
    for (unsigned h : hoods) for (unsigned s : seeds) for (int n = 1; n <= 4; ++n) {
        RandomNumGen rng(s);
        ringgame::RingTrackGroup g[16];
        ringgame::generateRings(h, n, rng, g);
        for (int i = 0; i < 16; ++i) for (int r = 0; r < n; ++r) {
            printf("h%u s%u n%d g%d r%d:", h, s, n, i, r);
            for (double t : ts) { float x, z; g[i].ringPos(r, t, &x, &z); printf(" %.3f,%.3f", x, z); }
            printf("\n");
        }
    }
}
