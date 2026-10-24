// Host test for source/world/suit_path.cpp: replays tools/suit_test/reference.py's queries
// (recorded from the real libtoontown) through SuitGraph::genPath / SuitLegList and compares
// paths exactly and leg lists / walk-leg positions to 1e-3. Also sanity-checks samplePose
// continuity across the whole timeline.
//
//   suit_test <zones dir with <branch>.tsp> <reference.txt>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "world/suit_path.h"

using namespace tt::world;

namespace {

int gFails = 0;

void fail(const char* what, int zone, int s, int e, int lo, int hi) {
    if (gFails++ < 20) fprintf(stderr, "FAIL %s: zone %d %d->%d min %d max %d\n", what, zone, s, e, lo, hi);
}

bool near(double a, double b, double tol = 1e-3) { return std::fabs(a - b) <= tol * (1.0 + std::fabs(b) * 1e-3); }

struct RefLeg {
    int type, zone, a, b, block;
    double start, legTime;
};
struct RefSample {
    int leg;
    double frac, x, y, z;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: suit_test <zones dir> <reference.txt>\n");
        return 2;
    }
    FILE* f = fopen(argv[2], "r");
    if (!f) {
        perror("reference");
        return 2;
    }
    std::map<int, std::unique_ptr<SuitGraph>> graphs;
    char line[8192];
    int queries = 0, found = 0, legsChecked = 0, samplesChecked = 0, posesChecked = 0;
    int zone = 0, s = 0, e = 0, lo = 0, hi = 0, n = 0;
    std::vector<int> refPath;
    std::vector<RefLeg> refLegs;
    std::vector<RefSample> refSamples;
    SuitGraph* g = nullptr;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'Q') {
            refPath.clear();
            refLegs.clear();
            refSamples.clear();
            char* p = line + 2;
            zone = static_cast<int>(strtol(p, &p, 10));
            s = static_cast<int>(strtol(p, &p, 10));
            e = static_cast<int>(strtol(p, &p, 10));
            lo = static_cast<int>(strtol(p, &p, 10));
            hi = static_cast<int>(strtol(p, &p, 10));
            n = static_cast<int>(strtol(p, &p, 10));
            for (int i = 0; i < n; i++) refPath.push_back(static_cast<int>(strtol(p, &p, 10)));
            if (!graphs.count(zone)) {
                char path[512];
                snprintf(path, sizeof(path), "%s/%d.tsp", argv[1], zone);
                auto sg = std::make_unique<SuitGraph>();
                if (!sg->load(path)) {
                    fprintf(stderr, "cannot load %s\n", path);
                    return 2;
                }
                graphs[zone] = std::move(sg);
            }
            g = graphs[zone].get();
        } else if (line[0] == 'L') {
            RefLeg l;
            sscanf(line + 2, "%d %lf %lf %d %d %d %d", &l.type, &l.start, &l.legTime, &l.zone, &l.a, &l.b, &l.block);
            refLegs.push_back(l);
        } else if (line[0] == 'W') {
            RefSample w;
            int type;
            sscanf(line + 2, "%d %lf %lf %lf %lf", &type, &w.frac, &w.x, &w.y, &w.z);
            w.leg = static_cast<int>(refLegs.size()) - 1;
            refSamples.push_back(w);
        } else if (line[0] == 'E') {
            queries++;
            uint16_t out[kMaxSuitPathPoints];
            const int got = g->genPath(static_cast<uint16_t>(s), static_cast<uint16_t>(e), lo, hi, out, kMaxSuitPathPoints);
            if (got != n) {
                fail("path length", zone, s, e, lo, hi);
                continue;
            }
            if (n == 0) continue;
            found++;
            bool bad = false;
            for (int i = 0; i < n && !bad; i++) bad = g->dnaIndex(out[i]) != refPath[i];
            if (bad) {
                fail("path points", zone, s, e, lo, hi);
                continue;
            }
            SuitLegList legs;
            if (!legs.build(*g, out, n)) {
                fail("leg build", zone, s, e, lo, hi);
                continue;
            }
            if (legs.numLegs() != static_cast<int>(refLegs.size())) {
                fail("leg count", zone, s, e, lo, hi);
                continue;
            }
            for (int i = 0; i < legs.numLegs() && !bad; i++) {
                const SuitLeg& l = legs.leg(i);
                const RefLeg& r = refLegs[i];
                legsChecked++;
                if (l.type != r.type || l.zone != r.zone || g->dnaIndex(l.a) != r.a || g->dnaIndex(l.b) != r.b ||
                    l.block != r.block || !near(l.startTime, r.start) || !near(l.legTime, r.legTime)) {
                    fprintf(stderr,
                            "  leg %d: ours type %d zone %d %d->%d start %.4f time %.4f; ref type %d zone %d %d->%d "
                            "start %.4f time %.4f\n",
                            i, l.type, l.zone, g->dnaIndex(l.a), g->dnaIndex(l.b), double(l.startTime),
                            double(l.legTime), r.type, r.zone, r.a, r.b, r.start, r.legTime);
                    bad = true;
                }
            }
            if (bad) {
                fail("leg", zone, s, e, lo, hi);
                continue;
            }
            // SuitLeg.getPosAtTime on walk legs == our Walk pose (walk-to/from-street differ by
            // design: DistributedSuit starts/stops them STAND_OUTSIDE_DOOR from the door).
            for (const RefSample& w : refSamples) {
                const SuitLeg& l = legs.leg(w.leg);
                if (l.type != kLegWalk) continue;
                SuitPose p;
                samplePose(*g, l, static_cast<float>(l.legTime * w.frac), &p);
                samplesChecked++;
                if (!near(p.x, w.x, 2e-3) || !near(p.y, w.y, 2e-3) || !near(p.z, w.z, 2e-3)) {
                    fail("walk position", zone, s, e, lo, hi);
                    break;
                }
            }
            // The whole timeline: legIndexAtTime agrees with the leg bounds, poses are finite
            // and never jump more than a suit can walk plus the fly height between samples.
            const float total = legs.duration();
            float px = 0, py = 0, pz = 0;
            bool first = true;
            int idx = 0;
            for (float t = 0.0f; t <= total + 0.5f && !bad; t += 0.25f) {
                idx = legs.legIndexAtTime(t, idx);
                const SuitLeg& l = legs.leg(idx);
                if (l.startTime > t + 1e-4f || (idx + 1 < legs.numLegs() && legs.leg(idx + 1).startTime <= t)) {
                    fail("legIndexAtTime", zone, s, e, lo, hi);
                    bad = true;
                    break;
                }
                SuitPose p;
                samplePose(*g, l, t - l.startTime, &p);
                posesChecked++;
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(p.h)) {
                    fail("pose not finite", zone, s, e, lo, hi);
                    bad = true;
                    break;
                }
                if (l.type == kLegFromSuitBuilding || l.type == kLegToSuitBuilding || l.type == kLegToToonBuilding ||
                    l.type == kLegWalkToStreet || l.type == kLegWalkFromStreet) {
                    first = true; // door legs start/stop STAND_OUTSIDE_DOOR from their points: no continuity check
                } else if (p.visible) {
                    if (!first) {
                        const float d = std::sqrt((p.x - px) * (p.x - px) + (p.y - py) * (p.y - py));
                        if (d > 0.25f * 4.8f * 3.0f + 3.0f) {
                            fprintf(stderr, "  pose jump %.2f at t=%.2f leg %d type %d\n", double(d), double(t), idx, l.type);
                            fail("pose jump", zone, s, e, lo, hi);
                            bad = true;
                        }
                    }
                    px = p.x, py = p.y, pz = p.z;
                    first = false;
                }
            }
            (void)pz;
        }
    }
    fclose(f);
    printf("%d queries (%d with paths), %d legs, %d walk samples, %d poses compared; %d failures\n", queries, found,
           legsChecked, samplesChecked, posesChecked, gFails);
    return gFails ? 1 : 0;
}
