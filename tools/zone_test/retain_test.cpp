// Host test for the zone retain cache: a chunk that leaves the draw set stays
// resident, and a near mesh that is still resident is not replaced by the far
// blob. Build: see the comment in main().
#include "world/zone.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace tt::log {
void write(const char*, ...) {}
}

namespace {

void append(std::vector<uint8_t>& b, const void* p, size_t n) {
    const auto* c = static_cast<const uint8_t*>(p);
    b.insert(b.end(), c, c + n);
}
void u16(std::vector<uint8_t>& b, uint16_t v) { append(b, &v, 2); }
void u32(std::vector<uint8_t>& b, uint32_t v) { append(b, &v, 4); }
void f32(std::vector<uint8_t>& b, float v) { append(b, &v, 4); }
void align128(std::vector<uint8_t>& b) {
    while (b.size() % 128) b.push_back(0);
}
void patch32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }

// One untextured triangle. geom = 3*16 + 3*2 = 54.
void writeTtm(std::vector<uint8_t>& b) {
    const size_t base = b.size();
    // header filled after we know offsets
    b.resize(b.size() + 64, 0);
    const uint32_t batchOff = 64, vertOff = 128, indexOff = 176;
    b.resize(base + indexOff + 6, 0);
    b[base + 0] = 'T'; b[base + 1] = 'T'; b[base + 2] = 'M'; b[base + 3] = '1';
    const uint16_t version = 1, flags = 0, ntex = 0, nbatch = 1;
    std::memcpy(b.data() + base + 4, &version, 2);
    std::memcpy(b.data() + base + 6, &flags, 2);
    std::memcpy(b.data() + base + 8, &ntex, 2);
    std::memcpy(b.data() + base + 10, &nbatch, 2);
    const uint32_t nv = 3, ni = 3;
    std::memcpy(b.data() + base + 12, &nv, 4);
    std::memcpy(b.data() + base + 16, &ni, 4);
    const float scale[3] = {1, 1, 1}, origin[3] = {0, 0, 0};
    std::memcpy(b.data() + base + 20, scale, 12);
    std::memcpy(b.data() + base + 32, origin, 12);
    const uint32_t texOff = 64, reserved = 0;
    std::memcpy(b.data() + base + 44, &texOff, 4);
    std::memcpy(b.data() + base + 48, &batchOff, 4);
    std::memcpy(b.data() + base + 52, &vertOff, 4);
    std::memcpy(b.data() + base + 56, &indexOff, 4);
    std::memcpy(b.data() + base + 60, &reserved, 4);
    // batch: texture 65535, 3 verts, 3 indices
    const uint16_t none = 65535;
    std::memcpy(b.data() + base + batchOff, &none, 2);
    const uint32_t fv = 0, numV = 3, fi = 0, numI = 3;
    std::memcpy(b.data() + base + batchOff + 8, &fv, 4);
    std::memcpy(b.data() + base + batchOff + 12, &numV, 4);
    std::memcpy(b.data() + base + batchOff + 16, &fi, 4);
    std::memcpy(b.data() + base + batchOff + 20, &numI, 4);
    const uint16_t idx[3] = {0, 1, 2};
    std::memcpy(b.data() + base + indexOff, idx, 6);
}

// Minimal TTC1: 1x1 grid, no triangles.
void writeCollision(std::vector<uint8_t>& b) {
    const size_t base = b.size();
    b.resize(base + 112, 0);
    b[base] = 'T'; b[base + 1] = 'T'; b[base + 2] = 'C'; b[base + 3] = '1';
    const uint16_t version = 1;
    std::memcpy(b.data() + base + 4, &version, 2);
    const float cell = 1.0f;
    std::memcpy(b.data() + base + 16, &cell, 4);
    const uint16_t gw = 1, gh = 1;
    std::memcpy(b.data() + base + 20, &gw, 2);
    std::memcpy(b.data() + base + 22, &gh, 2);
    const float hi[3] = {1, 1, 1};
    std::memcpy(b.data() + base + 52, hi, 12);
    const uint32_t cellTable = 96, cellData = 100, cellCount = 6, end = 112, fileSize = 112;
    std::memcpy(b.data() + base + 64, &end, 4);       // tri
    std::memcpy(b.data() + base + 68, &end, 4);       // cap
    std::memcpy(b.data() + base + 72, &end, 4);       // plane
    std::memcpy(b.data() + base + 76, &cellTable, 4);
    std::memcpy(b.data() + base + 80, &cellData, 4);
    std::memcpy(b.data() + base + 84, &cellCount, 4);
    std::memcpy(b.data() + base + 88, &end, 4);       // trigger
    std::memcpy(b.data() + base + 92, &fileSize, 4);
}

