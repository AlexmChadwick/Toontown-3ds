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
void writeTtmN(std::vector<uint8_t>& b, int batches) {
    const uint32_t nv = static_cast<uint32_t>(batches) * 3;
    const uint32_t ni = nv;
    const uint32_t batchOff = 64;
    const uint32_t vertOff = batchOff + static_cast<uint32_t>(batches) * 24;
    const uint32_t indexOff = vertOff + nv * 16;
    const size_t base = b.size();
    b.resize(base + indexOff + ni * 2, 0);
    b[base + 0] = 'T'; b[base + 1] = 'T'; b[base + 2] = 'M'; b[base + 3] = '1';
    const uint16_t version = 1, flags = 0, ntex = 0;
    const uint16_t nbatch = static_cast<uint16_t>(batches);
    std::memcpy(b.data() + base + 4, &version, 2);
    std::memcpy(b.data() + base + 6, &flags, 2);
    std::memcpy(b.data() + base + 8, &ntex, 2);
    std::memcpy(b.data() + base + 10, &nbatch, 2);
    std::memcpy(b.data() + base + 12, &nv, 4);
    std::memcpy(b.data() + base + 16, &ni, 4);
    const float scale[3] = {1, 1, 1};
    std::memcpy(b.data() + base + 20, scale, 12);
    const uint32_t texOff = 64;
    std::memcpy(b.data() + base + 44, &texOff, 4);
    std::memcpy(b.data() + base + 48, &batchOff, 4);
    std::memcpy(b.data() + base + 52, &vertOff, 4);
    std::memcpy(b.data() + base + 56, &indexOff, 4);
    for (int i = 0; i < batches; ++i) {
        const size_t at = base + batchOff + static_cast<uint32_t>(i) * 24;
        const uint16_t none = 65535;
        std::memcpy(b.data() + at, &none, 2);
        const uint32_t fv = static_cast<uint32_t>(i) * 3, numV = 3, fi = fv, numI = 3;
        std::memcpy(b.data() + at + 8, &fv, 4);
        std::memcpy(b.data() + at + 12, &numV, 4);
        std::memcpy(b.data() + at + 16, &fi, 4);
        std::memcpy(b.data() + at + 20, &numI, 4);
    }
}

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

// Hall (130 draws) beside a one-draw shop. The shop is inside the draw distance
// and past lodKeep, so the plan keeps the hall near and the shop far. Both must
// be submitted: 131 draws. Walking onto the shop fills the cap with the hall's
// near mesh and demotes the hall to its far mesh.
std::vector<uint8_t> buildBudget() {
    std::vector<uint8_t> near, far;
    writeTtmN(near, 130);
    writeTtmN(far, 1);
    const uint32_t nearGeom = 130u * 54u;
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 2100, 0, 2, 0, static_cast<uint32_t>(near.size()), 0, 0, "hall", true);
    writeChunk(b, 2101, 0, 2, 0, static_cast<uint32_t>(far.size()), 200, 1, "shop", true);
    // writeChunk recorded the small geom/draws. Patch the hall's near mesh.
    patch32(b, chunkOff + 16, static_cast<uint32_t>(near.size()));
    patch32(b, chunkOff + 20, nearGeom);
    patch32(b, chunkOff + 28, 130);
    patch32(b, chunkOff + 32, 130);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0); u16(b, 1);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 2100);
    writeFloor(b, 196, -4, 204, -4, 200, 6, 1, 2101);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    const uint32_t hallNear = static_cast<uint32_t>(b.size());
    append(b, near.data(), near.size());
    align128(b);
    const uint32_t hallFar = static_cast<uint32_t>(b.size());
    append(b, far.data(), far.size());
    align128(b);
    const uint32_t shopNear = static_cast<uint32_t>(b.size());
    append(b, far.data(), far.size());
    align128(b);
    const uint32_t shopFar = static_cast<uint32_t>(b.size());
    append(b, far.data(), far.size());
    patch32(b, chunkOff + 12, hallNear);
    patch32(b, chunkOff + 80, hallFar);          // lowOffset
    patch32(b, chunkOff + 84, static_cast<uint32_t>(far.size()));
    patch32(b, chunkOff + 88, 54);
    patch32(b, chunkOff + 96, 1);
    patch32(b, chunkOff + 100, 1);
    patch32(b, chunkOff + 104 + 12, shopNear);
    patch32(b, chunkOff + 104 + 80, shopFar);
    patch32(b, chunkOff + 104 + 84, static_cast<uint32_t>(far.size()));
    patch32(b, chunkOff + 104 + 88, 54);
    patch32(b, chunkOff + 104 + 96, 1);
    patch32(b, chunkOff + 104 + 100, 1);
    b[0] = 'T'; b[1] = 'Z'; b[2] = '0'; b[3] = '1';
    const uint16_t version = 2;
    std::memcpy(b.data() + 4, &version, 2);
    uint32_t fields[14] = {2100, 2, 2, 2, 0, chunkOff, visOff, floorOff, collisionOff, collisionOff, 112,
                           static_cast<uint32_t>(b.size()), 0, 0};
    std::memcpy(b.data() + 8, fields, sizeof fields);
    return b;
}

