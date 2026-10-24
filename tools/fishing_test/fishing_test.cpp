// Host test for source/world/fish_logic.*:  tools/fishing_test/run.sh
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "world/fish_logic.h"

using namespace tt::world::fish;

static int failures = 0;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

int main() {
    // fishValue vs the formula evaluated by hand (Python FishGlobals.getValue, no holiday):
    // Balloon Fish (0, 0): rarity 1, 1..3 lb. 32 oz: 15*((0.2)^1.5 + (32*0.05/16)^1.1) = 15*(0.0894+0.1)^...
    const float rv = powf(0.2f * 1, 1.5f), wv = powf(0.05f / 16.0f * 32, 1.1f);
    CHECK(fishValue(0, 0, 32) == static_cast<int>(ceilf(15.0f * (rv + wv))), "value formula");
    CHECK(fishValue(0, 0, 32) >= 1 && fishValue(34, 0, 20 * 16) > fishValue(0, 0, 32), "value ordering");

    char buf[40];
    weightText(1 * 16 + 1, buf, sizeof buf);
    CHECK(!strcmp(buf, "1 lb. 1 oz."), "weight text '%s'", buf);
    weightText(35, buf, sizeof buf);
    CHECK(!strcmp(buf, "2 lbs. 3 oz."), "weight text '%s'", buf);

    CHECK(!strcmp(genusName(0), "Balloon Fish") && !strcmp(findSpecies(0, 1)->name, "Hot Air Balloon Fish"), "names");
    CHECK(kNumSpecies == 70 && kNumRods == 5, "table sizes");

    // Projectile: compare to a numeric integration of moveBobTask (dt = 1/1000).
    for (float power : {0.1f, 0.5f, 1.0f}) {
        const float water = -1.4f;
        const float vZero = power * kVZeroMax, ang = power * kAngleMaxDeg * 3.14159265f / 180.0f;
        float t = 0, y = 0;
        for (; t < 10.0f; t += 0.001f) {
            const float z = kBobStartZ + vZero * sinf(ang) * t - kGravity * t * t / 2.0f;
            y = kBobStartY + vZero * cosf(ang) * t;
            if (z < water) break;
        }
        const Landing l = castLanding(power, water);
        CHECK(fabsf(l.y - y) < 0.05f && fabsf(l.t - t) < 0.002f, "landing power %.1f: %.3f vs %.3f", double(power),
              double(l.y), double(y));
    }
    CHECK(castLanding(1.0f, -1.4f).y > 25.0f && castLanding(1.0f, -1.4f).y < 35.0f, "full cast reach");

    // Catch distribution at the TTC pond with the starter rod.
    Rng rng;
    rng.seed(12345);
    int fish = 0, bean = 0, boot = 0, none = 0;
    int rarityCount[11] = {};
    const int n = 200000;
    for (int i = 0; i < n; i++) {
        const Catch c = rollCatch(2000, 0, rng);
        switch (c.kind) {
        case CatchKind::Fish: {
            fish++;
            const Species* s = findSpecies(c.genus, c.species);
            CHECK(s != nullptr, "unknown species %d/%d", c.genus, c.species);
            if (s) {
                rarityCount[s->rarity]++;
                CHECK(c.weight >= s->minLb * 16 && c.weight <= s->maxLb * 16 && c.weight <= kRods[0].maxLb * 16,
                      "weight %d outside range for %s", c.weight, s->name);
                CHECK(c.value >= 1, "value");
            }
            break;
        }
        case CatchKind::Jellybeans:
            bean++;
            CHECK(c.beans == kRods[0].beans, "beans");
            break;
        case CatchKind::Boot: boot++; break;
        default: none++; break;
        }
    }
    printf("TTC rod 0: fish %.1f%% beans %.1f%% boot %.1f%% (no fish in list %.1f%%)\n", 100.0 * fish / n,
           100.0 * bean / n, 100.0 * boot / n, 100.0 * none / n);
    CHECK(fabs(100.0 * (fish + none) / n - 93.0) < 1.0, "fish roll ~93%%");
    CHECK(fabs(100.0 * bean / n - 1.0) < 0.3, "bean roll ~1%%");
    CHECK(fabs(100.0 * boot / n - 6.0) < 0.6, "boot roll ~6%%");
    CHECK(rarityCount[1] > rarityCount[2] && rarityCount[2] > rarityCount[5], "rarer fish are less common");

    CHECK(fabsf(waterLevel(2000) + 1.4f) < 1e-4f, "TTC water level");
    float cx, cy, r;
    CHECK(pondRegion(2000, &cx, &cy, &r) && cx == -81.0f && cy == 31.0f && r == 14.0f, "TTC pond region");
    CHECK(!pondRegion(7777, &cx, &cy, &r), "unknown pond");

    printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
