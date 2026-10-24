#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ui/cchar_chatter_data.h"
#include "world/cchar.h"
#include "net/client.h"
#include "world/collision.h"

namespace tt::log {
void write(const char*, ...) {}
}

namespace tt::net {
const char* Client::avatarName(uint32_t) const { return nullptr; }
bool Client::avatarPos(uint32_t, float*, float*, float*) const { return false; }
const CChar* Client::findCChar(uint32_t) const { return nullptr; }
}

namespace tt::world {
float CollisionWorld::groundHeight(float, float, float zFrom, bool* found, float) const {
    if (found) *found = false;
    return zFrom;
}
}

namespace {

constexpr float kTolerance = 1e-4f;

bool nearlyEqual(float a, float b, float tol = kTolerance) {
    return fabsf(a - b) <= tol;
}

void test_chatter_lookup() {
    using namespace tt::ui::chatter;

    // Mickey default greeting 0: "Welcome to Toontown Central."
    const char* m0 = getChatterRaw(67, 0, kGreeting, 0);
    assert(m0 != nullptr);
    assert(!strcmp(m0, "Welcome to Toontown Central."));

    // Mickey default greeting 1: "Hi, my name is Mickey. What's yours?"
    const char* m1 = getChatterRaw(67, 0, kGreeting, 1);
    assert(m1 != nullptr);
    assert(!strcmp(m1, "Hi, my name is Mickey. What's yours?"));

    // Mickey default greeting 2: "Hi, %!"
    const char* m2 = getChatterRaw(67, 0, kGreeting, 2);
    assert(m2 != nullptr);
    assert(!strcmp(m2, "Hi, %!"));

    // Test % substitution
    char buf[160];
    bool ok = formatChatter(buf, sizeof(buf), 67, 0, kGreeting, 2, "Flippy");
    assert(ok);
    assert(!strcmp(buf, "Hi, Flippy!"));

    // Test substitution with no %
    ok = formatChatter(buf, sizeof(buf), 67, 0, kGreeting, 0, "Flippy");
    assert(ok);
    assert(!strcmp(buf, "Welcome to Toontown Central."));

    // Test substitution with null targetName
    ok = formatChatter(buf, sizeof(buf), 67, 0, kGreeting, 2, nullptr);
    assert(ok);
    assert(!strcmp(buf, "Hi, !"));

    // Test holiday overrides: Mickey with April Fools (29)
    const char* af0 = getChatterRaw(67, 29, kGreeting, 0);
    assert(af0 != nullptr);
    assert(!strcmp(af0, "Happy April Toons' Week!"));

    // Test character with no default chatter: Pluto (81)
    const char* p_def = getChatterRaw(81, 0, kGreeting, 0);
    assert(p_def == nullptr);
    ok = formatChatter(buf, sizeof(buf), 81, 0, kGreeting, 0, "Flippy");
    assert(!ok);

    // Pluto with April Fools (29)
    const char* p_af = getChatterRaw(81, 29, kGreeting, 0);
    assert(p_af != nullptr);
    assert(!strcmp(p_af, "Happy April Toons' Week!"));

    // DonaldDock (80) default vs April Fools
    assert(getChatterRaw(80, 0, kGreeting, 0) == nullptr);
    assert(getChatterRaw(80, 29, kGreeting, 0) != nullptr);

    // Out of bounds tests
    assert(getChatterRaw(67, 0, 3, 0) == nullptr);        // bad category
    assert(getChatterRaw(67, 0, kGreeting, 9999) == nullptr); // bad msgIndex
    assert(getChatterRaw(999, 0, kGreeting, 0) == nullptr);  // bad classId

    // By-name lookup
    const char* mn = getChatterRawByName("Minnie", 0, kComment, 0);
    assert(mn != nullptr);
    assert(!strcmp(mn, "The hills are alive with the sound of music!"));

    ok = formatChatterByName(buf, sizeof(buf), "Mickey", 0, kGreeting, 2, "Donald");
    assert(ok);
    assert(!strcmp(buf, "Hi, Donald!"));

    printf("PASS: test_chatter_lookup\n");
}

void test_segment_timing() {
    using namespace tt::world;

    // Test a 2-segment path: (17, -17, 4.025) -> (17.5, 7.6, 4.025)
    float pts[2][3] = {
        {17.0f, -17.0f, 4.025f},
        {17.5f, 7.6f, 4.025f}
    };
    CCharActors::Segment seg[4];
    int nSeg = CCharActors::buildSegments(seg, 4, pts, 2, 5.0f, 0.0f);
    assert(nSeg == 1);

    // Distance = sqrt(0.5^2 + 24.6^2) = 24.60508...
    // Speed = 5.0 -> duration = 4.921016...
    // dx = 0.5, dy = 24.6 -> dest = atan2(-0.5, 24.6) / degToRad = -1.1643...
    // turnTime = |-1.1643| / 270 = 0.00431...
    // duration - turnTime = 4.9167 > 0.01
    // moveTime = 4.9167..., length = max(turnTime, moveTime) = 4.9167...
    assert(nearlyEqual(seg[0].turnTime, 0.004312f, 1e-4f));
    assert(nearlyEqual(seg[0].moveTime, 4.916704f, 1e-3f));
    assert(nearlyEqual(seg[0].length, 4.916704f, 1e-3f));
    assert(nearlyEqual(seg[0].h0, 0.0f, 1e-4f));
    assert(nearlyEqual(seg[0].h1, -1.16436f, 1e-3f));

    printf("PASS: test_segment_timing\n");
}

void test_dale_orbit() {
    using namespace tt::world;

    // Leg a -> b from dale_6000.paths:
    // pts[0] = (50.004, 102.725, 0.600)
    // pts[-1] = (-29.552, 112.531, 0.600)
    // chip_dist = 80.1581
    // src_offset = (3.0, 0.0) -> src_theta = 0
    // dst_offset = (-1.5, 2.0) -> dst_theta = 2.214297 (126.87 deg)
    // revs = 80.1581 / 13.0 = 6.166... + ... = 6.35237
    // chip_src = (50.004 - 3, 102.725, 0.600) = (47.004, 102.725, 0.600)
    // chip_dst = (-29.552 - (-1.5), 112.531 - 2.0, 0.600) = (-28.052, 110.531, 0.600)
    float chipSrc[3] = {47.0040016f, 102.7249985f, 0.6f};
    float chipDst[3] = {-28.0520000f, 110.5309982f, 0.6f};
    float srcTheta = 0.0f;
    float destTheta = 2.2142974f;
    float revs = 6.352375f;
    float radius = 3.0f;
    float duration = 26.719f;

    // At t = 0.0: Dale should be at pts[0] = (50.004, 102.725, 0.600) and heading 0
    auto o0 = CCharActors::computeDaleOrbit(0.0f, duration, chipSrc, chipDst, srcTheta, destTheta, revs, radius);
    assert(nearlyEqual(o0.x, 50.0040016f, 1e-3f));
    assert(nearlyEqual(o0.y, 102.7249985f, 1e-3f));
    assert(nearlyEqual(o0.z, 0.6f, 1e-3f));
    assert(nearlyEqual(o0.h, 0.0f, 1e-3f));

    // At t = duration: Dale should be at chipDst + (cos(destTheta)*3, sin(destTheta)*3)
    // = (-28.052 - 1.8, 110.531 + 2.4, 0.6) = (-29.852, 112.931, 0.6)
    // and heading 126.87 deg
    auto o1 = CCharActors::computeDaleOrbit(duration, duration, chipSrc, chipDst, srcTheta, destTheta, revs, radius);
    assert(nearlyEqual(o1.x, -29.852f, 1e-2f));
    assert(nearlyEqual(o1.y, 112.931f, 1e-2f));
    assert(nearlyEqual(o1.z, 0.6f, 1e-3f));
    assert(nearlyEqual(o1.h, 126.87f, 0.1f));

    printf("PASS: test_dale_orbit\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && !strcmp(argv[1], "--cli-timing")) {
        // Read: speed, initial_h, nPoints, then x y z per point
        float speed = 0, initialH = 0;
        int n = 0;
        if (scanf("%f %f %d", &speed, &initialH, &n) != 3 || n < 2 || n > 64) return 1;
        float pts[64][3];
        for (int i = 0; i < n; ++i) {
            if (scanf("%f %f %f", &pts[i][0], &pts[i][1], &pts[i][2]) != 3) return 1;
        }
        tt::world::CCharActors::Segment seg[64];
        int nSeg = tt::world::CCharActors::buildSegments(seg, 64, pts, n, speed, initialH);
        printf("%d\n", nSeg);
        for (int i = 0; i < nSeg; ++i) {
            printf("%.6f %.6f %.6f %.6f %.6f\n",
                   seg[i].turnTime, seg[i].moveTime, seg[i].length, seg[i].h0, seg[i].h1);
        }
        return 0;
    }

    if (argc >= 2 && !strcmp(argv[1], "--cli-dale")) {
        // Read: t, duration, srcX srcY srcZ, dstX dstY dstZ, srcTheta, destTheta, revs, radius
        float t = 0, duration = 0, src[3], dst[3], srcTheta = 0, destTheta = 0, revs = 0, radius = 0;
        if (scanf("%f %f %f %f %f %f %f %f %f %f %f %f",
                  &t, &duration, &src[0], &src[1], &src[2], &dst[0], &dst[1], &dst[2],
                  &srcTheta, &destTheta, &revs, &radius) != 12) return 1;
        auto orb = tt::world::CCharActors::computeDaleOrbit(t, duration, src, dst, srcTheta, destTheta, revs, radius);
        printf("%.6f %.6f %.6f %.6f\n", orb.x, orb.y, orb.z, orb.h);
        return 0;
    }

    test_chatter_lookup();
    test_segment_timing();
    test_dale_orbit();
    printf("All host tests passed successfully.\n");
    return 0;
}
