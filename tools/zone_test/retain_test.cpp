// Host test for the zone retain cache: a chunk that leaves the draw set stays
// resident, and a near mesh that is still resident is not replaced by the far
// blob. Build: see the comment in main().
#include "render/quality.h"
#include "world/zone.h"

#include <algorithm>
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
void patchBounds(std::vector<uint8_t>& b, size_t chunk, float x0, float y0, float z0,
                 float x1, float y1, float z1) {
    const float v[6] = {x0, y0, z0, x1, y1, z1};
    std::memcpy(b.data() + chunk + 36, v, sizeof v);
}

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

// MickeyFont decals plus untextured batches. letterCount < 0 marks every batch.
// validModel counts the decals; text density is that count over the mesh's draws.
void writeLetterTtm(std::vector<uint8_t>& b, int batches, int letterCount = -1) {
    if (letterCount < 0 || letterCount > batches) letterCount = batches;
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
        const uint16_t tex = i < letterCount ? 0 : 65535;
        std::memcpy(b.data() + at, &tex, 2);
        if (i < letterCount) b[at + 5] = 1; // layer: decal letter
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

// Near mesh is several triangles; far mesh is one. This chunk is the cell, so
// it reads the near mesh and does not hold the card.
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
std::vector<uint8_t> buildSigns(float signX = 200) {
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
    writeChunk(b, 1002, 4, 1, 0, letI.size, signX, 2, "sign", true);
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

// 64x64 L8 with a full mip chain (5,440 bytes). validModel requires chunk.tex to match.
void writeSolidTex(std::vector<uint8_t>& b, int side) {
    uint32_t bytes = 0;
    int mips = 0;
    for (int w = side; w >= 8; w >>= 1) {
        bytes += static_cast<uint32_t>(w) * static_cast<uint32_t>(w);
        ++mips;
    }
    const uint32_t texOff = 64;
    const uint32_t batchOff = texOff + 48;
    const uint32_t vertOff = batchOff + 24;
    const uint32_t indexOff = vertOff + 48;
    const uint32_t dataOff = indexOff + 6;
    const size_t base = b.size();
    b.resize(base + dataOff + bytes, 0);
    b[base + 0] = 'T'; b[base + 1] = 'T'; b[base + 2] = 'M'; b[base + 3] = '1';
    const uint16_t version = 1, flags = 0, ntex = 1, nbatch = 1;
    std::memcpy(b.data() + base + 4, &version, 2);
    std::memcpy(b.data() + base + 6, &flags, 2);
    std::memcpy(b.data() + base + 8, &ntex, 2);
    std::memcpy(b.data() + base + 10, &nbatch, 2);
    const uint32_t nv = 3, ni = 3;
    std::memcpy(b.data() + base + 12, &nv, 4);
    std::memcpy(b.data() + base + 16, &ni, 4);
    const float scale[3] = {1, 1, 1};
    std::memcpy(b.data() + base + 20, scale, 12);
    std::memcpy(b.data() + base + 44, &texOff, 4);
    std::memcpy(b.data() + base + 48, &batchOff, 4);
    std::memcpy(b.data() + base + 52, &vertOff, 4);
    std::memcpy(b.data() + base + 56, &indexOff, 4);
    const uint16_t w = static_cast<uint16_t>(side), h = w;
    const uint8_t fmt = 7;
    std::memcpy(b.data() + base + texOff, &w, 2);
    std::memcpy(b.data() + base + texOff + 2, &h, 2);
    b[base + texOff + 4] = fmt;
    b[base + texOff + 5] = static_cast<uint8_t>(mips);
    std::memcpy(b.data() + base + texOff + 8, &dataOff, 4);
    std::memcpy(b.data() + base + texOff + 12, &bytes, 4);
    const uint16_t none = 65535;
    std::memcpy(b.data() + base + batchOff, &none, 2);
    const uint32_t numV = 3, numI = 3;
    std::memcpy(b.data() + base + batchOff + 12, &numV, 4);
    std::memcpy(b.data() + base + batchOff + 20, &numI, 4);
    const uint16_t idx[3] = {0, 1, 2};
    std::memcpy(b.data() + base + indexOff, idx, 6);
}

// Big chunk with an empty far mesh in front of a small chunk that still needs
// its far mesh. The big chunk's near read starts in the same update, and the
// card does not wait for the small chunk.
std::vector<uint8_t> buildOverlap() {
    std::vector<uint8_t> near, far;
    writeTtmN(near, 4);
    writeTtmN(far, 1);
    const MeshInfo ni = meshInfo(near, 4, 0), fi = meshInfo(far, 1, 0);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 2100, 0, 2, 0, ni.size, 0, 0, "big", true);
    writeChunk(b, 2100, 2, 1, 0, ni.size, 70, 1, "small", true);
    patchBounds(b, chunkOff, 0, 0, 0, 36, 24, 16);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0); u16(b, 1); u16(b, 1);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 2100);
    writeFloor(b, 66, -4, 74, -4, 70, 6, 1, 2100);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    const uint32_t bigNear = static_cast<uint32_t>(b.size());
    append(b, near.data(), near.size());
    align128(b);
    const uint32_t smallNear = static_cast<uint32_t>(b.size());
    append(b, near.data(), near.size());
    align128(b);
    const uint32_t smallFar = static_cast<uint32_t>(b.size());
    append(b, far.data(), far.size());
    // Big chunk: near mesh only. The far fields stay zero, so the read is skipped.
    patch32(b, chunkOff + 12, bigNear);
    patch32(b, chunkOff + 16, ni.size);
    patch32(b, chunkOff + 20, ni.geom);
    patch32(b, chunkOff + 24, ni.tex);
    patch32(b, chunkOff + 28, ni.draws);
    patch32(b, chunkOff + 32, ni.tris);
    patchNearFar(b, chunkOff + 104, ni, smallNear, fi, smallFar);
    finishHeader(b, 2100, 2, 3, 2, chunkOff, visOff, floorOff, collisionOff);
    return b;
}

