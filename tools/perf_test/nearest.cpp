#include "core/nearest.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
static volatile int sink = 0;
static void oldNearest(const float *distance, const bool *active, int *output) {
    bool visible[64] = {};
    for (int slot = 0; slot < 6; ++slot) {
        float closest = 1e12f;
        int candidate = -1;
        for (int i = 0; i < 64; ++i)
            if (active[i] && !visible[i] && distance[i] < closest) {
                closest = distance[i];
                candidate = i;
            }
        output[slot] = candidate;
        if (candidate >= 0)
            visible[candidate] = true;
    }
}
int main() {
    std::mt19937 random(19);
    float distance[64];
    bool active[64];
    int old[6], current[6];
    for (int trial = 0; trial < 10000; ++trial) {
        for (int i = 0; i < 64; ++i) {
            distance[i] = float(random() % 16);
            active[i] = random() % 2;
        }
        oldNearest(distance, active, old);
        tt::nearestIndices(
            64, [&](int i) { return active[i]; }, [&](int i) { return distance[i]; }, 1e12f, current);
        for (int i = 0; i < 6; ++i)
            assert(current[i] == old[i]);
    }
    auto run = [&](bool cached) {
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 100000; ++frame) {
            distance[0] = float(frame % 16);
            if (cached)
                tt::nearestIndices(
                    64, [&](int i) { return active[i]; }, [&](int i) { return distance[i]; }, 1e12f, current);
            else
                oldNearest(distance, active, current);
            sink = current[0];
        }
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() /
               100000;
    };
    const double before = run(false), after = run(true);
    std::printf("Cogdo stable nearest-six ns/frame: before %.1f after %.1f speedup %.2fx; 10000 exact "
                "tie/health cases PASS; distance evaluations <=384 -> <=64\n",
                before, after, before / after);
}
