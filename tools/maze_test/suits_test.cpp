// Host test: maze_logic MazeSuit tile walk vs the stock MazeSuit (suits_ref.py).
#include <cstdio>
#include "game/maze_logic.h"
using namespace tt::game;
// Same order as MazeGame::init: start shuffle, toon RNGs, period shuffle, suits.
int main() {
    const unsigned seeds[] = {12345, 4000123};
    const unsigned hoods[] = {2000, 9000};
    for (unsigned seed : seeds) for (unsigned hood : hoods) for (int np = 1; np <= 4; ++np) {
        maze::setMaze(np);
        RandomNumGen rng(seed);
        int starts[4] = {0, 1, 2, 3};
        rng.shuffle(starts, 4);
        RandomNumGen toon[4];
        for (int i = 0; i < np; ++i) toon[i].reseedFrom(rng);
        double diff = hood == 2000 ? 0.0 : 1.0;
        int hi = 0; for (int i = 0; i < maze::kSuitHoods; ++i) if (maze::kSuitHood[i] == hood) hi = i;
        int ns = 4 * np, half = ns / 2, row = ns / 4 - 1;
        const uint8_t* slower = maze::kSuitPeriods[diff < 0.5 ? 1 : 0][hi][row];
        const uint8_t* faster = maze::kSuitPeriods[3][hi][row];
        int per[16]; for (int i = 0; i < half; ++i) per[i] = slower[i], per[half + i] = faster[i];
        rng.shuffle(per, ns);
        static maze::Suit suits[16];
        for (int i = 0; i < ns; ++i) { suits[i].init(i, rng, per[i], diff); suits[i].onstage(); suits[i].gameStart(); }
        printf("s%u h%u n%d starts %d %d %d %d\n", seed, hood, np, starts[0], starts[1], starts[2], starts[3]);
        for (int step = 1; step <= 60; ++step) {
            const float t = step * 0.37f;
            maze::thinkSuits(suits, ns, t);
            if (step % 10 == 0) {
                printf("s%u h%u n%d t%d:", seed, hood, np, step);
                for (int i = 0; i < ns; ++i) printf(" %d,%d,%d", suits[i].tileX(), suits[i].tileY(), suits[i].direction());
                printf("\n");
            }
        }
    }
}