void writeChunk(std::vector<uint8_t>& b, uint32_t zone, uint32_t firstVis, uint32_t numVis,
                uint32_t off, uint32_t size, float x, uint32_t firstFloor, const char* name, bool v2) {
    u32(b, zone); u32(b, firstVis); u32(b, numVis);
    u32(b, off); u32(b, size); u32(b, size ? 54 : 0); u32(b, 0); u32(b, size ? 1 : 0); u32(b, size ? 1 : 0);
    f32(b, x); f32(b, 0); f32(b, 0);
    f32(b, x + 2); f32(b, 2); f32(b, 2);
    u32(b, firstFloor); u32(b, 1);
    char n[12] = {};
    std::snprintf(n, sizeof n, "%s", name);
    append(b, n, 12);
    if (v2) {
        u32(b, 0); u32(b, 0); u32(b, 0); u32(b, 0); u32(b, 0); u32(b, 0);
    }
}

void writeFloor(std::vector<uint8_t>& b, float x0, float y0, float x1, float y1, float x2, float y2,
                uint16_t cell, uint16_t zone) {
    f32(b, x0); f32(b, y0); f32(b, 0);
    f32(b, x1); f32(b, y1); f32(b, 0);
    f32(b, x2); f32(b, y2); f32(b, 0);
    append(b, &cell, 2);
    append(b, &zone, 2);
}

void save(const char* path, const std::vector<uint8_t>& b) {
    FILE* f = std::fopen(path, "wb");
    assert(f);
    assert(std::fwrite(b.data(), 1, b.size(), f) == b.size());
    std::fclose(f);
}

std::vector<uint8_t> buildV1() {
    std::vector<uint8_t> ttm;
    writeTtm(ttm);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    // placeholders; model offsets patched after the tables
    writeChunk(b, 2100, 0, 1, 0, static_cast<uint32_t>(ttm.size()), 0, 0, "a", false);
    writeChunk(b, 2101, 1, 1, 0, static_cast<uint32_t>(ttm.size()), 600, 1, "b", false);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0); u16(b, 1);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 2100);
    writeFloor(b, 596, -4, 604, -4, 600, 6, 1, 2101);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    const uint32_t model0 = static_cast<uint32_t>(b.size());
    append(b, ttm.data(), ttm.size());
    align128(b);
    const uint32_t model1 = static_cast<uint32_t>(b.size());
    append(b, ttm.data(), ttm.size());
    // patch chunk model offset/size (fields 3 and 4 of each 80-byte record)
    patch32(b, chunkOff + 12, model0);
    patch32(b, chunkOff + 80 + 12, model1);
    b[0] = 'T'; b[1] = 'Z'; b[2] = '0'; b[3] = '1';
    const uint16_t version = 1;
    std::memcpy(b.data() + 4, &version, 2);
    uint32_t fields[14] = {2100, 2, 2, 2, 0, chunkOff, visOff, floorOff, collisionOff, collisionOff, 112,
                           static_cast<uint32_t>(b.size()), 0, 0};
    std::memcpy(b.data() + 8, fields, sizeof fields);
    return b;
}