// Ground, a 120-draw filler, a sign, and an out-of-cone chunk whose far texture
// alone is 5,440 bytes. Retain tex is 5,500. The sign fits only if that texture
// is not charged against the cap, and only if the filler gives up its draws.
std::vector<uint8_t> buildSignRoom() {
    std::vector<uint8_t> one, filler, letters, far, solid;
    writeTtmN(one, 1);
    writeTtmN(filler, 120);
    writeLetterTtm(letters, 15);
    writeTtmN(far, 1);
    writeSolidTex(solid, 64);
    const MeshInfo oneI = meshInfo(one, 1, 0);
    const MeshInfo fillI = meshInfo(filler, 120, 0);
    const MeshInfo letI = meshInfo(letters, 15, 64);
    const MeshInfo farI = meshInfo(far, 1, 0);
    const MeshInfo solI = meshInfo(solid, 1, 5440);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 1000, 0, 4, 0, oneI.size, 0, 0, "ground", true);
    writeChunk(b, 1001, 4, 1, 0, fillI.size, 80, 1, "filler", true);
    writeChunk(b, 1002, 5, 1, 0, letI.size, 200, 2, "sign", true);
    writeChunk(b, 1003, 6, 1, 0, solI.size, -400, 3, "hidden", true);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0); u16(b, 1); u16(b, 2); u16(b, 3);
    u16(b, 1); u16(b, 2); u16(b, 3);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 1000);
    writeFloor(b, 76, -4, 84, -4, 80, 6, 1, 1001);
    writeFloor(b, 196, -4, 204, -4, 200, 6, 2, 1002);
    writeFloor(b, -404, -4, -396, -4, -400, 6, 3, 1003);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    auto add=[&](const std::vector<uint8_t>& src) {
        align128(b);
        const uint32_t off = static_cast<uint32_t>(b.size());
        append(b, src.data(), src.size());
        return off;
    };
    const uint32_t groundNear = add(one);
    const uint32_t groundFar = add(far);
    const uint32_t fillerNear = add(filler);
    const uint32_t fillerFar = add(far);
    const uint32_t signNear = add(letters);
    const uint32_t signFar = add(far);
    const uint32_t hidNear = add(solid);
    const uint32_t hidFar = add(solid);
    patchNearFar(b, chunkOff, oneI, groundNear, farI, groundFar);
    patchNearFar(b, chunkOff + 104, fillI, fillerNear, farI, fillerFar);
    patchNearFar(b, chunkOff + 208, letI, signNear, farI, signFar);
    patchNearFar(b, chunkOff + 312, solI, hidNear, solI, hidFar);
    finishHeader(b, 1000, 4, 7, 4, chunkOff, visOff, floorOff, collisionOff);
    return b;
}

// Two chunks of 90 near draws, 100 units apart. Standing on the second keeps the
// first inside lodKeep. Without a camera that must not push the cell past 150 draws.
std::vector<uint8_t> buildKeepWalk() {
    std::vector<uint8_t> near, far;
    writeTtmN(near, 90);
    writeTtmN(far, 10);
    const MeshInfo ni = meshInfo(near, 90, 0), fi = meshInfo(far, 10, 0);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 2100, 0, 2, 0, ni.size, 0, 0, "a", true);
    writeChunk(b, 2101, 2, 2, 0, ni.size, 100, 1, "b", true);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0); u16(b, 1); u16(b, 0); u16(b, 1);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 2100);
    writeFloor(b, 96, -4, 104, -4, 100, 6, 1, 2101);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    uint32_t offs[4];
    for (int i = 0; i < 4; ++i) {
        align128(b);
        offs[i] = static_cast<uint32_t>(b.size());
        const std::vector<uint8_t>& src = (i % 2 == 0) ? near : far;
        append(b, src.data(), src.size());
    }
    patchNearFar(b, chunkOff, ni, offs[0], fi, offs[1]);
    patchNearFar(b, chunkOff + 104, ni, offs[2], fi, offs[3]);
    finishHeader(b, 2100, 2, 4, 2, chunkOff, visOff, floorOff, collisionOff);
    return b;
}