struct MeshInfo {
    uint32_t size, geom, tex, draws, tris;
};

MeshInfo meshInfo(const std::vector<uint8_t>& ttm, uint32_t draws, uint32_t tex) {
    const uint32_t geom = draws * 54u;
    return {static_cast<uint32_t>(ttm.size()), geom, tex, draws, draws};
}

// One MickeyFont decal plus untextured batches. validModel flags this as sign letters.
void writeLetterTtm(std::vector<uint8_t>& b, int batches) {
    const uint32_t nv = static_cast<uint32_t>(batches) * 3;
    const uint32_t ni = nv;
    const uint32_t texOff = 64;
    const uint32_t batchOff = texOff + 48;
    const uint32_t vertOff = batchOff + static_cast<uint32_t>(batches) * 24;
    const uint32_t indexOff = vertOff + nv * 16;
    const uint32_t dataOff = indexOff + ni * 2;
    const uint32_t texBytes = 64; // 8x8 L8
    const size_t base = b.size();
    b.resize(base + dataOff + texBytes, 0);
    b[base + 0] = 'T'; b[base + 1] = 'T'; b[base + 2] = 'M'; b[base + 3] = '1';
    const uint16_t version = 1, flags = 0, ntex = 1;
    const uint16_t nbatch = static_cast<uint16_t>(batches);
    std::memcpy(b.data() + base + 4, &version, 2);
    std::memcpy(b.data() + base + 6, &flags, 2);
    std::memcpy(b.data() + base + 8, &ntex, 2);
    std::memcpy(b.data() + base + 10, &nbatch, 2);
    std::memcpy(b.data() + base + 12, &nv, 4);
    std::memcpy(b.data() + base + 16, &ni, 4);
    const float scale[3] = {1, 1, 1};
    std::memcpy(b.data() + base + 20, scale, 12);
    std::memcpy(b.data() + base + 44, &texOff, 4);
    std::memcpy(b.data() + base + 48, &batchOff, 4);
    std::memcpy(b.data() + base + 52, &vertOff, 4);
    std::memcpy(b.data() + base + 56, &indexOff, 4);
    const uint16_t w = 8, h = 8;
    const uint8_t fmt = 7, mips = 1;
    std::memcpy(b.data() + base + texOff, &w, 2);
    std::memcpy(b.data() + base + texOff + 2, &h, 2);
    b[base + texOff + 4] = fmt;
    b[base + texOff + 5] = mips;
    std::memcpy(b.data() + base + texOff + 8, &dataOff, 4);
    std::memcpy(b.data() + base + texOff + 12, &texBytes, 4);
    const char* font = "phase_3_palette_2tmlc_1";
    std::memcpy(b.data() + base + texOff + 16, font, std::strlen(font));
    for (int i = 0; i < batches; ++i) {
        const size_t at = base + batchOff + static_cast<uint32_t>(i) * 24;
        const uint16_t tex = i == 0 ? 0 : 65535;
        std::memcpy(b.data() + at, &tex, 2);
        if (i == 0) b[at + 5] = 1; // layer: decal letter
        const uint32_t fv = static_cast<uint32_t>(i) * 3, numV = 3, fi = fv, numI = 3;
        std::memcpy(b.data() + at + 8, &fv, 4);
        std::memcpy(b.data() + at + 12, &numV, 4);
        std::memcpy(b.data() + at + 16, &fi, 4);
        std::memcpy(b.data() + at + 20, &numI, 4);
        const uint16_t idx[3] = {0, 1, 2};
        std::memcpy(b.data() + base + indexOff + static_cast<uint32_t>(i) * 6, idx, 6);
    }
}

void patchNearFar(std::vector<uint8_t>& b, uint32_t chunkBase, const MeshInfo& near, uint32_t nearOff,
                  const MeshInfo& far, uint32_t farOff) {
    patch32(b, chunkBase + 12, nearOff);
    patch32(b, chunkBase + 16, near.size);
    patch32(b, chunkBase + 20, near.geom);
    patch32(b, chunkBase + 24, near.tex);
    patch32(b, chunkBase + 28, near.draws);
    patch32(b, chunkBase + 32, near.tris);
    patch32(b, chunkBase + 80, farOff);
    patch32(b, chunkBase + 84, far.size);
    patch32(b, chunkBase + 88, far.geom);
    patch32(b, chunkBase + 92, far.tex);
    patch32(b, chunkBase + 96, far.draws);
    patch32(b, chunkBase + 100, far.tris);
}

