// Host test for battle fx: projectile arcs, movie timing tables, splat variants, and gag atlas.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "world/battle_fx.h"

namespace {

constexpr float kEps = 0.001f;

bool approx(float a, float b, float eps = kEps) {
    return std::fabs(a - b) <= eps;
}

void testProjectileArc() {
    printf("Testing projectile arc math...\n");

    tt::world::ProjectileArc arc;
    arc.x0 = 0.0f; arc.y0 = -6.0f; arc.z0 = 2.0f;
    arc.x1 = 0.0f; arc.y1 = 5.0f;  arc.z1 = 3.0f;
    arc.maxHeight = 3.0f;
    arc.startMs = 1000;
    arc.hitMs = 1300;

    // 1. Alpha clamping
    assert(approx(arc.alpha(900), 0.0f));
    assert(approx(arc.alpha(1000), 0.0f));
    assert(approx(arc.alpha(1150), 0.5f));
    assert(approx(arc.alpha(1300), 1.0f));
    assert(approx(arc.alpha(1400), 1.0f));

    // 2. Completion flag
    assert(!arc.done(900));
    assert(!arc.done(1000));
    assert(!arc.done(1150));
    assert(!arc.done(1299));
    assert(arc.done(1300));
    assert(arc.done(1400));

    // 3. Start position
    float x = 0, y = 0, z = 0, heading = 0, pitch = 0;
    arc.sample(1000, &x, &y, &z, &heading, &pitch);
    assert(approx(x, arc.x0));
    assert(approx(y, arc.y0));
    assert(approx(z, arc.z0));
    assert(pitch > 0.0f); // ascending at start

    // 4. Hit position
    arc.sample(1300, &x, &y, &z, &heading, &pitch);
    assert(approx(x, arc.x1));
    assert(approx(y, arc.y1));
    assert(approx(z, arc.z1));
    assert(pitch < 0.0f); // descending at hit

    // 5. Apex at midpoint (alpha = 0.5)
    arc.sample(1150, &x, &y, &z, &heading, &pitch);
    assert(approx(x, 0.5f * (arc.x0 + arc.x1)));
    assert(approx(y, 0.5f * (arc.y0 + arc.y1)));
    const float expectedZ = 0.5f * (arc.z0 + arc.z1) + arc.maxHeight;
    assert(approx(z, expectedZ));
    // At alpha=0.5 with equal z, pitch is 0. With z1-z0=1 over 11 units, slight offset:
    assert(std::fabs(pitch) < 10.0f);

    // 6. Tangent heading points along flight direction (+Y -> heading ~0 deg)
    assert(std::fabs(heading) < 1.0f);

    printf("  Projectile arc tests passed.\n");
}

void testMovieTimings() {
    printf("Testing movie timing tables...\n");

    // Throw (track 4): MovieThrow upstream timing (release 2.7s, hit 3.0s)
    const auto throwTiming = tt::world::getMovieTiming(4, 0);
    assert(throwTiming.releaseMs == 2700);
    assert(throwTiming.hitMs == 3000);
    assert(throwTiming.durMs >= 3300);
    assert(throwTiming.hitMs > throwTiming.releaseMs);
    assert(throwTiming.hitMs - throwTiming.releaseMs == 300);

    // Squirt (track 5): MovieSquirt upstream timing (spray 2.5s, hit 2.7s)
    const auto squirtTiming = tt::world::getMovieTiming(5, 0);
    assert(squirtTiming.releaseMs == 2500);
    assert(squirtTiming.hitMs == 2700);
    assert(squirtTiming.durMs >= 3000);
    assert(squirtTiming.hitMs > squirtTiming.releaseMs);
    assert(squirtTiming.hitMs - squirtTiming.releaseMs == 200);

    // Sound (track 3)
    const auto soundTiming = tt::world::getMovieTiming(3, 0);
    assert(soundTiming.releaseMs <= soundTiming.hitMs);
    assert(soundTiming.hitMs <= soundTiming.durMs);

    // Drop (track 6)
    const auto dropTiming = tt::world::getMovieTiming(6, 0);
    assert(dropTiming.releaseMs <= dropTiming.hitMs);
    assert(dropTiming.hitMs <= dropTiming.durMs);

    // Heal (track 0)
    const auto healTiming = tt::world::getMovieTiming(0, 0);
    assert(healTiming.releaseMs <= healTiming.hitMs);
    assert(healTiming.hitMs <= healTiming.durMs);

    // Cog attack (-1)
    const auto cogTiming = tt::world::getMovieTiming(-1, 0);
    assert(cogTiming.releaseMs == 1000);
    assert(cogTiming.hitMs == 1200);
    assert(cogTiming.durMs == 2200);

    // Fire is a stock special track, not a gag-table index.  It must survive
    // setMovie filtering and keep its long dismissal timing without reserving
    // a held-gag prop.
    static_assert(tt::world::hasBattleMovie(tt::world::kAttackFire));
    static_assert(tt::world::hasBattleMovie(tt::world::kAttackNpcSos));
    const auto fireTiming = tt::world::getMovieTiming(tt::world::kAttackFire, -1);
    assert(fireTiming.releaseMs == 2500);
    assert(fireTiming.hitMs == 6000);
    assert(fireTiming.durMs == 8500);

    printf("  Movie timing tests passed.\n");
}

void testSplatConfigs() {
    printf("Testing splat color and scale configurations...\n");

    // Tart (purple tint, scale 0.3)
    const auto tartCfg = tt::world::getSplatConfig(4, 0);
    assert(approx(tartCfg.r, 0.215686f));
    assert(approx(tartCfg.g, 0.156863f));
    assert(approx(tartCfg.b, 0.580392f));
    assert(approx(tartCfg.a, 1.0f));
    assert(approx(tartCfg.scale, 0.3f));

    // Fruit pie slice (purple tint, scale 0.5)
    const auto fruitCfg = tt::world::getSplatConfig(4, 1);
    assert(approx(fruitCfg.r, 0.215686f));
    assert(approx(fruitCfg.g, 0.156863f));
    assert(approx(fruitCfg.b, 0.580392f));
    assert(approx(fruitCfg.scale, 0.5f));

    // Cream pie slice (yellow tint, scale 0.5)
    const auto creamCfg = tt::world::getSplatConfig(4, 2);
    assert(approx(creamCfg.r, 0.980392f));
    assert(approx(creamCfg.g, 0.945098f));
    assert(approx(creamCfg.b, 0.094118f));
    assert(approx(creamCfg.scale, 0.5f));

    // Splash (light blue, scale 1.0)
    const auto splashCfg = tt::world::getSplatConfig(5, 0);
    assert(approx(splashCfg.r, 0.75f));
    assert(approx(splashCfg.g, 0.75f));
    assert(approx(splashCfg.b, 1.0f));
    assert(approx(splashCfg.scale, 1.0f));

    printf("  Splat configuration tests passed.\n");
}

void testGagAtlasSubtextures() {
    printf("Testing gag atlas subtexture tables...\n");

    for (int t = 0; t < 7; ++t) {
        for (int l = 0; l < 7; ++l) {
            const auto& st = tt::world::kGagSubTextures[t][l];
            assert(st.width == 28);
            assert(st.height == 28);

            // Valid UV range [0, 1]
            assert(st.left >= 0.0f && st.left <= 1.0f);
            assert(st.right >= 0.0f && st.right <= 1.0f);
            assert(st.bottom >= 0.0f && st.bottom <= 1.0f);
            assert(st.top >= 0.0f && st.top <= 1.0f);

            // Left < Right
            assert(st.left < st.right);
            // Top > Bottom in tex3ds unrotated subtexture
            assert(st.top > st.bottom);

            // Dimensions correspond to roughly 28/256
            const float du = st.right - st.left;
            const float dv = st.top - st.bottom;
            assert(approx(du, 28.0f / 256.0f, 0.01f));
            assert(approx(dv, 28.0f / 256.0f, 0.01f));
        }
    }

    printf("  Gag atlas subtexture tests passed (all 49 gags valid).\n");
}

} // namespace

int main() {
    printf("=== Battle Fx Host Test ===\n");
    testProjectileArc();
    testMovieTimings();
    testSplatConfigs();
    testGagAtlasSubtextures();
    printf("All battle fx host tests passed!\n");
    return 0;
}