// Eight chunks inside lodNear. Six near meshes fill 120 draws. The next far
// mesh fits under 150. The one after that must not be loaded. The far set of
// the vis list stays at 8*16 = 128 draws so the file cap (150) still accepts it.
std::vector<uint8_t> buildDrawCap() {
    std::vector<uint8_t> near, far;
    writeTtmN(near, 20);
    writeTtmN(far, 16);
    const MeshInfo ni = meshInfo(near, 20, 0), fi = meshInfo(far, 16, 0);
    const int n = 8;
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    for (int i = 0; i < n; ++i) {
        writeChunk(b, 2100, 0, static_cast<uint32_t>(n), 0, ni.size, static_cast<float>(i * 15),
                   static_cast<uint32_t>(i), "cap", true);
    }
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    for (int i = 0; i < n; ++i) u16(b, static_cast<uint16_t>(i));
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    for (int i = 0; i < n; ++i) {
        const float x = static_cast<float>(i * 15);
        writeFloor(b, x - 4, -4, x + 4, -4, x, 6, static_cast<uint16_t>(i), 2100);
    }
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    uint32_t nearOff[8], farOff[8];
    for (int i = 0; i < n; ++i) {
        align128(b);
        nearOff[i] = static_cast<uint32_t>(b.size());
        append(b, near.data(), near.size());
        align128(b);
        farOff[i] = static_cast<uint32_t>(b.size());
        append(b, far.data(), far.size());
    }
    for (int i = 0; i < n; ++i)
        patchNearFar(b, chunkOff + static_cast<uint32_t>(i) * 104, ni, nearOff[i], fi, farOff[i]);
    finishHeader(b, 2100, static_cast<uint32_t>(n), static_cast<uint32_t>(n), static_cast<uint32_t>(n),
                 chunkOff, visOff, floorOff, collisionOff);
    return b;
}

// Three letter chunks past lodNear. The two densest go near. The big bounds
// win on screen rank; the closer small sign is second. The middle one stays far.
std::vector<uint8_t> buildSignCrowd() {
    std::vector<uint8_t> one, letters, far;
    writeTtmN(one, 1);
    writeLetterTtm(letters, 15);
    writeTtmN(far, 1);
    const MeshInfo oneI = meshInfo(one, 1, 0);
    const MeshInfo letI = meshInfo(letters, 15, 64);
    const MeshInfo farI = meshInfo(far, 1, 0);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 1000, 0, 4, 0, oneI.size, 0, 0, "ground", true);
    writeChunk(b, 1001, 0, 4, 0, letI.size, 200, 1, "near", true);
    writeChunk(b, 1002, 0, 4, 0, letI.size, 220, 2, "mid", true);
    writeChunk(b, 1003, 0, 4, 0, letI.size, 260, 3, "big", true);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0); u16(b, 1); u16(b, 2); u16(b, 3);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 1000);
    writeFloor(b, 196, -4, 204, -4, 200, 6, 1, 1001);
    writeFloor(b, 216, -4, 224, -4, 220, 6, 2, 1002);
    writeFloor(b, 256, -4, 264, -4, 260, 6, 3, 1003);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    auto add=[&](const std::vector<uint8_t>& src) {
        align128(b);
        const uint32_t off = static_cast<uint32_t>(b.size());
        append(b, src.data(), src.size());
        return off;
    };
    const uint32_t gN = add(one), gF = add(far);
    const uint32_t aN = add(letters), aF = add(far);
    const uint32_t bN = add(letters), bF = add(far);
    const uint32_t cN = add(letters), cF = add(far);
    patchNearFar(b, chunkOff, oneI, gN, farI, gF);
    patchNearFar(b, chunkOff + 104, letI, aN, farI, aF);
    patchNearFar(b, chunkOff + 208, letI, bN, farI, bF);
    patchNearFar(b, chunkOff + 312, letI, cN, farI, cF);
    patchBounds(b, chunkOff + 312, 80, -40, 0, 360, 40, 30);
    finishHeader(b, 1000, 4, 4, 4, chunkOff, visOff, floorOff, collisionOff);
    return b;
}