std::vector<uint8_t> buildV2() {
    std::vector<uint8_t> ttm;
    writeTtm(ttm);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 2100, 0, 1, 0, static_cast<uint32_t>(ttm.size()), 0, 0, "near", true);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -1000, -1000, 1000, -1000, 0, 1000, 0, 2100);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    const uint32_t model0 = static_cast<uint32_t>(b.size());
    append(b, ttm.data(), ttm.size());
    patch32(b, chunkOff + 12, model0);
    b[0] = 'T'; b[1] = 'Z'; b[2] = '0'; b[3] = '1';
    const uint16_t version = 2;
    std::memcpy(b.data() + 4, &version, 2);
    uint32_t fields[14] = {2100, 1, 1, 1, 0, chunkOff, visOff, floorOff, collisionOff, collisionOff, 112,
                           static_cast<uint32_t>(b.size()), 0, 0};
    std::memcpy(b.data() + 8, fields, sizeof fields);
    return b;
}

} // namespace

int main() {
    const char* v1 = "/tmp/tt_retain_v1.tz";
    const char* v2 = "/tmp/tt_retain_v2.tz";
    save(v1, buildV1());
    save(v2, buildV2());

    tt::world::Zone zone;
    assert(zone.load(v1));
    const tt::world::Vec3 a{0, 0, 1}, b{600, 0, 1};
    assert(zone.zoneAt(a) == 2100);
    assert(zone.zoneAt(b) == 2101);
    assert(zone.update(a));
    assert(zone.chunkLod(0) == 0);
    assert(zone.chunkLod(1) == -1);
    assert(zone.detailReloads() == 1);
    assert(zone.triangles() == 1 && zone.drawCalls() == 1);
    const uint32_t geomOne = zone.geometryBytes();
    assert(geomOne == 54);

    assert(zone.update(b));
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 0);
    assert(zone.detailReloads() == 2);
    assert(zone.triangles() == 1);          // hidden chunk is not submitted
    assert(zone.geometryBytes() == 108);    // both stay resident
    const uint32_t loads = zone.loadsQueued();

    assert(zone.update(a));
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 0);
    assert(zone.detailReloads() == 2);
    assert(zone.loadsQueued() == loads);    // coming back did not read the disk

    zone.setRetainBudget(geomOne, 8u * 1024u * 1024u);
    assert(zone.update(a));
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == -1);
    assert(zone.geometryBytes() == geomOne);
    assert(zone.detailReloads() == 2);

    assert(zone.update(b));
    assert(zone.chunkLod(1) == 0 && zone.chunkLod(0) == -1);
    assert(zone.detailReloads() == 3);      // the evicted chunk had to be read again
    zone.unload();

    assert(zone.load(v2));
    const tt::world::Vec3 near{0, 0, 1}, mid{200, 0, 1};
    assert(zone.zoneAt(near) == 2100 && zone.zoneAt(mid) == 2100);
    assert(zone.update(near));
    assert(zone.chunkLod(0) == 0 && !zone.highParked(0));
    assert(zone.detailReloads() == 1);
    assert(zone.triangles() == 1);
    const uint32_t queued = zone.loadsQueued();
    assert(zone.update(mid));
    assert(zone.chunkLod(0) == 0 && !zone.highParked(0));
    assert(zone.triangles() == 1);          // the near mesh stays; the far blob is not loaded
    assert(zone.geometryBytes() == 54);
    assert(zone.detailReloads() == 1 && zone.loadsQueued() == queued);
    assert(zone.update(near));
    assert(zone.chunkLod(0) == 0 && !zone.highParked(0));
    assert(zone.detailReloads() == 1 && zone.loadsQueued() == queued);
    assert(zone.triangles() == 1);
    zone.unload();

    assert(zone.load(v1));
    assert(zone.update(a));
    zone.warmAround(a, 2000.0f);
    assert(zone.warming());
    assert(zone.update(a));
    assert(!zone.warming());
    assert(zone.chunkLod(1) == 0);
    assert(zone.detailReloads() == 2);
    assert(zone.triangles() == 1 && zone.geometryBytes() == 108);
    zone.unload();

    std::printf("retain: hidden chunks stay resident, parked detail returns without a reload\n");
    return 0;
}