void finishHeader(std::vector<uint8_t>& b, uint32_t branch, uint32_t chunks, uint32_t vis, uint32_t floors,
                  uint32_t chunkOff, uint32_t visOff, uint32_t floorOff, uint32_t collisionOff) {
    b[0] = 'T'; b[1] = 'Z'; b[2] = '0'; b[3] = '1';
    const uint16_t version = 2;
    std::memcpy(b.data() + 4, &version, 2);
    uint32_t fields[14] = {branch, chunks, vis, floors, 0, chunkOff, visOff, floorOff, collisionOff, collisionOff, 112,
                           static_cast<uint32_t>(b.size()), 0, 0};
    std::memcpy(b.data() + 8, fields, sizeof fields);
}

// Near mesh is several triangles; far mesh is one. The card must publish the far
// mesh before it queues the near mesh.
std::vector<uint8_t> buildCard() {
    std::vector<uint8_t> near, far;
    writeTtmN(near, 4);
    writeTtmN(far, 1);
    const MeshInfo ni = meshInfo(near, 4, 0), fi = meshInfo(far, 1, 0);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 2100, 0, 1, 0, ni.size, 0, 0, "card", true);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 2100);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    const uint32_t nearOff = static_cast<uint32_t>(b.size());
    append(b, near.data(), near.size());
    align128(b);
    const uint32_t farOff = static_cast<uint32_t>(b.size());
    append(b, far.data(), far.size());
    patchNearFar(b, chunkOff, ni, nearOff, fi, farOff);
    finishHeader(b, 2100, 1, 1, 1, chunkOff, visOff, floorOff, collisionOff);
    return b;
}

// Ground underfoot, a 120-draw filler inside lodNear, and a sign past lodNear
// (200 > 160) but inside twice lodNear (320). The sign must take the near slot.
std::vector<uint8_t> buildSigns() {
    std::vector<uint8_t> one, filler, letters, far;
    writeTtmN(one, 1);
    writeTtmN(filler, 120);
    writeLetterTtm(letters, 15);
    writeTtmN(far, 1);
    const MeshInfo oneI = meshInfo(one, 1, 0);
    const MeshInfo fillI = meshInfo(filler, 120, 0);
    const MeshInfo letI = meshInfo(letters, 15, 64);
    const MeshInfo farI = meshInfo(far, 1, 0);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 1000, 0, 3, 0, oneI.size, 0, 0, "ground", true);
    writeChunk(b, 1001, 3, 1, 0, fillI.size, 80, 1, "filler", true);
    writeChunk(b, 1002, 4, 1, 0, letI.size, 200, 2, "sign", true);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0); u16(b, 1); u16(b, 2); u16(b, 1); u16(b, 2);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 1000);
    writeFloor(b, 76, -4, 84, -4, 80, 6, 1, 1001);
    writeFloor(b, 196, -4, 204, -4, 200, 6, 2, 1002);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    const uint32_t groundNear = static_cast<uint32_t>(b.size());
    append(b, one.data(), one.size());
    align128(b);
    const uint32_t groundFar = static_cast<uint32_t>(b.size());
    append(b, far.data(), far.size());
    align128(b);
    const uint32_t fillerNear = static_cast<uint32_t>(b.size());
    append(b, filler.data(), filler.size());
    align128(b);
    const uint32_t fillerFar = static_cast<uint32_t>(b.size());
    append(b, far.data(), far.size());
    align128(b);
    const uint32_t signNear = static_cast<uint32_t>(b.size());
    append(b, letters.data(), letters.size());
    align128(b);
    const uint32_t signFar = static_cast<uint32_t>(b.size());
    append(b, far.data(), far.size());
    patchNearFar(b, chunkOff, oneI, groundNear, farI, groundFar);
    patchNearFar(b, chunkOff + 104, fillI, fillerNear, farI, fillerFar);
    patchNearFar(b, chunkOff + 208, letI, signNear, farI, signFar);
    finishHeader(b, 1000, 3, 5, 3, chunkOff, visOff, floorOff, collisionOff);
    return b;
}