// B is showing its near mesh. Moving onto A makes B the far mesh. The near mesh
// stays until that far read is published; an empty swap must not blank it.
std::vector<uint8_t> buildHold() {
    std::vector<uint8_t> aNear, bNear, far;
    writeTtmN(aNear, 40);
    writeTtmN(bNear, 120);
    writeTtmN(far, 1);
    const MeshInfo aI = meshInfo(aNear, 40, 0);
    const MeshInfo bI = meshInfo(bNear, 120, 0);
    const MeshInfo fI = meshInfo(far, 1, 0);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 2100, 0, 2, 0, aI.size, 0, 0, "a", true);
    writeChunk(b, 2101, 0, 2, 0, bI.size, 20, 1, "b", true);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0); u16(b, 1);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 2100);
    writeFloor(b, 16, -4, 24, -4, 20, 6, 1, 2101);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    auto add=[&](const std::vector<uint8_t>& src) {
        align128(b);
        const uint32_t off = static_cast<uint32_t>(b.size());
        append(b, src.data(), src.size());
        return off;
    };
    const uint32_t aN = add(aNear), aF = add(far), bN = add(bNear), bF = add(far);
    patchNearFar(b, chunkOff, aI, aN, fI, aF);
    patchNearFar(b, chunkOff + 104, bI, bN, fI, bF);
    finishHeader(b, 2100, 2, 2, 2, chunkOff, visOff, floorOff, collisionOff);
    return b;
}

// Far meshes of a street, published, then the toon walks into lodNear.
// The planned near cost is over 150 draws. The fars already on screen must
// stay in the submitted set until those nears publish.
std::vector<uint8_t> buildBlob() {
    std::vector<uint8_t> near, far;
    writeTtmN(near, 40);
    writeTtmN(far, 8);
    const MeshInfo ni = meshInfo(near, 40, 0), fi = meshInfo(far, 8, 0);
    const int n = 8;
    const float xs[8] = {0, 180, 195, 210, 225, 240, 255, 270};
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    for (int i = 0; i < n; ++i)
        writeChunk(b, 2100, 0, static_cast<uint32_t>(n), 0, ni.size, xs[i], static_cast<uint32_t>(i), "st", true);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    for (int i = 0; i < n; ++i) u16(b, static_cast<uint16_t>(i));
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    for (int i = 0; i < n; ++i)
        writeFloor(b, xs[i] - 4, -4, xs[i] + 4, -4, xs[i], 6, static_cast<uint16_t>(i), 2100);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    uint32_t nearOff[8], farOff[8];
    for (int i = 0; i < n; ++i) {
        align128(b);
        nearOff[i] = static_cast<uint32_t>(b.size());
        append(b, near.data(), near.size());
        align128(b);
        farOff[i] = static_cast<uint32_t>(b.size());
        append(b, far.data(), far.size());
    }
    for (int i = 0; i < n; ++i)
        patchNearFar(b, chunkOff + static_cast<uint32_t>(i) * 104, ni, nearOff[i], fi, farOff[i]);
    finishHeader(b, 2100, static_cast<uint32_t>(n), static_cast<uint32_t>(n), static_cast<uint32_t>(n),
                 chunkOff, visOff, floorOff, collisionOff);
    return b;
}

// A close building with one letter decal has more screen rank than the far
// sign, whose batches are all letters. Density keeps the far sign near.
// Retain tex fits one sign.
std::vector<uint8_t> buildLandmark() {
    std::vector<uint8_t> one, decoy, sign, far;
    writeTtmN(one, 1);
    writeLetterTtm(decoy, 15, 1);
    writeLetterTtm(sign, 15, 15);
    writeTtmN(far, 1);
    const MeshInfo oneI = meshInfo(one, 1, 0);
    const MeshInfo decI = meshInfo(decoy, 15, 64);
    const MeshInfo sigI = meshInfo(sign, 15, 64);
    const MeshInfo farI = meshInfo(far, 1, 0);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 1000, 0, 3, 0, oneI.size, 0, 0, "ground", true);
    writeChunk(b, 1001, 0, 3, 0, decI.size, 190, 1, "decoy", true);
    writeChunk(b, 1002, 0, 3, 0, sigI.size, 220, 2, "mark", true);
    patchBounds(b, chunkOff + 104, 165, -25, 0, 215, 25, 20);
    patchBounds(b, chunkOff + 208, 200, -20, 0, 240, 20, 20);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0); u16(b, 1); u16(b, 2);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 1000);
    writeFloor(b, 186, -4, 194, -4, 190, 6, 1, 1001);
    writeFloor(b, 216, -4, 224, -4, 220, 6, 2, 1002);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    auto add=[&](const std::vector<uint8_t>& src) {
        align128(b);
        const uint32_t off = static_cast<uint32_t>(b.size());
        append(b, src.data(), src.size());
        return off;
    };
    const uint32_t gN = add(one), gF = add(far);
    const uint32_t dN = add(decoy), dF = add(far);
    const uint32_t sN = add(sign), sF = add(far);
    patchNearFar(b, chunkOff, oneI, gN, farI, gF);
    patchNearFar(b, chunkOff + 104, decI, dN, farI, dF);
    patchNearFar(b, chunkOff + 208, sigI, sN, farI, sF);
    finishHeader(b, 1000, 3, 3, 3, chunkOff, visOff, floorOff, collisionOff);
    return b;
}

