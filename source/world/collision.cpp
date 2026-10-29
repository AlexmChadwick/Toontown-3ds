// TTC collision queries. Format spec: tools/pipeline/collide.py.
//
// Upstream behaviour this mirrors (ref/open-toontown, direct = panda3d 1.10.15 wheel):
//   otp/avatar/LocalAvatar.py:166  setupControls(avatarRadius=1.4, floorOffset=FloorOffset,
//                                  reach=4.0, wallBitmask=WallBitmask, floorBitmask=FloorBitmask)
//   otp/otpbase/OTPGlobals.py:5-7  WallBitmask=1, FloorBitmask=2, CameraBitmask=4
//   otp/otpbase/OTPGlobals.py:66   FloorOffset = 0.025
//   direct/controls/GravityWalker.py:136  wall sphere CollisionSphere(0, 0, r, r): a single
//                                  sphere resting on the feet, i.e. our capsule with
//                                  height = 2 * radius
//   direct/controls/GravityWalker.py:102  floor ray from z = 4000 straight down
//                                  (ControlManager.py:19 CollisionHandlerRayStart)
//   otp/avatar/LocalAvatar.py:210-216  camera: CollisionSegment with CameraBitmask
#include "world/collision.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"

namespace tt::world {

namespace {

constexpr uint32_t kMagic = 0x31435454; // "TTC1"
constexpr uint16_t kVersion = 1;
constexpr int kHeaderSize = 96;
constexpr float kSkin = 1e-3f;      // extra clearance after a wall push
constexpr int kMaxSubsteps = 32;    // per slideCapsule call
constexpr int kResolveIters = 8;    // Gauss-Seidel passes per sub-step
constexpr int kMaxSpheres = 4;      // spheres approximating the capsule

struct Header {
    char magic[4];
    uint16_t version, flags;
    float origin[2];
    float cellSize;
    uint16_t gridW, gridH;
    uint32_t numTris, numCaps, numPlanes, numTriggers;
    float boundsMin[3], boundsMax[3];
    uint32_t triOffset, capOffset, planeOffset, cellTableOffset;
    uint32_t cellDataOffset, cellDataCount, triggerOffset, fileSize;
};
static_assert(sizeof(Header) == kHeaderSize, "must match collide.py HEADER_SIZE");

inline float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }

// Closest point on triangle (a, b, c) to p (Ericson, Real-Time Collision Detection 5.1.5).
void closestPtTri(const float* p, const float* a, const float* b, const float* c, float* out) {
    float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    float ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    float d1 = dot3(ab, ap), d2 = dot3(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        memcpy(out, a, 12);
        return;
    }
    float bp[3] = {p[0] - b[0], p[1] - b[1], p[2] - b[2]};
    float d3 = dot3(ab, bp), d4 = dot3(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        memcpy(out, b, 12);
        return;
    }
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        for (int i = 0; i < 3; ++i) out[i] = a[i] + v * ab[i];
        return;
    }
    float cp[3] = {p[0] - c[0], p[1] - c[1], p[2] - c[2]};
    float d5 = dot3(ab, cp), d6 = dot3(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        memcpy(out, c, 12);
        return;
    }
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        for (int i = 0; i < 3; ++i) out[i] = a[i] + w * ac[i];
        return;
    }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        for (int i = 0; i < 3; ++i) out[i] = b[i] + w * (c[i] - b[i]);
        return;
    }
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom, w = vc * denom;
    for (int i = 0; i < 3; ++i) out[i] = a[i] + ab[i] * v + ac[i] * w;
}

void closestPtSeg(const float* p, const float* a, const float* b, float* out) {
    float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float len2 = dot3(ab, ab);
    float t = 0.0f;
    if (len2 > 1e-12f) {
        float ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
        t = dot3(ap, ab) / len2;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }
    for (int i = 0; i < 3; ++i) out[i] = a[i] + t * ab[i];
}