// Three chunks whose near-plus-far mix passes 150 draws. No camera must refuse
// the last far mesh so the cell stays inside the file cap.
std::vector<uint8_t> buildCellCap() {
    std::vector<uint8_t> near, far;
    writeTtmN(near, 80);
    writeTtmN(far, 40);
    const MeshInfo ni = meshInfo(near, 80, 0), fi = meshInfo(far, 40, 0);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 2100, 0, 3, 0, ni.size, 0, 0, "c0", true);
    writeChunk(b, 2101, 3, 1, 0, ni.size, 12, 1, "c1", true);
    writeChunk(b, 2102, 4, 1, 0, ni.size, 24, 2, "c2", true);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0); u16(b, 1); u16(b, 2); u16(b, 1); u16(b, 2);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 2100);
    writeFloor(b, 8, -4, 16, -4, 12, 6, 1, 2101);
    writeFloor(b, 20, -4, 28, -4, 24, 6, 2, 2102);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    uint32_t offs[6];
    for (int i = 0; i < 6; ++i) {
        align128(b);
        offs[i] = static_cast<uint32_t>(b.size());
        const std::vector<uint8_t>& src = (i % 2 == 0) ? near : far;
        append(b, src.data(), src.size());
    }
    patchNearFar(b, chunkOff, ni, offs[0], fi, offs[1]);
    patchNearFar(b, chunkOff + 104, ni, offs[2], fi, offs[3]);
    patchNearFar(b, chunkOff + 208, ni, offs[4], fi, offs[5]);
    finishHeader(b, 2100, 3, 5, 3, chunkOff, visOff, floorOff, collisionOff);
    return b;
}

void settle(tt::world::Zone& zone, tt::world::Vec3 at) {
    for (int i = 0; i < 500; ++i) {
        assert(zone.update(at));
        const int n = zone.completeLoads();
        if (!zone.loading() && !zone.warming() && n == 0) return;
    }
    assert(false);
}

} // namespace

