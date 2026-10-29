// Host-side test for source/world/collision.cpp against a baked .ttc and Panda reference
// answers (tools/collision_test/panda_reference.py). Build + run: tools/collision_test/run.sh
//
//   collision_test <file.ttc> <reference.txt> [--zone]
//
// --zone: the .ttc is an assembled DNA zone, so the bare-terrain expectations (fixed
// heights, the +X fence walk) are skipped; Panda comparison and random walks still run.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "world/collision.h"

using tt::world::CollisionWorld;
using tt::world::CollTri;
using tt::world::Vec3;

namespace {

constexpr float kRadius = tt::world::kToonRadius; // LocalAvatar.py:166 avatarRadius
constexpr float kHeight = 2.0f * kRadius;         // GravityWalker.py:136 one sphere at z = r
constexpr float kSpeed = 20.0f;                   // >= ToonForwardSpeed 16 * 1.25 = 20
constexpr float kDt = 1.0f / 30.0f;

int failures = 0;

void check(bool ok, const char* what) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Brute-force reference helpers over every wall triangle (no grid).
void closestPtTri(const float* p, const CollTri& t, float* out) {
    const float *a = t.v0, *b = t.v1, *c = t.v2;
    float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    float ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    float d1 = dot3(ab, ap), d2 = dot3(ac, ap);
    if (d1 <= 0 && d2 <= 0) { memcpy(out, a, 12); return; }
    float bp[3] = {p[0] - b[0], p[1] - b[1], p[2] - b[2]};
    float d3 = dot3(ab, bp), d4 = dot3(ac, bp);
    if (d3 >= 0 && d4 <= d3) { memcpy(out, b, 12); return; }
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) { float v = d1 / (d1 - d3); for (int i = 0; i < 3; ++i) out[i] = a[i] + v * ab[i]; return; }
    float cp[3] = {p[0] - c[0], p[1] - c[1], p[2] - c[2]};
    float d5 = dot3(ab, cp), d6 = dot3(ac, cp);
    if (d6 >= 0 && d5 <= d6) { memcpy(out, c, 12); return; }
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) { float w = d2 / (d2 - d6); for (int i = 0; i < 3; ++i) out[i] = a[i] + w * ac[i]; return; }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) { float w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); for (int i = 0; i < 3; ++i) out[i] = b[i] + w * (c[i] - b[i]); return; }
    float den = 1.0f / (va + vb + vc), v = vb * den, w = vc * den;
    for (int i = 0; i < 3; ++i) out[i] = a[i] + ab[i] * v + ac[i] * w;
}

// Horizontal clearance of the toon sphere from all walls; matches what slideCapsule
// enforces (contacts directly above/below a sphere centre are ignored there).
float wallPenetration(const CollisionWorld& w, const Vec3& p) {
    float c[3] = {p.x, p.y, p.z + kRadius};
    float worst = 0.0f;
    for (uint32_t i = 0; i < w.numTris(); ++i) {
        const CollTri& t = w.tris()[i];
        if (!(t.flags & tt::world::kCollideWall) || (t.flags & tt::world::kCollideTrigger)) continue;
        float q[3];
        closestPtTri(c, t, q);
        float v[3] = {c[0] - q[0], c[1] - q[1], c[2] - q[2]};
        float hl = sqrtf(v[0] * v[0] + v[1] * v[1]);
        if (hl < 1e-4f) continue;
        float d = sqrtf(dot3(v, v));
        if (kRadius - d > worst) worst = kRadius - d;
    }
    for (uint32_t i = 0; i < w.numCaps(); ++i) {
        const tt::world::CollCap& k = w.caps()[i];
        if (!(k.flags & tt::world::kCollideWall) || (k.flags & tt::world::kCollideTrigger)) continue;
        float ab[3] = {k.b[0] - k.a[0], k.b[1] - k.a[1], k.b[2] - k.a[2]};
        float ap[3] = {c[0] - k.a[0], c[1] - k.a[1], c[2] - k.a[2]};
        float l2 = dot3(ab, ab), t = l2 > 0 ? fminf(fmaxf(dot3(ap, ab) / l2, 0.0f), 1.0f) : 0.0f;
        float v[3] = {ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t};
        if (hypotf(v[0], v[1]) < 1e-4f) continue;
        float d = sqrtf(dot3(v, v));
        if (kRadius + k.r - d > worst) worst = kRadius + k.r - d;
    }
    return worst;
}