// Ray (o, unit dir) vs sphere: first t >= 0 of entry, or -1. Origin inside -> 0.
float raySphere(const float* o, const float* dir, const float* c, float r) {
    float oc[3] = {o[0] - c[0], o[1] - c[1], o[2] - c[2]};
    float cc = dot3(oc, oc) - r * r;
    if (cc <= 0.0f) return 0.0f;
    float b = dot3(oc, dir);
    if (b > 0.0f) return -1.0f;
    float h = b * b - cc;
    if (h < 0.0f) return -1.0f;
    return -b - sqrtf(h);
}

// Ray (o, unit dir) vs capsule: union of the cylinder body and the two end spheres, so
// the first entry is the minimum of the parts' entries. Origin inside -> 0.
float rayCapsule(const float* o, const float* dir, const CollCap& cap) {
    float q[3];
    closestPtSeg(o, cap.a, cap.b, q);
    float dq[3] = {o[0] - q[0], o[1] - q[1], o[2] - q[2]};
    if (dot3(dq, dq) <= cap.r * cap.r) return 0.0f;
    float best = -1.0f;
    float ta = raySphere(o, dir, cap.a, cap.r);
    if (ta >= 0.0f) best = ta;
    float ba[3] = {cap.b[0] - cap.a[0], cap.b[1] - cap.a[1], cap.b[2] - cap.a[2]};
    float baba = dot3(ba, ba);
    if (baba > 1e-12f) {
        float tb = raySphere(o, dir, cap.b, cap.r);
        if (tb >= 0.0f && (best < 0.0f || tb < best)) best = tb;
        float oa[3] = {o[0] - cap.a[0], o[1] - cap.a[1], o[2] - cap.a[2]};
        float bard = dot3(ba, dir), baoa = dot3(ba, oa);
        float a = baba - bard * bard;
        if (a > 1e-9f * baba) {
            float b = baba * dot3(dir, oa) - baoa * bard;
            float c = baba * dot3(oa, oa) - baoa * baoa - cap.r * cap.r * baba;
            float h = b * b - a * c;
            if (h >= 0.0f) {
                float t = (-b - sqrtf(h)) / a;
                float y = baoa + t * bard;
                if (t >= 0.0f && y > 0.0f && y < baba && (best < 0.0f || t < best)) best = t;
            }
        }
    }
    return best;
}