// Same row as buildBlob, but after the card the camera jumps ~295 units onto
// chunks that were out of cone. Draws must not fall below 80% of the pre-card count.
std::vector<uint8_t> buildJump295() {
    std::vector<uint8_t> near, far;
    writeTtmN(near, 40);
    writeTtmN(far, 8);
    const MeshInfo ni = meshInfo(near, 40, 0), fi = meshInfo(far, 8, 0);
    const int n = 12;
    const float xs[12] = {0,   180, 195, 210, 225, 240, 255, 270, 285, 300, 315, 330};
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    for (int i = 0; i < n; ++i)
        writeChunk(b, 2100, 0, static_cast<uint32_t>(n), 0, ni.size, xs[i], static_cast<uint32_t>(i), "st", true);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    for (int i = 0; i < n; ++i) u16(b, static_cast<uint16_t>(i));
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    for (int i = 0; i < n; ++i)
        writeFloor(b, xs[i] - 4, -4, xs[i] + 4, -4, xs[i], 6, static_cast<uint16_t>(i), 2100);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    uint32_t nearOff[12], farOff[12];
    for (int i = 0; i < n; ++i) {
        align128(b);
        nearOff[i] = static_cast<uint32_t>(b.size());
        append(b, near.data(), near.size());
        align128(b);
        farOff[i] = static_cast<uint32_t>(b.size());
        append(b, far.data(), far.size());
    }
    for (int i = 0; i < n; ++i)
        patchNearFar(b, chunkOff + static_cast<uint32_t>(i) * 104, ni, nearOff[i], fi, farOff[i]);
    finishHeader(b, 2100, static_cast<uint32_t>(n), static_cast<uint32_t>(n), static_cast<uint32_t>(n),
                 chunkOff, visOff, floorOff, collisionOff);
    return b;
}

// The tail chunk is inside lodNear and outside the card. It reads far first, then near.
std::vector<uint8_t> buildOnce() {
    std::vector<uint8_t> near, far;
    writeTtmN(near, 4);
    writeTtmN(far, 1);
    const MeshInfo ni = meshInfo(near, 4, 0), fi = meshInfo(far, 1, 0);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 2100, 0, 3, 0, ni.size, 0, 0, "cell", true);
    writeChunk(b, 2100, 0, 3, 0, ni.size, 70, 1, "tail", true);
    writeChunk(b, 2100, 0, 3, 0, ni.size, 140, 2, "card", true);
    patchBounds(b, chunkOff + 208, 80, -40, 0, 220, 40, 40);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0); u16(b, 1); u16(b, 2);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 2100);
    writeFloor(b, 66, -4, 74, -4, 70, 6, 1, 2100);
    writeFloor(b, 136, -4, 144, -4, 140, 6, 2, 2100);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    auto add=[&](const std::vector<uint8_t>& src) {
        align128(b);
        const uint32_t off = static_cast<uint32_t>(b.size());
        append(b, src.data(), src.size());
        return off;
    };
    const uint32_t cN = add(near), cF = add(far);
    const uint32_t tN = add(near), tF = add(far);
    const uint32_t kN = add(near), kF = add(far);
    patchNearFar(b, chunkOff, ni, cN, fi, cF);
    patchNearFar(b, chunkOff + 104, ni, tN, fi, tF);
    patchNearFar(b, chunkOff + 208, ni, kN, fi, kF);
    finishHeader(b, 2100, 3, 3, 3, chunkOff, visOff, floorOff, collisionOff);
    return b;
}

// Few draws, many triangles. The second near mesh must stay far.
void writeFatTtm(std::vector<uint8_t>& b, int batches, int trisEach) {
    const uint32_t nv = static_cast<uint32_t>(batches) * 3;
    const uint32_t ni = static_cast<uint32_t>(batches) * static_cast<uint32_t>(trisEach) * 3;
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
        const uint32_t fv = static_cast<uint32_t>(i) * 3, numV = 3;
        const uint32_t fi = static_cast<uint32_t>(i) * static_cast<uint32_t>(trisEach) * 3;
        const uint32_t numI = static_cast<uint32_t>(trisEach) * 3;
        std::memcpy(b.data() + at + 8, &fv, 4);
        std::memcpy(b.data() + at + 12, &numV, 4);
        std::memcpy(b.data() + at + 16, &fi, 4);
        std::memcpy(b.data() + at + 20, &numI, 4);
        for (int t = 0; t < trisEach; ++t) {
            const uint16_t idx[3] = {0, 1, 2};
            std::memcpy(b.data() + base + indexOff + (fi + static_cast<uint32_t>(t) * 3) * 2, idx, 6);
        }
    }
}

