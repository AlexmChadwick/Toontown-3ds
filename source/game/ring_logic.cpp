#include "game/ring_logic.h"

#include <cmath>

#include "game/ring_tables.h"

namespace tt::game::ringgame {

namespace {
constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

double wrap1(double t) { // Python's % 1.0
    t = std::fmod(t, 1.0);
    return t < 0.0 ? t + 1.0 : t;
}

// RingTracks.ringClerp between a and b, then back (the two half-length actions).
void clerpThere(double t, double ax, double ay, double bx, double by, double* x, double* z) {
    double u;
    if (t < 0.5) {
        u = t / 0.5;
    } else {
        u = (t - 0.5) / 0.5;
        double tx = ax, ty = ay;
        ax = bx, ay = by, bx = tx, by = ty;
    }
    u = u * u * (3.0 - 2.0 * u);
    *x = ax * (1.0 - u) + bx * u;
    *z = ay * (1.0 - u) + by * u;
}

constexpr double kRingOffset = 0.4; // RingTracks.RingOffset

enum Func : uint8_t {
    Keypad, EvenCircle, FollowCircle, EvenCircleStationary, VerticalSlots, HorizontalSlots, Plus,
    VertInfinity, HorizInfinity, EvenCircleStationaryFaster, PlusFaster
};
// RingTrackGroups.allFuncs per difficulty; dontUseFuncs[0] drops the multi-ring ones for one ring.
const Func kAllFuncs[3][6] = {{Keypad},
                              {EvenCircle, FollowCircle, EvenCircleStationary, VerticalSlots, HorizontalSlots, Plus},
                              {VertInfinity, HorizInfinity, EvenCircleStationaryFaster, PlusFaster}};
const int kNumFuncs[3] = {1, 6, 4};
bool multiRingOnly(Func f) {
    return f == FollowCircle || f == EvenCircleStationary || f == EvenCircleStationaryFaster || f == Plus ||
           f == PlusFaster;
}

void evenCircleWithStationaryCenter(int n, RandomNumGen& rng, RingTrackGroup* g) {
    const int numCenter = rng.randint(1, n - 1);
    // getTightCircleStaticPositions
    for (int i = 0; i < numCenter; ++i) {
        RingTrack& t = g->tracks[i];
        t.kind = RingTrack::Static;
        if (numCenter == 1) {
            t.a = t.b = 0.0;
        } else {
            const double step = kTwoPi / double(numCenter);
            const double angle = i * step + step / 2.0;
            t.a = std::sin(angle) / 3.0;
            t.b = std::cos(angle) / 3.0;
        }
        g->trackTOffsets[i] = 0.0;
    }
    const int numOuter = n - numCenter;
    for (int i = 0; i < numOuter; ++i) {
        g->tracks[numCenter + i].kind = RingTrack::Circle;
        g->trackTOffsets[numCenter + i] = double(i) / numOuter;
    }
    g->period = 6.0;
}
} // namespace

void RingTrack::eval(double t, double* x, double* z) const {
    switch (kind) {
    case Static: *x = a, *z = b; return;
    case Circle: *x = std::sin(t * kTwoPi), *z = std::cos(t * kTwoPi); return;
    case VertInfinity: *x = 0.5 * std::sin(2.0 * t * kTwoPi), *z = std::cos(t * kTwoPi); return;
    case HorizInfinity: *x = std::sin(t * kTwoPi), *z = 0.5 * std::sin(2.0 * t * kTwoPi); return;
    case VertSlot: clerpThere(t, a, 1.0, a, -1.0, x, z); return;
    case HorizSlot: clerpThere(t, 1.0, a, -1.0, a, x, z); return;
    case PlusUp: clerpThere(t, 0.0, kRingOffset, 0.0, 1.0, x, z); return;
    case PlusDown: clerpThere(t, 0.0, -kRingOffset, 0.0, -1.0, x, z); return;
    case PlusRight: clerpThere(t, kRingOffset, 0.0, 1.0, 0.0, x, z); return;
    case PlusLeft: clerpThere(t, -kRingOffset, 0.0, -1.0, 0.0, x, z); return;
    }
}

void RingTrackGroup::ringPos(int ring, double t, float* x, float* z) const {
    double normT = wrap1(t / period + tOffset);
    if (reverse) normT = 1.0 - normT;
    double px, pz;
    tracks[ring].eval(wrap1(normT + trackTOffsets[ring]), &px, &pz);
    *x = float(px * kMaxToonXZ);
    *z = float(pz * kMaxToonXZ);
}

void getRandomRingTrackGroup(int type, int numRings, RandomNumGen& rng, RingTrackGroup* g) {
    const int n = numRings < 1 ? 1 : numRings > kMaxRings ? kMaxRings : numRings;
    if (type < 0 || type > 2) type = 0;
    Func table[6];
    int count = 0;
    for (int i = 0; i < kNumFuncs[type]; ++i)
        if (n > 1 || !multiRingOnly(kAllFuncs[type][i])) table[count++] = kAllFuncs[type][i];
    const Func func = table[rng.choice(count)];
    *g = RingTrackGroup{};
    g->numRings = n;
    for (int i = 0; i < n; ++i) g->trackTOffsets[i] = 0.0;
    switch (func) {
    case Keypad: {
        static const double kPositions[9][2] = {{0, 0}, {0, 1}, {0, -1}, {-1, 0}, {1, 0},
                                                {-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
        const double posScale = 0.7 + rng.random() * 0.2;
        int used[kMaxRings];
        for (int i = 0; i < n; ++i) {
            int pos;
            bool taken;
            do {
                pos = rng.choice(9);
                taken = false;
                for (int k = 0; k < i; ++k) taken |= used[k] == pos;
            } while (taken);
            used[i] = pos;
            g->tracks[i].kind = RingTrack::Static;
            g->tracks[i].a = kPositions[pos][0] * posScale;
            g->tracks[i].b = kPositions[pos][1] * posScale;
        }
        g->period = 1.0;
        break;
    }
    case EvenCircle:
    case FollowCircle:
        for (int i = 0; i < n; ++i) {
            g->tracks[i].kind = RingTrack::Circle;
            g->trackTOffsets[i] = func == EvenCircle ? double(i) / n : double(i) * 0.12;
        }
        g->period = 6.0;
        break;
    case EvenCircleStationary:
    case EvenCircleStationaryFaster:
        evenCircleWithStationaryCenter(n, rng, g);
        if (func == EvenCircleStationaryFaster) g->period = 5.0;
        break;
    case VerticalSlots:
    case HorizontalSlots: {
        // __get_Slots: lineupPos(i, n, 2/3), shifted by a random offset.
        double fp[kMaxRings];
        for (int i = 0; i < n; ++i) fp[i] = double(i) * (2.0 / 3) - ((2.0 / 3) * (n - 1)) / 2.0;
        double offset = 1.0 - fp[n - 1];
        offset = rng.random() * (offset * 2) - offset;
        for (int i = 0; i < n; ++i) {
            g->tracks[i].kind = func == VerticalSlots ? RingTrack::VertSlot : RingTrack::HorizSlot;
            g->tracks[i].a = fp[i] + offset;
            g->trackTOffsets[i] = double(i) / n * 0.5;
        }
        g->period = 6.0;
        break;
    }
    case Plus:
    case PlusFaster: {
        using K = RingTrack::Kind;
        static const K k2[2][2] = {{RingTrack::PlusUp, RingTrack::PlusDown}, {RingTrack::PlusLeft, RingTrack::PlusRight}};
        static const K k3[4][3] = {{RingTrack::PlusUp, RingTrack::PlusLeft, RingTrack::PlusRight},
                                   {RingTrack::PlusLeft, RingTrack::PlusUp, RingTrack::PlusDown},
                                   {RingTrack::PlusDown, RingTrack::PlusLeft, RingTrack::PlusRight},
                                   {RingTrack::PlusRight, RingTrack::PlusUp, RingTrack::PlusDown}};
        static const K k4[4] = {RingTrack::PlusUp, RingTrack::PlusDown, RingTrack::PlusLeft, RingTrack::PlusRight};
        const K* set = n == 2 ? k2[rng.choice(2)] : n == 3 ? k3[rng.choice(4)] : (rng.choice(1), k4);
        for (int i = 0; i < n; ++i) g->tracks[i].kind = set[i];
        g->period = func == Plus ? 4.0 : 2.5;
        break;
    }
    case VertInfinity:
    case HorizInfinity: {
        static const double kOffsets[4][4] = {{0.0}, {0.0, 0.75}, {0.0, 1.0 / 3.0, 2.0 / 3.0},
                                              {0.0, std::fmod(14.0 / 23.0, 1.0), std::fmod(28.0 / 23.0, 1.0),
                                               std::fmod(42.0 / 23.0, 1.0)}};
        for (int i = 0; i < n; ++i) {
            g->tracks[i].kind = func == VertInfinity ? RingTrack::VertInfinity : RingTrack::HorizInfinity;
            g->trackTOffsets[i] = kOffsets[n - 1][i];
        }
        g->period = 5.0;
        break;
    }
    }
    // __scramble: draw indices until every track is taken.
    RingTrack tracks[kMaxRings];
    double offsets[kMaxRings];
    bool usedSlot[kMaxRings] = {};
    for (int count2 = 0; count2 < n;) {
        const int i = rng.randint(0, n - 1);
        if (usedSlot[i]) continue;
        usedSlot[i] = true;
        tracks[count2] = g->tracks[i];
        offsets[count2] = g->trackTOffsets[i];
        ++count2;
    }
    for (int i = 0; i < n; ++i) g->tracks[i] = tracks[i], g->trackTOffsets[i] = offsets[i];
    g->reverse = rng.choice(2) != 0;
    g->tOffset = rng.random();
}

void getDifficultyPattern(uint32_t hood, RandomNumGen& rng, int* outPattern) {
    int h = 1; // 2000
    for (int i = 0; i < kPatternHoods; ++i)
        if (kPatternHood[i] == hood) h = i;
    const int choice = rng.choice(kPatternCount[h]);
    for (int i = 0; i < kNumRingGroups; ++i) outPattern[i] = kPatterns[h][choice][i];
}

bool checkCollision(float toonX, float toonZ, float ringX, float ringZ) {
    const double dx = double(toonX) - double(ringX);
    const double dz = double(toonZ) - double(ringZ);
    const double d2 = dx * dx + dz * dz;
    return d2 <= double(kRingRadiusSqrd);
}

int calcScore(int ringsCollected, bool perfect, int numPlayers) {
    int score = ringsCollected;
    if (perfect) {
        int bonus = 5;
        if (numPlayers == 3) bonus = 10;
        else if (numPlayers >= 4) bonus = 18;
        score += bonus;
    }
    return score > 1 ? score : 1;
}

void generateRings(uint32_t hood, int numRings, RandomNumGen& rng, RingTrackGroup* outGroups) {
    int pattern[kNumRingGroups];
    getDifficultyPattern(hood, rng, pattern);
    for (int i = 0; i < kNumRingGroups; ++i) getRandomRingTrackGroup(pattern[i], numRings, rng, &outGroups[i]);
}

} // namespace tt::game::ringgame