// Two-sided segment-triangle test (Moller-Trumbore). Returns t in [0, tMax] or -1.
float rayTri(const float* o, const float* d, const CollTri& t, float tMax) {
    float e1[3] = {t.v1[0] - t.v0[0], t.v1[1] - t.v0[1], t.v1[2] - t.v0[2]};
    float e2[3] = {t.v2[0] - t.v0[0], t.v2[1] - t.v0[1], t.v2[2] - t.v0[2]};
    float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    float det = dot3(e1, p);
    if (fabsf(det) < 1e-12f) return -1.0f;
    float inv = 1.0f / det;
    float s[3] = {o[0] - t.v0[0], o[1] - t.v0[1], o[2] - t.v0[2]};
    float u = dot3(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return -1.0f;
    float qv[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    float v = dot3(d, qv) * inv;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;
    float tt = dot3(e2, qv) * inv;
    return (tt >= 0.0f && tt <= tMax) ? tt : -1.0f;
}

// Horizontal push that moves a sphere centre c (radius r) clear of the closest point q.
// Returns false if there is no penetration or the contact is purely vertical.
bool horizontalPush(const float* c, const float* q, float r, const float* fallbackN, float* push) {
    float v[3] = {c[0] - q[0], c[1] - q[1], c[2] - q[2]};
    float d2 = dot3(v, v);
    if (d2 >= r * r) return false;
    float hl2 = v[0] * v[0] + v[1] * v[1];
    float hx, hy, hl;
    if (hl2 > 1e-10f) {
        hl = sqrtf(hl2);
        hx = v[0] / hl;
        hy = v[1] / hl;
    } else {
        // Centre exactly on the surface: use the surface normal's horizontal part.
        if (!fallbackN) return false;
        float nl = sqrtf(fallbackN[0] * fallbackN[0] + fallbackN[1] * fallbackN[1]);
        if (nl < 1e-4f) return false;
        hx = fallbackN[0] / nl;
        hy = fallbackN[1] / nl;
        hl = 0.0f;
    }
    // Need (hl + s)^2 + vz^2 >= r^2 for this contact point.
    float s = sqrtf(r * r - v[2] * v[2]) - hl + kSkin;
    push[0] = hx * s;
    push[1] = hy * s;
    return true;
}

} // namespace

CollisionWorld::~CollisionWorld() { unload(); }

void CollisionWorld::unload() {
    free(blob_);
    free(stamps_);
    blob_ = nullptr;
    stamps_ = nullptr;
    heapBytes_ = 0;
    numTris_ = numCaps_ = numPlanes_ = numTriggers_ = 0;
    gridW_ = gridH_ = 0;
    tris_ = nullptr;
    caps_ = nullptr;
    planes_ = nullptr;
    cellTable_ = nullptr;
    cellData_ = nullptr;
    triggers_ = nullptr;
    cellDataCount_ = 0;
}

bool CollisionWorld::load(const char* path) {
    unload();
    FILE* f = fopen(path, "rb");
    if (!f) {
        TT_LOG("collision: cannot open %s", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    const bool ok = size > 0 && loadFrom(f, 0, static_cast<uint32_t>(size), path);
    fclose(f);
    return ok;
}

bool CollisionWorld::loadFrom(FILE* f, uint32_t base, uint32_t sizeBytes, const char* label) {
    unload();
    const char* path = label ? label : "(embedded)";
    const long size = static_cast<long>(sizeBytes);
    if (size < kHeaderSize) {
        TT_LOG("collision: %s too small", path);
        return false;
    }
    uint8_t* blob = static_cast<uint8_t*>(malloc(static_cast<size_t>(size)));
    if (!blob || fseek(f, static_cast<long>(base), SEEK_SET) != 0 ||
        fread(blob, 1, static_cast<size_t>(size), f) != static_cast<size_t>(size)) {
        free(blob);
        TT_LOG("collision: cannot read %s", path);
        return false;
    }

    return adoptBlob(blob, static_cast<uint32_t>(size), path);
}

bool CollisionWorld::loadMemory(const void* data, uint32_t size, const char* label) {
    unload();
    if (!data || size < kHeaderSize) {
        TT_LOG("collision: %s too small", label ? label : "(memory)");
        return false;
    }
    uint8_t* blob = static_cast<uint8_t*>(malloc(size));
    if (!blob) return false;
    memcpy(blob, data, size);
    return adoptBlob(blob, size, label ? label : "(memory)");
}

bool CollisionWorld::adoptBlob(uint8_t* blob, uint32_t sizeBytes, const char* path) {
    Header h;
    memcpy(&h, blob, sizeof(h));
    uint32_t magic;
    memcpy(&magic, h.magic, 4);
    const uint32_t usize = sizeBytes;
    auto sectionOk = [usize](uint32_t off, uint64_t bytes) {
        return (off & 3) == 0 && off >= kHeaderSize && off + bytes <= usize;
    };
    bool ok = magic == kMagic && h.version == kVersion && h.fileSize == usize && h.cellSize > 0.0f &&
              h.gridW > 0 && h.gridH > 0 && h.numTris <= 65535 && h.numCaps <= 65535 &&
              sectionOk(h.triOffset, uint64_t(h.numTris) * sizeof(CollTri)) &&
              sectionOk(h.capOffset, uint64_t(h.numCaps) * sizeof(CollCap)) &&
              sectionOk(h.planeOffset, uint64_t(h.numPlanes) * sizeof(CollPlane)) &&
              sectionOk(h.cellTableOffset, uint64_t(h.gridW) * h.gridH * 4) &&
              sectionOk(h.cellDataOffset, uint64_t(h.cellDataCount) * 2) &&
              sectionOk(h.triggerOffset, uint64_t(h.numTriggers) * sizeof(CollTrigger));
    if (!ok) {
        free(blob);
        TT_LOG("collision: bad header in %s (want TTC1 v%u)", path, kVersion);
        return false;
    }
    const uint32_t* table = reinterpret_cast<const uint32_t*>(blob + h.cellTableOffset);
    const uint16_t* data = reinterpret_cast<const uint16_t*>(blob + h.cellDataOffset);
    // Validate every cell once so queries can trust the indices.
    for (uint32_t c = 0; ok && c < uint32_t(h.gridW) * h.gridH; ++c) {
        uint32_t off = table[c];
        if (off + 6 > h.cellDataCount) {
            ok = false;
            break;
        }
        const uint16_t* cnt = data + off;
        uint32_t total = 0;
        for (int k = 0; k < 6; ++k) total += cnt[k];
        if (off + 6 + total > h.cellDataCount) {
            ok = false;
            break;
        }
        const uint16_t* idx = cnt + 6;
        for (int k = 0; k < 6 && ok; ++k) {
            uint32_t lim = (k & 1) ? h.numCaps : h.numTris;
            for (uint32_t i = 0; i < cnt[k]; ++i)
                if (idx[i] >= lim) ok = false;
            idx += cnt[k];
        }
    }
    const CollTrigger* trig = reinterpret_cast<const CollTrigger*>(blob + h.triggerOffset);
    for (uint32_t i = 0; ok && i < h.numTriggers; ++i) {
        uint32_t lim = trig[i].kind ? h.numTris : h.numCaps;
        if (trig[i].kind > 1 || trig[i].first + trig[i].count > lim) ok = false;
    }
    uint32_t* stamps = nullptr;
    if (ok) {
        stamps = static_cast<uint32_t*>(calloc(h.numTris + h.numCaps + 1, sizeof(uint32_t)));
        ok = stamps != nullptr;
    }
    if (!ok) {
        free(blob);
        TT_LOG("collision: corrupt cell/trigger data in %s", path);
        return false;
    }

    blob_ = blob;
    stamps_ = stamps;
    stamp_ = 0;
    heapBytes_ = usize + (h.numTris + h.numCaps + 1) * 4;
    originX_ = h.origin[0];
    originY_ = h.origin[1];
    cellSize_ = h.cellSize;
    invCell_ = 1.0f / h.cellSize;
    gridW_ = h.gridW;
    gridH_ = h.gridH;
    numTris_ = h.numTris;
    numCaps_ = h.numCaps;
    numPlanes_ = h.numPlanes;
    numTriggers_ = h.numTriggers;
    tris_ = reinterpret_cast<const CollTri*>(blob + h.triOffset);
    caps_ = reinterpret_cast<const CollCap*>(blob + h.capOffset);
    planes_ = reinterpret_cast<const CollPlane*>(blob + h.planeOffset);
    cellTable_ = table;
    cellData_ = data;
    cellDataCount_ = h.cellDataCount;
    triggers_ = trig;
    TT_LOG("collision: %s: %lu tris, %lu caps, %lu triggers, %dx%d grid, %lu KB heap", path,
           (unsigned long)numTris_, (unsigned long)numCaps_, (unsigned long)numTriggers_, gridW_, gridH_,
           (unsigned long)(heapBytes_ / 1024));
    return true;
}

bool CollisionWorld::cellCoords(float x, float y, int* cx, int* cy) const {
    float fx = (x - originX_) * invCell_;
    float fy = (y - originY_) * invCell_;
    if (!(fx >= 0.0f && fy >= 0.0f && fx < float(gridW_) && fy < float(gridH_))) return false;
    *cx = int(fx);
    *cy = int(fy);
    if (*cx >= gridW_) *cx = gridW_ - 1;
    if (*cy >= gridH_) *cy = gridH_ - 1;
    return true;
}

CollisionWorld::CellLists CollisionWorld::cell(int cx, int cy) const {
    const uint16_t* c = cellData_ + cellTable_[cy * gridW_ + cx];
    return {c, c + 6};
}

uint32_t CollisionWorld::nextStamp() const {
    if (++stamp_ == 0) { // wrapped: clear so old stamps can't alias
        memset(stamps_, 0, (numTris_ + numCaps_ + 1) * sizeof(uint32_t));
        stamp_ = 1;
    }
    return stamp_;
}

float CollisionWorld::groundHeight(float x, float y, float zFrom, bool* found, float stepHeight) const {
    const float limit = zFrom + stepHeight + 1e-4f;
    float best = -INFINITY;
    bool hit = false;
    int cx, cy;
    if (blob_ && cellCoords(x, y, &cx, &cy)) {
        CellLists cl = cell(cx, cy);
        const uint16_t* idx = cl.idx;
        for (uint16_t i = 0; i < cl.counts[0]; ++i) {
            const CollTri& t = tris_[idx[i]];
            float nz = t.n[2];
            if (fabsf(nz) < 1e-4f) continue;
            // 2D point-in-triangle, either winding, edges inclusive.
            float e0 = (t.v1[0] - t.v0[0]) * (y - t.v0[1]) - (t.v1[1] - t.v0[1]) * (x - t.v0[0]);
            float e1 = (t.v2[0] - t.v1[0]) * (y - t.v1[1]) - (t.v2[1] - t.v1[1]) * (x - t.v1[0]);
            float e2 = (t.v0[0] - t.v2[0]) * (y - t.v2[1]) - (t.v0[1] - t.v2[1]) * (x - t.v2[0]);
            const float eps = -1e-6f;
            bool inside = (nz > 0.0f) ? (e0 >= eps && e1 >= eps && e2 >= eps)
                                      : (e0 <= -eps && e1 <= -eps && e2 <= -eps);
            if (!inside) continue;
            float z = (t.d - t.n[0] * x - t.n[1] * y) / nz;
            if (z <= limit && z > best) {
                best = z;
                hit = true;
            }
        }
        idx += cl.counts[0];
        for (uint16_t i = 0; i < cl.counts[1]; ++i) {
            const CollCap& c = caps_[idx[i]];
            float top = maxf(c.a[2], c.b[2]) + c.r + 1.0f;
            float o[3] = {x, y, top};
            const float down[3] = {0.0f, 0.0f, -1.0f};
            float t = rayCapsule(o, down, c);
            if (t < 0.0f) continue;
            float z = top - t;
            if (z <= limit && z > best) {
                best = z;
                hit = true;
            }
        }
    }
    for (uint32_t i = 0; i < numPlanes_; ++i) {
        const CollPlane& p = planes_[i];
        if (!(p.flags & kCollideFloor) || p.n[2] < 1e-4f) continue;
        float z = (p.d - p.n[0] * x - p.n[1] * y) / p.n[2];
        if (z <= limit && z > best) {
            best = z;
            hit = true;
        }
    }
    if (found) *found = hit;
    return hit ? best : zFrom;
}

bool CollisionWorld::gatherWalls(float x0, float y0, float x1, float y1, float z0, float z1) const {
    numGatherTris_ = numGatherCaps_ = 0;
    int cx0 = int(floorf((x0 - originX_) * invCell_)), cx1 = int(floorf((x1 - originX_) * invCell_));
    int cy0 = int(floorf((y0 - originY_) * invCell_)), cy1 = int(floorf((y1 - originY_) * invCell_));
    if (cx1 < 0 || cy1 < 0 || cx0 >= gridW_ || cy0 >= gridH_) return true;
    cx0 = cx0 < 0 ? 0 : cx0;
    cy0 = cy0 < 0 ? 0 : cy0;
    cx1 = cx1 >= gridW_ ? gridW_ - 1 : cx1;
    cy1 = cy1 >= gridH_ ? gridH_ - 1 : cy1;
    const uint32_t s = nextStamp();
    bool fit = true;
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            CellLists cl = cell(cx, cy);
            const uint16_t* idx = cl.idx + cl.counts[0] + cl.counts[1];
            for (uint16_t i = 0; i < cl.counts[2]; ++i) {
                uint16_t ti = idx[i];
                if (stamps_[ti] == s) continue;
                stamps_[ti] = s;
                const CollTri& t = tris_[ti];
                if (minf(t.v0[0], minf(t.v1[0], t.v2[0])) > x1 || maxf(t.v0[0], maxf(t.v1[0], t.v2[0])) < x0 ||
                    minf(t.v0[1], minf(t.v1[1], t.v2[1])) > y1 || maxf(t.v0[1], maxf(t.v1[1], t.v2[1])) < y0 ||
                    minf(t.v0[2], minf(t.v1[2], t.v2[2])) > z1 || maxf(t.v0[2], maxf(t.v1[2], t.v2[2])) < z0)
                    continue;
                if (numGatherTris_ < kMaxGather)
                    gatherTris_[numGatherTris_++] = ti;
                else
                    fit = false;
            }
            idx += cl.counts[2];
            for (uint16_t i = 0; i < cl.counts[3]; ++i) {
                uint16_t ci = idx[i];
                if (stamps_[numTris_ + ci] == s) continue;
                stamps_[numTris_ + ci] = s;
                const CollCap& c = caps_[ci];
                if (minf(c.a[2], c.b[2]) - c.r > z1 || maxf(c.a[2], c.b[2]) + c.r < z0) continue;
                if (numGatherCaps_ < kMaxGather)
                    gatherCaps_[numGatherCaps_++] = ci;
                else
                    fit = false;
            }
        }
    }
    return fit;
}

Vec3 CollisionWorld::slideCapsule(const Vec3& pos, float radius, float height, const Vec3& desiredDelta) const {
    Vec3 cur = pos;
    if (!blob_ || radius <= 0.0f) {
        return {pos.x + desiredDelta.x, pos.y + desiredDelta.y, pos.z + desiredDelta.z};
    }
    if (height < 2.0f * radius) height = 2.0f * radius;

    float hlen = sqrtf(desiredDelta.x * desiredDelta.x + desiredDelta.y * desiredDelta.y);
    int steps = int(ceilf(hlen / (0.5f * radius)));
    if (steps < 1) steps = 1;
    if (steps > kMaxSubsteps) steps = kMaxSubsteps;
    const float sx = desiredDelta.x / float(steps), sy = desiredDelta.y / float(steps);
    const float sz = desiredDelta.z / float(steps);

    // Sphere centres relative to the feet.
    int nSph = 1 + int(ceilf((height - 2.0f * radius) / radius - 1e-4f));
    if (nSph > kMaxSpheres) nSph = kMaxSpheres;
    float offs[kMaxSpheres];
    for (int k = 0; k < nSph; ++k)
        offs[k] = radius + (nSph > 1 ? (height - 2.0f * radius) * float(k) / float(nSph - 1) : 0.0f);

    // Pushes can carry the toon beyond the straight path: by at most the move length plus
    // one radius of initial penetration, so gather that much further out.
    const float margin = 2.0f * radius + hlen + 0.05f;
    const float zLo = minf(pos.z, pos.z + desiredDelta.z) - 0.05f;
    const float zHi = maxf(pos.z, pos.z + desiredDelta.z) + height + 0.05f;
    const float bx0 = minf(pos.x, pos.x + desiredDelta.x) - margin;
    const float by0 = minf(pos.y, pos.y + desiredDelta.y) - margin;
    const float bx1 = maxf(pos.x, pos.x + desiredDelta.x) + margin;
    const float by1 = maxf(pos.y, pos.y + desiredDelta.y) + margin;
    gatherWalls(bx0, by0, bx1, by1, zLo, zHi);

    // A start that is already penetrating (e.g. the ground snap lowered the toon into a
    // gap narrower than its diameter) must not be "reverted to": then best effort wins.
    Vec3 probe = cur;
    const bool startClear = !pushPass(&probe, radius, offs, nSph, false);
    for (int s = 0; s < steps; ++s) {
        const Vec3 prev = cur;
        cur.x += sx;
        cur.y += sy;
        cur.z += sz;
        bool pushed = true;
        for (int it = 0; it < kResolveIters && pushed; ++it) pushed = pushPass(&cur, radius, offs, nSph, true);
        if (pushed && startClear) { // did not converge (acute corner): stay where we were
            cur = prev;
            cur.z += sz * float(steps - s);
            break;
        }
    }
    // Safety net: if the result left the gathered box, walls out there were never seen.
    // Re-gather around the result and resolve in place (rare; never seen in TTC tests).
    if (cur.x - radius < bx0 || cur.x + radius > bx1 || cur.y - radius < by0 || cur.y + radius > by1) {
        gatherWalls(cur.x - margin, cur.y - margin, cur.x + margin, cur.y + margin, cur.z - 0.05f,
                    cur.z + height + 0.05f);
        bool pushed = true;
        for (int it = 0; it < kResolveIters && pushed; ++it) pushed = pushPass(&cur, radius, offs, nSph, true);
    }
    return cur;
}

bool CollisionWorld::pushPass(Vec3* pos, float radius, const float* offs, int nSph, bool apply) const {
    Vec3& cur = *pos;
    bool pushed = false;
    for (int k = 0; k < nSph; ++k) {
        float c[3] = {cur.x, cur.y, cur.z + offs[k]};
        float q[3], push[2];
        for (int i = 0; i < numGatherTris_; ++i) {
            const CollTri& t = tris_[gatherTris_[i]];
            float pd = dot3(t.n, c) - t.d;
            if (pd > radius || pd < -radius) continue; // plane reject
            closestPtTri(c, t.v0, t.v1, t.v2, q);
            const float side = pd >= 0.0f ? 1.0f : -1.0f;
            const float fn[3] = {t.n[0] * side, t.n[1] * side, t.n[2] * side};
            if (horizontalPush(c, q, radius, fn, push)) {
                if (!apply) return true;
                cur.x += push[0];
                cur.y += push[1];
                c[0] = cur.x;
                c[1] = cur.y;
                pushed = true;
            }
        }
        for (int i = 0; i < numGatherCaps_; ++i) {
            const CollCap& cp = caps_[gatherCaps_[i]];
            closestPtSeg(c, cp.a, cp.b, q);
            if (horizontalPush(c, q, radius + cp.r, nullptr, push)) {
                if (!apply) return true;
                cur.x += push[0];
                cur.y += push[1];
                c[0] = cur.x;
                c[1] = cur.y;
                pushed = true;
            }
        }
        for (uint32_t i = 0; i < numPlanes_; ++i) {
            const CollPlane& p = planes_[i];
            if (!(p.flags & kCollideWall)) continue;
            float dist = dot3(p.n, c) - p.d;
            float nl = sqrtf(p.n[0] * p.n[0] + p.n[1] * p.n[1]);
            if (dist >= radius || nl < 1e-4f) continue;
            if (!apply) return true;
            float m = (radius - dist) / nl + kSkin; // horizontal move clearing the plane
            cur.x += p.n[0] / nl * m;
            cur.y += p.n[1] / nl * m;
            c[0] = cur.x;
            c[1] = cur.y;
            pushed = true;
        }
    }
    return pushed;
}

bool CollisionWorld::cameraRaycast(const Vec3& from, const Vec3& to, float* hitT) const {
    if (!blob_) return false;
    float d[3] = {to.x - from.x, to.y - from.y, to.z - from.z};
    float len = sqrtf(dot3(d, d));
    if (len < 1e-6f) return false;
    const float o[3] = {from.x, from.y, from.z};
    float dir[3] = {d[0] / len, d[1] / len, d[2] / len};
    float best = 2.0f; // in segment parameter units (0..1)

    for (uint32_t i = 0; i < numPlanes_; ++i) {
        const CollPlane& p = planes_[i];
        if (!(p.flags & kCollideCamera)) continue;
        float d0 = dot3(p.n, o) - p.d, dd = dot3(p.n, d);
        if (d0 >= 0.0f && dd < 0.0f) {
            float t = -d0 / dd;
            if (t <= 1.0f && t < best) best = t;
        } else if (d0 < 0.0f) {
            best = 0.0f;
        }
    }

    // Clip the XY segment to the grid rectangle.
    float gx0 = originX_, gy0 = originY_;
    float gx1 = originX_ + float(gridW_) * cellSize_, gy1 = originY_ + float(gridH_) * cellSize_;
    float tEnter = 0.0f, tExit = 1.0f;
    const float pxy[2] = {from.x, from.y};
    const float lo[2] = {gx0, gy0}, hi[2] = {gx1, gy1};
    for (int a = 0; a < 2; ++a) {
        if (fabsf(d[a]) < 1e-9f) {
            if (pxy[a] < lo[a] || pxy[a] >= hi[a]) tExit = -1.0f;
        } else {
            float t0 = (lo[a] - pxy[a]) / d[a], t1 = (hi[a] - pxy[a]) / d[a];
            if (t0 > t1) {
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }
            tEnter = maxf(tEnter, t0);
            tExit = minf(tExit, t1);
        }
    }
    if (tEnter <= tExit && tExit >= 0.0f) {
        const uint32_t s = nextStamp();
        float px = from.x + d[0] * tEnter, py = from.y + d[1] * tEnter;
        int cx = int((px - originX_) * invCell_), cy = int((py - originY_) * invCell_);
        cx = cx < 0 ? 0 : (cx >= gridW_ ? gridW_ - 1 : cx);
        cy = cy < 0 ? 0 : (cy >= gridH_ ? gridH_ - 1 : cy);
        int stepX = d[0] > 0.0f ? 1 : -1, stepY = d[1] > 0.0f ? 1 : -1;
        float tMaxX = INFINITY, tMaxY = INFINITY, tDX = INFINITY, tDY = INFINITY;
        if (fabsf(d[0]) > 1e-9f) {
            float bx = originX_ + float(cx + (stepX > 0 ? 1 : 0)) * cellSize_;
            tMaxX = (bx - from.x) / d[0];
            tDX = cellSize_ / fabsf(d[0]);
        }
        if (fabsf(d[1]) > 1e-9f) {
            float by = originY_ + float(cy + (stepY > 0 ? 1 : 0)) * cellSize_;
            tMaxY = (by - from.y) / d[1];
            tDY = cellSize_ / fabsf(d[1]);
        }
        for (int guard = 0; guard < gridW_ + gridH_ + 2; ++guard) {
            CellLists cl = cell(cx, cy);
            const uint16_t* idx = cl.idx + cl.counts[0] + cl.counts[1] + cl.counts[2] + cl.counts[3];
            for (uint16_t i = 0; i < cl.counts[4]; ++i) {
                uint16_t ti = idx[i];
                if (stamps_[ti] == s) continue;
                stamps_[ti] = s;
                float t = rayTri(o, d, tris_[ti], 1.0f);
                if (t >= 0.0f && t < best) best = t;
            }
            idx += cl.counts[4];
            for (uint16_t i = 0; i < cl.counts[5]; ++i) {
                uint16_t ci = idx[i];
                if (stamps_[numTris_ + ci] == s) continue;
                stamps_[numTris_ + ci] = s;
                float t = rayCapsule(o, dir, caps_[ci]);
                if (t >= 0.0f) {
                    t /= len;
                    if (t <= 1.0f && t < best) best = t;
                }
            }
            float tCellExit = minf(tMaxX, tMaxY);
            if (best <= tCellExit || tCellExit > tExit) break;
            if (tMaxX < tMaxY) {
                cx += stepX;
                tMaxX += tDX;
            } else {
                cy += stepY;
                tMaxY += tDY;
            }
            if (cx < 0 || cy < 0 || cx >= gridW_ || cy >= gridH_) break;
        }
    }
    if (best > 1.0f) return false;
    if (hitT) *hitT = best;
    return true;
}

int CollisionWorld::triggersAt(const Vec3& p, float radius, int* out, int maxOut) const {
    int n = 0;
    const float c[3] = {p.x, p.y, p.z};
    for (uint32_t i = 0; i < numTriggers_; ++i) {
        const CollTrigger& tr = triggers_[i];
        if (c[0] + radius < tr.min[0] || c[0] - radius > tr.max[0] || c[1] + radius < tr.min[1] ||
            c[1] - radius > tr.max[1] || c[2] + radius < tr.min[2] || c[2] - radius > tr.max[2])
            continue;
        bool hit = false;
        float q[3];
        for (uint32_t k = 0; k < tr.count && !hit; ++k) {
            float rr = radius;
            if (tr.kind == 0) {
                const CollCap& cp = caps_[tr.first + k];
                closestPtSeg(c, cp.a, cp.b, q);
                rr += cp.r;
            } else {
                const CollTri& t = tris_[tr.first + k];
                closestPtTri(c, t.v0, t.v1, t.v2, q);
            }
            float v[3] = {c[0] - q[0], c[1] - q[1], c[2] - q[2]};
            hit = dot3(v, v) <= rr * rr;
        }
        if (!hit) continue;
        if (n < maxOut) out[n] = static_cast<int>(i);
        ++n;
    }
    return n;
}

} // namespace tt::world
