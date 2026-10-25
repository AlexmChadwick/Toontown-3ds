#include "world/level_spec.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tt::world {

namespace {

struct Header {
    char magic[4];
    uint16_t version, numEntities;
    uint32_t attrOffset, numAttrs, floatOffset, numFloats, stringOffset, stringBytes;
};
static_assert(sizeof(Header) == 32, "must match tlv.py HEADER");

// p' = x*r0 + y*r1 + z*r2 + r3 for a 3x4 row-major transform.
void transformPoint(const float m[12], const float in[3], float out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = in[0] * m[c] + in[1] * m[3 + c] + in[2] * m[6 + c] + m[9 + c];
}

// out = a then b (both 3x4 row-major, as LevelEntity::world).
void compose(const float a[12], const float b[12], float out[12]) {
    float r[12];
    for (int row = 0; row < 4; ++row)
        for (int c = 0; c < 3; ++c)
            r[row * 3 + c] = a[row * 3] * b[c] + a[row * 3 + 1] * b[3 + c] + a[row * 3 + 2] * b[6 + c] +
                             (row == 3 ? b[9 + c] : 0.0f);
    std::memcpy(out, r, sizeof(r));
}

} // namespace

LevelSpec::~LevelSpec() { unload(); }

void LevelSpec::unload() {
    ++revision_;
    for (auto &entry : findCache_)
        entry = FindEntry{};
    for (int i = 0; i < numRooms_; ++i) {
        std::free(rooms_[i].data);
        rooms_[i] = Room{};
    }
    numRooms_ = numEntities_ = 0;
}

bool LevelSpec::load(const char* path) {
    unload();
    return append(path, nullptr) == 0;
}

int LevelSpec::append(const char* path, const float placement[12]) {
    if (numRooms_ >= kMaxRooms) return -1;
    FILE* f = std::fopen(path, "rb");
    if (!f) return -1;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 4 * 1024 * 1024) {
        std::fclose(f);
        return -1;
    }
    void* buf = std::malloc(size_t(size));
    const bool ok = buf && std::fread(buf, 1, size_t(size), f) == size_t(size);
    std::fclose(f);
    if (!ok) {
        std::free(buf);
        return -1;
    }
    Room r;
    r.data = static_cast<uint8_t*>(buf);
    auto fail = [&r]() {
        std::free(r.data);
        return -1;
    };
    const uint32_t n = uint32_t(size);
    Header h;
    if (n < sizeof(h)) return fail();
    std::memcpy(&h, r.data, sizeof(h));
    const uint64_t entEnd = sizeof(h) + uint64_t(h.numEntities) * sizeof(LevelEntity);
    if (std::memcmp(h.magic, "TLV1", 4) != 0 || h.version != 1 || entEnd > h.attrOffset ||
        uint64_t(h.attrOffset) + uint64_t(h.numAttrs) * sizeof(Attr) > h.floatOffset ||
        uint64_t(h.floatOffset) + uint64_t(h.numFloats) * 4 > h.stringOffset ||
        uint64_t(h.stringOffset) + h.stringBytes > n || (h.floatOffset & 3) || (h.attrOffset & 3))
        return fail();
    r.entities = reinterpret_cast<LevelEntity*>(r.data + sizeof(h));
    r.attrs = reinterpret_cast<const Attr*>(r.data + h.attrOffset);
    r.floats = reinterpret_cast<float*>(r.data + h.floatOffset);
    r.strings = reinterpret_cast<const char*>(r.data + h.stringOffset);
    r.numEntities = h.numEntities;
    r.numAttrs = h.numAttrs;
    r.numFloats = h.numFloats;
    r.stringBytes = h.stringBytes;
    while (r.numSpec < r.numEntities && !(r.entities[r.numSpec].flags & kLevelSynthetic)) r.numSpec++;
    for (int i = 0; i < r.numEntities; ++i) {
        const LevelEntity& e = r.entities[i];
        if (uint64_t(e.firstAttr) + e.numAttrs > r.numAttrs || e.type[23] != 0) return fail();
    }
    for (uint32_t i = 0; i < r.numAttrs; ++i) {
        const Attr& a = r.attrs[i];
        const bool str = a.kind == uint8_t(LevelAttrKind::String);
        if (a.name >= r.stringBytes || (str ? uint64_t(a.data) + a.count >= r.stringBytes
                                            : uint64_t(a.data) + a.count > r.numFloats))
            return fail();
    }
    if (r.stringBytes == 0 || r.strings[r.stringBytes - 1] != 0) return fail();
    const int room = numRooms_;
    for (int i = 0; i < r.numEntities; ++i) r.entities[i].room = uint16_t(room);
    r.first = numEntities_;
    rooms_[room] = r;
    numRooms_++;
    ++revision_;
    for (auto &entry : findCache_)
        entry = FindEntry{};
    numEntities_ += r.numEntities;
    if (placement) place(rooms_[room], placement);
    return room;
}