int main() {
    const char* v1 = "/tmp/tt_retain_v1.tz";
    const char* v2 = "/tmp/tt_retain_v2.tz";
    const char* budget = "/tmp/tt_retain_budget.tz";
    save(v1, buildV1());
    save(v2, buildV2());
    save(budget, buildBudget());

    tt::world::Zone zone;
    assert(zone.load(v1));
    const tt::world::Vec3 a{0, 0, 1}, b{600, 0, 1};
    assert(zone.zoneAt(a) == 2100);
    assert(zone.zoneAt(b) == 2101);
    settle(zone, a);
    assert(zone.chunkLod(0) == 0);
    assert(zone.chunkLod(1) == -1);
    assert(zone.detailReloads() == 1);
    assert(zone.triangles() == 1 && zone.drawCalls() == 1);
    const uint32_t geomOne = zone.geometryBytes();
    assert(geomOne == 54);

    settle(zone, b);
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 0);
    assert(zone.detailReloads() == 2);
    assert(zone.triangles() == 1);          // hidden chunk is not submitted
    assert(zone.geometryBytes() == 108);    // both stay resident
    const uint32_t loads = zone.loadsQueued();

    settle(zone, a);
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 0);
    assert(zone.detailReloads() == 2);
    assert(zone.loadsQueued() == loads);    // coming back did not read the disk

    zone.setRetainBudget(geomOne, 8u * 1024u * 1024u);
    settle(zone, a);
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == -1);
    assert(zone.geometryBytes() == geomOne);
    assert(zone.detailReloads() == 2);

    settle(zone, b);
    assert(zone.chunkLod(1) == 0 && zone.chunkLod(0) == -1);
    assert(zone.detailReloads() == 3);      // the evicted chunk had to be read again
    zone.unload();

    assert(zone.load(v2));
    const tt::world::Vec3 near{0, 0, 1}, mid{200, 0, 1};
    assert(zone.zoneAt(near) == 2100 && zone.zoneAt(mid) == 2100);
    settle(zone, near);
    assert(zone.chunkLod(0) == 0 && !zone.highParked(0));
    assert(zone.detailReloads() == 1);
    assert(zone.triangles() == 1);
    const uint32_t queued = zone.loadsQueued();
    settle(zone, mid);
    assert(zone.chunkLod(0) == 0 && !zone.highParked(0));
    assert(zone.triangles() == 1);          // under the cap the near mesh stays; no far blob
    assert(zone.geometryBytes() == 54);
    assert(zone.detailReloads() == 1 && zone.loadsQueued() == queued);
    settle(zone, near);
    assert(zone.chunkLod(0) == 0 && !zone.highParked(0));
    assert(zone.detailReloads() == 1 && zone.loadsQueued() == queued);
    assert(zone.triangles() == 1);
    zone.unload();

    // The host loader used to mark a chunk resident inside queueLoad, so a warm
    // upload that update() then discarded never showed up. Queue, observe the gap,
    // then publish.
    assert(zone.load(v1));
    settle(zone, a);
    const uint32_t beforeWarm = zone.loadsQueued();
    zone.warmAround(a, 2000.0f);
    assert(zone.warming());
    assert(zone.update(a));
    assert(zone.warming());
    assert(zone.chunkLod(1) == -1);
    assert(zone.completeLoads() >= 1);
    assert(zone.chunkLod(1) == 0);
    assert(!zone.warming());
    assert(zone.loadsQueued() == beforeWarm + 1);
    assert(zone.triangles() == 1 && zone.geometryBytes() == 108);
    // Shrinking the cap while the ring is still the preload set must not evict it.
    // preload ends at the bottom of this update. The next one evicts the neighbour
    // and must not queue it again.
    zone.setRetainBudget(geomOne, 8u * 1024u * 1024u);
    assert(zone.update(a));
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 0);
    assert(zone.completeLoads() == 0);
    const uint32_t afterHold = zone.loadsQueued();
    assert(zone.update(a));
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == -1);
    assert(zone.loadsQueued() == afterHold);
    zone.unload();

    assert(zone.load(budget));
    const tt::world::Vec3 hall{0, 0, 1}, shop{200, 0, 1};
    assert(zone.zoneAt(hall) == 2100 && zone.zoneAt(shop) == 2101);
    zone.setCamera({-30, 0, 8}, {220, 0, 4});
    settle(zone, hall);
    std::printf("cone: at hall lod %d/%d draws %u tris %u\n",
                zone.chunkLod(0), zone.chunkLod(1), zone.drawCalls(), zone.triangles());
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 1);
    assert(zone.drawCalls() == 131 && zone.triangles() == 131);
    settle(zone, shop);
    std::printf("cone: at shop lod %d/%d draws %u tris %u\n",
                zone.chunkLod(0), zone.chunkLod(1), zone.drawCalls(), zone.triangles());
    assert(zone.chunkLod(0) == 1 && zone.chunkLod(1) == 0);
    assert(zone.drawCalls() == 2 && zone.triangles() == 2);
    zone.unload();

    std::printf("retain: hidden chunks stay resident, parked detail returns without a reload\n");
    std::printf("retain: warm uploads publish, in-cone far meshes are not dropped\n");

    const char* cardPath = "/tmp/tt_retain_card.tz";
    const char* signPath = "/tmp/tt_retain_sign.tz";
    const char* cellPath = "/tmp/tt_retain_cell.tz";
    save(cardPath, buildCard());
    save(signPath, buildSigns());
    save(cellPath, buildCellCap());

    assert(zone.load(cardPath));
    zone.setCamera({-20.0f, 0.0f, 8.0f}, {10.0f, 0.0f, 2.0f});
    const tt::world::Vec3 origin{0, 0, 1};
    assert(zone.update(origin));
    assert(zone.cardPending() && zone.loading());
    assert(zone.chunkLod(0) == -1 && zone.loadsQueued() == 1);
    assert(zone.completeLoads() == 1);
    assert(zone.chunkLod(0) == 1 && !zone.cardPending() && zone.loading());
    const uint32_t afterFar = zone.loadsQueued();
    assert(zone.update(origin));
    assert(zone.loadsQueued() == afterFar + 1);
    assert(zone.completeLoads() == 1);
    assert(zone.chunkLod(0) == 0 && !zone.loading() && !zone.cardPending());
    std::printf("card: far mesh drops the card, near streams after\n");
    zone.unload();

    assert(zone.load(signPath));
    zone.setCamera({-40.0f, 0.0f, 8.0f}, {250.0f, 0.0f, 4.0f});
    settle(zone, origin);
    std::printf("sign: lod ground/filler/letters %d/%d/%d draws %u tris %u\n",
                zone.chunkLod(0), zone.chunkLod(1), zone.chunkLod(2), zone.drawCalls(), zone.triangles());
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(2) == 0 && zone.chunkLod(1) == 1);
    assert(zone.drawCalls() == 17 && zone.triangles() == 17);
    zone.unload();

    assert(zone.load(cellPath));
    settle(zone, origin);
    std::printf("cell: no camera lod %d/%d/%d draws %u tris %u\n",
                zone.chunkLod(0), zone.chunkLod(1), zone.chunkLod(2), zone.drawCalls(), zone.triangles());
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 1 && zone.chunkLod(2) == -1);
    assert(zone.triangles() <= 15000 && zone.drawCalls() <= 150);
    zone.unload();
    return 0;
}