// Does segment a-b cross any wall triangle? (tunnelling check)
bool crossesWall(const CollisionWorld& w, const float* a, const float* b) {
    float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    for (uint32_t i = 0; i < w.numTris(); ++i) {
        const CollTri& t = w.tris()[i];
        if (!(t.flags & tt::world::kCollideWall) || (t.flags & tt::world::kCollideTrigger)) continue;
        float da = dot3(t.n, a) - t.d, db = dot3(t.n, b) - t.d;
        if ((da > 0) == (db > 0) || da == db) continue;
        float s = da / (da - db);
        float p[3] = {a[0] + d[0] * s, a[1] + d[1] * s, a[2] + d[2] * s}, q[3];
        closestPtTri(p, t, q);
        float v[3] = {p[0] - q[0], p[1] - q[1], p[2] - q[2]};
        if (dot3(v, v) < 1e-8f) return true;
    }
    return false;
}

// One simulated frame the way Player should do it: horizontal slide at the current
// height, ground snap, then (only if the height changed) a zero-move slide at the new
// height so stepping off a ledge never leaves the toon inside the ledge's wall.
Vec3 stepToon(const CollisionWorld& w, Vec3 p, float heading, float dt) {
    Vec3 delta = {-sinf(heading) * kSpeed * dt, cosf(heading) * kSpeed * dt, 0.0f};
    p = w.slideCapsule(p, kRadius, kHeight, delta);
    bool found = false;
    float g = w.groundHeight(p.x, p.y, p.z, &found);
    if (found && g != p.z) p = w.slideCapsule(p, kRadius, kHeight, {0.0f, 0.0f, g - p.z});
    return p;
}

// Distance from the toon sphere centre to the nearest wall (brute force).
float wallDistance(const CollisionWorld& w, const Vec3& p) {
    float c[3] = {p.x, p.y, p.z + kRadius};
    float best = 1e9f;
    for (uint32_t i = 0; i < w.numTris(); ++i) {
        const CollTri& t = w.tris()[i];
        if (!(t.flags & tt::world::kCollideWall) || (t.flags & tt::world::kCollideTrigger)) continue;
        float q[3];
        closestPtTri(c, t, q);
        float v[3] = {c[0] - q[0], c[1] - q[1], c[2] - q[2]};
        best = fminf(best, sqrtf(dot3(v, v)));
    }
    return best;
}

