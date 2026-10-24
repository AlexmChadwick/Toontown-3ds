#include <cstdio>
#include "game/tug_logic.h"

using namespace tt::game::tuggame;

int main() {
    for (int i = 0; i < kNumTargetRates; ++i) {
        const float ideal = kTargetRates[i].idealRate;
        for (int rate = 0; rate < 16; ++rate) {
            const float awards[] = {0.0f, 0.3f, 0.9f};
            for (float award : awards) {
                const float f = computeForce(rate, ideal, 1.0f, award);
                printf("force ideal=%.1f rate=%d award=%.1f F=%.4f\n", ideal, rate, award, f);
            }
        }
    }

    // Test decay step matching python
    float ttl[4] = {1.0f, 0.8f, 0.5f, 0.2f};
    int count = 4;
    for (int step = 0; step < 5; ++step) {
        int nextCount = 0;
        for (int i = 0; i < count; ++i) {
            float rem = ttl[i] - 0.1f;
            if (rem > 0.0f) {
                ttl[nextCount++] = rem;
            }
        }
        count = nextCount;
        printf("decay step=%d count=%d\n", step, count);
    }

    return 0;
}