std::vector<uint8_t> buildTriCap() {
    std::vector<uint8_t> near, far;
    writeFatTtm(near, 10, 1200);
    writeTtmN(far, 1);
    const uint32_t nearGeom = 10u * 3u * 16u + 10u * 1200u * 3u * 2u;
    const MeshInfo ni = {static_cast<uint32_t>(near.size()), nearGeom, 0, 10, 12000};
    const MeshInfo fi = meshInfo(far, 1, 0);
    std::vector<uint8_t> b(64, 0);
    align128(b);
    const uint32_t chunkOff = static_cast<uint32_t>(b.size());
    writeChunk(b, 2100, 0, 2, 0, ni.size, 0, 0, "a", true);
    writeChunk(b, 2101, 0, 2, 0, ni.size, 12, 1, "b", true);
    align128(b);
    const uint32_t visOff = static_cast<uint32_t>(b.size());
    u16(b, 0); u16(b, 1);
    align128(b);
    const uint32_t floorOff = static_cast<uint32_t>(b.size());
    writeFloor(b, -4, -4, 4, -4, 0, 6, 0, 2100);
    writeFloor(b, 8, -4, 16, -4, 12, 6, 1, 2101);
    align128(b);
    const uint32_t collisionOff = static_cast<uint32_t>(b.size());
    writeCollision(b);
    align128(b);
    auto add=[&](const std::vector<uint8_t>& src) {
        align128(b);
        const uint32_t off = static_cast<uint32_t>(b.size());
        append(b, src.data(), src.size());
        return off;
    };
    const uint32_t aN = add(near), aF = add(far), bN = add(near), bF = add(far);
    patchNearFar(b, chunkOff, ni, aN, fi, aF);
    patchNearFar(b, chunkOff + 104, ni, bN, fi, bF);
    finishHeader(b, 2100, 2, 2, 2, chunkOff, visOff, floorOff, collisionOff);
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
    // The only chunk is under the camera, so the card does not wait on it and
    // the first read is the near mesh.
    assert(!zone.cardPending() && zone.loading());
    assert(zone.chunkLod(0) == -1 && zone.loadsQueued() == 1);
    assert(zone.completeLoads() == 1);
    assert(zone.chunkLod(0) == 0 && !zone.loading() && !zone.cardPending());
    std::printf("card: cell reads near, distant far still drops the card\n");
    zone.unload();

    assert(zone.load(signPath));
    zone.setCamera({-40.0f, 0.0f, 8.0f}, {250.0f, 0.0f, 4.0f});
    settle(zone, origin);
    std::printf("sign: lod ground/filler/letters %d/%d/%d draws %u tris %u\n",
                zone.chunkLod(0), zone.chunkLod(1), zone.chunkLod(2), zone.drawCalls(), zone.triangles());
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(2) == 0 && zone.chunkLod(1) == 1);
    assert(zone.drawCalls() == 17 && zone.triangles() == 17);
    zone.unload();

    const char* distantSignPath = "/tmp/tt_retain_distant_sign.tz";
    save(distantSignPath, buildSigns(400));
    assert(zone.load(distantSignPath));
    zone.setCamera({-30, 0, 8}, {400, 0, 4});
    settle(zone, origin);
    assert(zone.chunkLod(2) == 0); // sole far sign takes real near geometry, no LOD bias
    zone.unload();

    assert(zone.load(cellPath));
    settle(zone, origin);
    std::printf("cell: no camera lod %d/%d/%d draws %u tris %u\n",
                zone.chunkLod(0), zone.chunkLod(1), zone.chunkLod(2), zone.drawCalls(), zone.triangles());
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 1 && zone.chunkLod(2) == -1);
    assert(zone.triangles() <= 15000 && zone.drawCalls() <= 150);
    zone.unload();

    const char* overlapPath = "/tmp/tt_retain_overlap.tz";
    const char* signRoomPath = "/tmp/tt_retain_signroom.tz";
    const char* keepPath = "/tmp/tt_retain_keep.tz";
    save(overlapPath, buildOverlap());
    save(signRoomPath, buildSignRoom());
    save(keepPath, buildKeepWalk());

    assert(zone.load(overlapPath));
    zone.setCamera({-30.0f, 1.0f, 8.0f}, {80.0f, 1.0f, 4.0f});
    assert(zone.update(origin));
    std::printf("overlap: card %d queued %u lod %d/%d\n", zone.cardPending() ? 1 : 0,
                zone.loadsQueued(), zone.chunkLod(0), zone.chunkLod(1));
    // The cell reads near directly. The distant chunk is the card and reads far
    // in the same update. The card stays up until that far mesh is published.
    assert(zone.cardPending() && zone.loading());
    assert(zone.chunkLod(0) == -1 && zone.chunkLod(1) == -1);
    assert(zone.loadsQueued() == 2);
    assert(zone.completeLoads() == 2);
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 1 && !zone.cardPending());
    const uint32_t overlapNear = zone.loadsQueued();
    assert(zone.update(origin));
    assert(zone.loadsQueued() == overlapNear + 1); // the distant chunk's near read
    zone.unload();

    assert(zone.load(signRoomPath));
    zone.setRetainBudget(6u * 1024u * 1024u, 5500u);
    zone.setCamera({-40.0f, 0.0f, 8.0f}, {250.0f, 0.0f, 4.0f});
    settle(zone, origin);
    std::printf("sign: room lod %d/%d/%d/%d draws %u tex %u\n",
                zone.chunkLod(0), zone.chunkLod(1), zone.chunkLod(2), zone.chunkLod(3),
                zone.drawCalls(), zone.textureBytes());
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(2) == 0 && zone.chunkLod(1) == 1);
    assert(zone.chunkLod(3) == -1);
    assert(zone.drawCalls() == 17 && zone.textureBytes() <= 5500u);
    zone.unload();

    assert(zone.load(keepPath));
    settle(zone, origin);
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 1);
    assert(zone.drawCalls() <= 150 && zone.triangles() <= 15000);
    const tt::world::Vec3 walked{100, 0, 1};
    settle(zone, walked);
    std::printf("keep: walked lod %d/%d draws %u tris %u\n",
                zone.chunkLod(0), zone.chunkLod(1), zone.drawCalls(), zone.triangles());
    assert(zone.chunkLod(0) == 1 && zone.chunkLod(1) == 0);
    assert(zone.drawCalls() <= 150 && zone.triangles() <= 15000);
    zone.unload();

    const char* capPath = "/tmp/tt_retain_cap.tz";
    const char* crowdPath = "/tmp/tt_retain_crowd.tz";
    const char* holdPath = "/tmp/tt_retain_hold.tz";
    save(capPath, buildDrawCap());
    save(crowdPath, buildSignCrowd());
    save(holdPath, buildHold());

    assert(zone.load(capPath));
    zone.setCamera({-30.0f, 0.0f, 8.0f}, {80.0f, 0.0f, 4.0f});
    settle(zone, origin);
    std::printf("cap: old lod %d/%d draws %u tris %u\n",
                zone.chunkLod(6), zone.chunkLod(7), zone.drawCalls(), zone.triangles());
    assert(zone.chunkLod(0) == 0);
    // Reserve far coverage before near upgrades: the former expectation that
    // chunk 7 was absent hid distant scenery to keep an extra near chunk.
    for (int i=0; i<8; ++i) assert(zone.chunkLod(i) >= 0);
    assert(zone.chunkLod(6) == 1 && zone.chunkLod(7) == 1);
    assert(zone.drawCalls() <= 150 && zone.triangles() <= 15000);
    tt::render::setActiveQuality(true);
    zone.setCamera({-22.0f, 0.0f, 8.0f}, {80.0f, 0.0f, 4.0f});
    settle(zone, origin);
    std::printf("cap: new draws %u tris %u\n", zone.drawCalls(), zone.triangles());
    assert(zone.drawCalls() <= 150 && zone.triangles() <= 15000);
    tt::render::setActiveQuality(false);
    zone.unload();

    assert(zone.load(crowdPath));
    zone.setCamera({-40.0f, 0.0f, 8.0f}, {300.0f, 0.0f, 4.0f});
    settle(zone, origin);
    std::printf("sign: crowd lod %d/%d/%d/%d draws %u\n",
                zone.chunkLod(0), zone.chunkLod(1), zone.chunkLod(2), zone.chunkLod(3),
                zone.drawCalls());
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 0 && zone.chunkLod(3) == 0);
    assert(zone.chunkLod(2) == 0); // common 260-unit detail band retains this filler too
    assert(zone.drawCalls() <= 130);
    zone.unload();

    assert(zone.load(holdPath));
    const tt::world::Vec3 onB{20, 0, 1};
    settle(zone, onB);
    assert(zone.chunkLod(1) == 0 && zone.chunkLod(0) == 1);
    const uint32_t beforeMove = zone.loadsQueued();
    assert(zone.update(origin));
    std::printf("hold: shown lod %d queued %u\n", zone.chunkLod(1), zone.loadsQueued());
    assert(zone.chunkLod(1) == 0); // the far replacement is not published yet
    assert(zone.loadsQueued() > beforeMove);
    settle(zone, origin);
    assert(zone.chunkLod(1) == 1 && zone.chunkLod(0) == 0);
    assert(zone.drawCalls() <= 150 && zone.triangles() <= 15000);
    std::printf("hold: published lod %d/%d draws %u\n",
                zone.chunkLod(0), zone.chunkLod(1), zone.drawCalls());
    zone.unload();

    const char* blobPath = "/tmp/tt_retain_blob.tz";
    const char* markPath = "/tmp/tt_retain_mark.tz";
    const char* oncePath = "/tmp/tt_retain_once.tz";
    const char* triPath = "/tmp/tt_retain_tri.tz";
    save(blobPath, buildBlob());
    save(markPath, buildLandmark());
    save(oncePath, buildOnce());
    save(triPath, buildTriCap());

    assert(zone.load(blobPath));
    zone.setCamera({-40.0f, 0.0f, 30.0f}, {270.0f, 0.0f, 4.0f});
    assert(zone.update(origin));
    for (int i = 0; i < 500 && zone.cardPending(); ++i) {
        zone.completeLoads();
        assert(zone.update(origin));
    }
    assert(!zone.cardPending());
    const uint32_t atCard = zone.drawCalls();
    int blobLod[8];
    int blobResident = 0;
    for (int i = 0; i < 8; ++i) {
        blobLod[i] = zone.chunkLod(i);
        if (blobLod[i] >= 0) ++blobResident;
    }
    std::printf("blob: card draws %u resident %d\n", atCard, blobResident);
    assert(atCard >= 48 && blobResident >= 6);
    const uint32_t blobQueued = zone.loadsQueued();
    assert(zone.update({180.0f, 0.0f, 1.0f}));
    std::printf("blob: after walk draws %u queued %u\n", zone.drawCalls(), zone.loadsQueued());
    assert(zone.loadsQueued() > blobQueued);
    assert(zone.drawCalls() >= atCard);
    assert(zone.drawCalls() <= 150 && zone.triangles() <= 15000);
    for (int i = 0; i < 8; ++i) {
        if (blobLod[i] < 0) continue;
        assert(zone.chunkLod(i) == blobLod[i]);
    }
    zone.unload();

    assert(zone.load(markPath));
    zone.setRetainBudget(6u * 1024u * 1024u, 80u);
    zone.setCamera({-40.0f, 0.0f, 8.0f}, {250.0f, 0.0f, 4.0f});
    settle(zone, origin);
    std::printf("sign: landmark lod %d/%d/%d draws %u tex %u\n",
                zone.chunkLod(0), zone.chunkLod(1), zone.chunkLod(2),
                zone.drawCalls(), zone.textureBytes());
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 1 && zone.chunkLod(2) == 0);
    assert(zone.drawCalls() <= 130 && zone.textureBytes() <= 80u);
    zone.unload();

    assert(zone.load(oncePath));
    zone.setCamera({-30.0f, 1.0f, 8.0f}, {160.0f, 1.0f, 4.0f});
    assert(zone.update(origin));
    const uint32_t onceQ = zone.loadsQueued();
    std::printf("once: queued %u\n", onceQ);
    assert(onceQ == 3);
    assert(zone.update(origin));
    assert(zone.loadsQueued() == onceQ);
    settle(zone, origin);
    assert(zone.loadsQueued() >= onceQ + 1);
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 0 && zone.chunkLod(2) == 0);
    zone.unload();

    const char* jumpPath = "/tmp/tt_retain_jump295.tz";
    save(jumpPath, buildJump295());
    assert(zone.load(jumpPath));
    zone.setCamera({-40.0f, 0.0f, 30.0f}, {270.0f, 0.0f, 4.0f});
    assert(zone.update(origin));
    for (int i = 0; i < 500 && (zone.cardPending() || zone.loading()); ++i) {
        zone.completeLoads();
        assert(zone.update(origin));
    }
    assert(!zone.cardPending());
    for (int i = 0; i < 500 && zone.loading(); ++i) {
        zone.completeLoads();
        assert(zone.update(origin));
    }
    const uint32_t jumpCardDraws = zone.drawCalls();
    const uint32_t jumpFloor = jumpCardDraws * 4u / 5u;
    std::printf("jump: card draws %u floor %u\n", jumpCardDraws, jumpFloor);
    assert(jumpCardDraws >= 48);
    zone.setCamera({-40.0f, 0.0f, 30.0f}, {565.0f, 0.0f, 4.0f});
    const tt::world::Vec3 jumpFeet{0.0f, 0.0f, 1.0f};
    uint32_t jumpMin = jumpCardDraws;
    for (int step = 0; step < 400; ++step) {
        assert(zone.update(jumpFeet));
        jumpMin = std::min(jumpMin, zone.drawCalls());
        zone.completeLoads();
        if (!zone.loading() && zone.drawCalls() >= jumpFloor) break;
    }
    std::printf("jump: min draws %u after replan\n", jumpMin);
    assert(jumpMin >= jumpFloor);
    assert(zone.drawCalls() <= 150 && zone.triangles() <= 15000);
    zone.unload();

    assert(zone.load(triPath));
    settle(zone, origin);
    std::printf("tri: lod %d/%d draws %u tris %u\n",
                zone.chunkLod(0), zone.chunkLod(1), zone.drawCalls(), zone.triangles());
    assert(zone.chunkLod(0) == 0 && zone.chunkLod(1) == 1);
    assert(zone.triangles() <= 15000 && zone.drawCalls() <= 150);
    zone.unload();
    return 0;
}