void LevelSpec::place(Room& r, const float placement[12]) {
    for (int i = 0; i < r.numEntities; ++i) {
        LevelEntity& e = r.entities[i];
        compose(e.world, placement, e.world);
        // Attributes baked in level-root space (bake_level.py build_tlv / spin_props).
        for (uint16_t k = 0; k < e.numAttrs; ++k) {
            const Attr& a = r.attrs[e.firstAttr + k];
            const char* name = r.strings + a.name;
            if (a.kind != uint8_t(LevelAttrKind::List)) continue;
            float* v = r.floats + a.data;
            if (std::strcmp(name, "pathWorld") == 0) {
                for (int p = 0; p + 2 < a.count; p += 3) transformPoint(placement, v + p, v + p);
            } else if (std::strcmp(name, "world") == 0 && a.count == 12) {
                compose(v, placement, v);
            }
        }
    }
}

const LevelEntity& LevelSpec::entity(int index) const {
    int room = numRooms_ - 1;
    while (room > 0 && rooms_[room].first > index) --room;
    return rooms_[room].entities[index - rooms_[room].first];
}

const LevelEntity* LevelSpec::find(int room, uint32_t entId) const {
    if (room < 0 || room >= numRooms_)
        return nullptr;
    // Direct mapped, with full keys: collisions only cause a reference lookup.
    const uint32_t slot = ((entId * 2654435761u) ^ (uint32_t(room) * 2246822519u)) >> (32 - 8);
    FindEntry &entry = findCache_[slot];
    if (entry.room == room && entry.entity == entId)
        return entry.result;
    entry.room = room;
    entry.entity = entId;
    entry.result = findReference(room, entId);
    return entry.result;
}
const LevelEntity *LevelSpec::findReference(int room, uint32_t entId) const {
    if (room < 0 || room >= numRooms_) return nullptr;
    const Room& r = rooms_[room];
    int lo = 0, hi = r.numSpec - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const uint32_t id = r.entities[mid].entId;
        if (id == entId) return &r.entities[mid];
        if (id < entId) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

const LevelEntity* LevelSpec::findSynthetic(int room, const char* type, uint32_t id) const {
    if (room < 0 || room >= numRooms_) return nullptr;
    const Room& r = rooms_[room];
    for (int i = r.numSpec; i < r.numEntities; ++i)
        if (r.entities[i].entId == id && std::strcmp(r.entities[i].type, type) == 0) return &r.entities[i];
    return nullptr;
}

int LevelSpec::next(const char* type, int index) const {
    for (int i = index + 1; i < numEntities_; ++i)
        if (std::strcmp(entity(i).type, type) == 0) return i;
    return -1;
}

const LevelSpec::Attr* LevelSpec::attr(const LevelEntity& e, const char* name) const {
    if (e.room >= numRooms_) return nullptr;
    const Attr* attrs_ = rooms_[e.room].attrs;
    const char* strings_ = rooms_[e.room].strings;
    int lo = int(e.firstAttr), hi = int(e.firstAttr) + e.numAttrs - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const int c = std::strcmp(strings_ + attrs_[mid].name, name);
        if (c == 0) return &attrs_[mid];
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

LevelAttrKind LevelSpec::kind(const LevelEntity& e, const char* name) const {
    const Attr* a = attr(e, name);
    return a ? LevelAttrKind(a->kind) : LevelAttrKind::Missing;
}

bool LevelSpec::getNumber(const LevelEntity& e, const char* name, float* out) const {
    const Attr* a = attr(e, name);
    if (!a || a->kind != uint8_t(LevelAttrKind::Number)) return false;
    *out = rooms_[e.room].floats[a->data];
    return true;
}

float LevelSpec::number(const LevelEntity& e, const char* name, float fallback) const {
    float v;
    return getNumber(e, name, &v) ? v : fallback;
}

int32_t LevelSpec::integer(const LevelEntity& e, const char* name, int32_t fallback) const {
    float v;
    return getNumber(e, name, &v) ? int32_t(std::lround(v)) : fallback;
}

int LevelSpec::getFloats(const LevelEntity& e, const char* name, float* out, int capacity) const {
    const Attr* a = attr(e, name);
    if (!a || (a->kind != uint8_t(LevelAttrKind::Vector) && a->kind != uint8_t(LevelAttrKind::List) &&
               a->kind != uint8_t(LevelAttrKind::Number)))
        return 0;
    for (int i = 0; i < a->count && i < capacity; ++i) out[i] = rooms_[e.room].floats[a->data + i];
    return a->count;
}

const char* LevelSpec::string(const LevelEntity& e, const char* name) const {
    const Attr* a = attr(e, name);
    return a && a->kind == uint8_t(LevelAttrKind::String) ? rooms_[e.room].strings + a->data : nullptr;
}

void LevelSpec::toWorld(const LevelEntity& e, const float in[3], float out[3]) { transformPoint(e.world, in, out); }

void LevelSpec::toWorldDir(const LevelEntity& e, const float in[3], float out[3]) {
    const float* m = e.world;
    for (int c = 0; c < 3; ++c) out[c] = in[0] * m[c] + in[1] * m[3 + c] + in[2] * m[6 + c];
}

float LevelSpec::worldHeading(const LevelEntity& e) {
    // Panda heading: +Y forward, H rotates counter-clockwise about +Z.
    return std::atan2(-e.world[3], e.world[4]) * 57.29577951f;
}

} // namespace tt::world