double nowNs() {
    return double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s file.ttc reference.txt [--zone]\n", argv[0]);
        return 2;
    }
    const bool zone = argc > 3 && strcmp(argv[3], "--zone") == 0;
    CollisionWorld w;
    check(w.load(argv[1]), "load .ttc");
    if (!w.loaded()) return 1;
    FILE* bf = fopen(argv[1], "rb");
    long blobSize = -1;
    if (bf && fseek(bf, 0, SEEK_END) == 0) blobSize = ftell(bf);
    if (bf) rewind(bf);
    std::vector<unsigned char> blob(blobSize > 0 ? static_cast<size_t>(blobSize) : 0);
    const bool blobRead = bf && blobSize > 0 && fread(blob.data(), 1, blob.size(), bf) == blob.size();
    if (bf) fclose(bf);
    CollisionWorld memory;
    check(blobRead && memory.loadMemory(blob.data(), static_cast<uint32_t>(blob.size()), "memory fixture"),
          "load prefetched .ttc memory");
    check(memory.numTris() == w.numTris() && memory.numCaps() == w.numCaps() &&
              memory.numTriggers() == w.numTriggers() && memory.heapBytes() == w.heapBytes(),
          "prefetched collision metadata matches file load");
    std::vector<unsigned char> corrupt = blob;
    corrupt[0] ^= 0xff;
    CollisionWorld rejected;
    check(!rejected.loadMemory(corrupt.data(), static_cast<uint32_t>(corrupt.size()), "corrupt memory fixture"),
          "prefetched collision rejects corrupt header");
    printf("tris %u caps %u triggers %d heap %u bytes\n", (unsigned)w.numTris(), (unsigned)w.numCaps(),
           w.numTriggers(), (unsigned)w.heapBytes());

    // 1. Known heights (Panda raycast: coll_mainFoolr z=0, coll_plaza z=4).
    bool f1 = false, f2 = false;
    float g1 = w.groundHeight(0, -20, 10, &f1), g2 = w.groundHeight(20, 20, 10, &f2);
    printf("ground(0,-20)=%.4f found=%d  ground(20,20)=%.4f found=%d\n", g1, f1, g2, f2);
    if (!zone) {
        check(f1 && fabsf(g1 - 0.0f) < 0.01f, "groundHeight(0,-20) ~= 0");
        check(f2 && fabsf(g2 - 4.0f) < 0.01f, "groundHeight(20,20) ~= 4");
    }

    // 2. Panda reference comparison.
    FILE* rf = fopen(argv[2], "r");
    if (!rf) {
        fprintf(stderr, "cannot open %s\n", argv[2]);
        return 1;
    }
    char kind;
    int gN = 0, gMismatch = 0, cN = 0, cMismatch = 0;
    float gMaxErr = 0, cMaxErr = 0;
    char line[256];
    while (fgets(line, sizeof line, rf)) {
        if (sscanf(line, " %c", &kind) != 1) continue;
        if (kind == 'G') {
            float x, y, zf, st, z;
            int fd;
            if (sscanf(line + 1, "%f %f %f %f %d %f", &x, &y, &zf, &st, &fd, &z) != 6) continue;
            bool f = false;
            float mz = w.groundHeight(x, y, zf, &f, st);
            ++gN;
            if (f != (fd != 0)) {
                ++gMismatch;
                printf("  ground mismatch at (%.3f, %.3f) zFrom %.2f step %.1f: panda %d %.4f, ours %d %.4f\n", x, y,
                       zf, st, fd, z, f, mz);
            } else if (f && fabsf(mz - z) > gMaxErr) {
                gMaxErr = fabsf(mz - z);
            }
        } else if (kind == 'C') {
            float a[3], b[3], t;
            int fd;
            if (sscanf(line + 1, "%f %f %f %f %f %f %d %f", &a[0], &a[1], &a[2], &b[0], &b[1], &b[2], &fd, &t) != 8)
                continue;
            float mt = -1;
            bool f = w.cameraRaycast({a[0], a[1], a[2]}, {b[0], b[1], b[2]}, &mt);
            ++cN;
            if (f != (fd != 0)) {
                ++cMismatch;
                printf("  camera mismatch: panda %d %.5f, ours %d %.5f\n", fd, t, f, mt);
            } else if (f) {
                float len = sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) +
                                  (b[2] - a[2]) * (b[2] - a[2]));
                if (fabsf(mt - t) * len > cMaxErr) cMaxErr = fabsf(mt - t) * len;
            }
        }
    }
    fclose(rf);
    printf("ground vs Panda: %d queries, %d found-mismatches, max |dz| = %.6f\n", gN, gMismatch, gMaxErr);
    printf("camera vs Panda: %d segments, %d hit-mismatches, max hit distance error = %.6f\n", cN, cMismatch,
           cMaxErr);
    check(gN >= 200 && gMismatch == 0 && gMaxErr < 1e-3f, "groundHeight matches Panda CollisionTraverser");
    check(cN >= 100 && cMismatch == 0 && cMaxErr < 1e-3f, "cameraRaycast matches Panda CollisionSegment");

    // 3. Walk +X from (0,-20): a Panda CollisionSegment at z = 1.4 hits the fence
    // 'collision_1' at x = 91.34. The fence is angled, so after contact the toon slides
    // along it (correct); it must make contact before x = 91.34 and never cross it.
    if (!zone) {
        Vec3 p = {0, -20, g1};
        int contactFrame = -1;
        Vec3 contact = p;
        bool crossed = false;
        for (int f = 0; f < 300; ++f) {
            Vec3 q = stepToon(w, p, -1.5707963f, kDt); // heading -90 deg = +X
            float a[3] = {p.x, p.y, p.z + kRadius}, b[3] = {q.x, q.y, q.z + kRadius};
            crossed = crossed || crossesWall(w, a, b);
            if (contactFrame < 0 && q.x - p.x < 0.99f * kSpeed * kDt) {
                contactFrame = f;
                contact = q;
            }
            p = q;
        }
        float dist = wallDistance(w, p);
        printf("walk +X from (0,-20): first wall contact frame %d at (%.3f, %.3f), end (%.3f, %.3f, %.3f), "
               "end wall distance %.4f\n",
               contactFrame, contact.x, contact.y, p.x, p.y, p.z, dist);
        check(contactFrame > 0 && contact.x > 85.0f && contact.x < 91.34f - 0.5f * kRadius && !crossed &&
                  dist > kRadius - 0.01f && dist < kRadius + 0.05f,
              "walking toward the fence stops at it (then slides along, touching, never crossing)");
    }

    // 4. Random walks at 20 u/s, dt = 1/30: no tunnelling, no penetration, no corner jitter.
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> ux(-150, 150), uy(-130, 130), uh(-3.14159f, 3.14159f), ut(-0.3f, 0.3f);
        int walks = 0, frames = 0, tunnels = 0, stuckWalks = 0;
        float worstPen = 0, worstJitter = 0;
        while (walks < 300) {
            Vec3 p = {ux(rng), uy(rng), 0};
            bool found = false;
            p.z = w.groundHeight(p.x, p.y, 100.0f, &found, 0.0f);
            if (!found || wallPenetration(w, p) > 0.0f) continue;
            ++walks;
            float h = uh(rng);
            bool turning = (walks & 1) != 0; // half the walks keep a fixed heading into walls
            Vec3 hist[30];
            for (int f = 0; f < 300; ++f, ++frames) {
                if (turning) h += ut(rng);
                Vec3 q = stepToon(w, p, h, kDt);
                float a[3] = {p.x, p.y, p.z + kRadius}, b[3] = {q.x, q.y, q.z + kRadius};
                if (crossesWall(w, a, b)) ++tunnels;
                float pen = wallPenetration(w, q);
                if (pen > worstPen) worstPen = pen;
                p = q;
                if (f >= 270) hist[f - 270] = p;
            }
            if (!turning) { // pressed against something for the last second?
                // Jitter = back-and-forth motion: path length minus net displacement.
                float span = 0, path = 0;
                for (int i = 1; i < 30; ++i) {
                    span = fmaxf(span, hypotf(hist[i].x - hist[0].x, hist[i].y - hist[0].y));
                    path += hypotf(hist[i].x - hist[i - 1].x, hist[i].y - hist[i - 1].y);
                }
                if (span < 0.05f) {
                    ++stuckWalks;
                    float jitter = path - hypotf(hist[29].x - hist[0].x, hist[29].y - hist[0].y);
                    if (jitter > worstJitter) worstJitter = jitter;
                }
            }
        }
        printf("random walks: %d walks, %d frames, %d tunnels, worst wall penetration %.5f, "
               "%d stuck walks with worst 1 s jitter (path - net) %.6f\n",
               walks, frames, tunnels, worstPen, stuckWalks, worstJitter);
        check(tunnels == 0, "no tunnelling at 20 u/s, dt 1/30");
        check(worstPen < 0.01f, "toon sphere never penetrates a wall by > 0.01");
        check(stuckWalks > 0 && worstJitter < 1e-3f, "no jitter when pushing into walls/corners");
    }

    // 5. Timing (host). Scale to the 268 MHz ARM11 in the report.
    {
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> ux(-150, 150), uy(-130, 130), uh(-3.14159f, 3.14159f);
        const int n = 200000;
        std::vector<float> xs(n), ys(n), hs(n);
        for (int i = 0; i < n; ++i) {
            xs[i] = ux(rng);
            ys[i] = uy(rng);
            hs[i] = uh(rng);
        }
        volatile float sink = 0;
        double t0 = nowNs();
        for (int i = 0; i < n; ++i) {
            bool f;
            sink = sink + w.groundHeight(xs[i], ys[i], 30.0f, &f);
        }
        double t1 = nowNs();
        for (int i = 0; i < n; ++i) {
            float gz = w.groundHeight(xs[i], ys[i], 30.0f, nullptr);
            Vec3 r = w.slideCapsule({xs[i], ys[i], gz}, kRadius, kHeight,
                                    {-sinf(hs[i]) * kSpeed * kDt, cosf(hs[i]) * kSpeed * kDt, 0});
            sink = sink + r.x;
        }
        double t2 = nowNs();
        for (int i = 0; i < n; ++i) {
            float ht = 0;
            Vec3 a = {xs[i], ys[i], 3.0f};
            Vec3 b = {xs[i] + sinf(hs[i]) * 15.0f, ys[i] - cosf(hs[i]) * 15.0f, 8.0f};
            if (w.cameraRaycast(a, b, &ht)) sink = sink + ht;
        }
        double t3 = nowNs();
        printf("host timing: groundHeight %.0f ns, groundHeight+slideCapsule(0.67u) %.0f ns, "
               "cameraRaycast(15u) %.0f ns\n",
               (t1 - t0) / n, (t2 - t1) / n, (t3 - t2) / n);
    }

    printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
