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

} // namespace

LevelSpec::~LevelSpec() { unload(); }

void LevelSpec::unload() {
    std::free(data_);
    data_ = nullptr;
    entities_ = nullptr;
    attrs_ = nullptr;
    floats_ = nullptr;
    strings_ = nullptr;
    numEntities_ = numSpec_ = 0;
    numAttrs_ = numFloats_ = stringBytes_ = 0;
}

bool LevelSpec::load(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 4 * 1024 * 1024) {
        std::fclose(f);
        return false;
    }
    void* buf = std::malloc(size_t(size));
    const bool ok = buf && std::fread(buf, 1, size_t(size), f) == size_t(size);
    std::fclose(f);
    if (!ok) {
        std::free(buf);
        return false;
    }
    unload();
    data_ = static_cast<uint8_t*>(buf);
    const uint32_t n = uint32_t(size);
    Header h;
    if (n < sizeof(h)) return unload(), false;
    std::memcpy(&h, data_, sizeof(h));
    const uint64_t entEnd = sizeof(h) + uint64_t(h.numEntities) * sizeof(LevelEntity);
    if (std::memcmp(h.magic, "TLV1", 4) != 0 || h.version != 1 || entEnd > h.attrOffset ||
        uint64_t(h.attrOffset) + uint64_t(h.numAttrs) * sizeof(Attr) > h.floatOffset ||
        uint64_t(h.floatOffset) + uint64_t(h.numFloats) * 4 > h.stringOffset ||
        uint64_t(h.stringOffset) + h.stringBytes > n || (h.floatOffset & 3) || (h.attrOffset & 3)) {
        unload();
        return false;
    }
    entities_ = reinterpret_cast<const LevelEntity*>(data_ + sizeof(h));
    attrs_ = reinterpret_cast<const Attr*>(data_ + h.attrOffset);
    floats_ = reinterpret_cast<const float*>(data_ + h.floatOffset);
    strings_ = reinterpret_cast<const char*>(data_ + h.stringOffset);
    numEntities_ = h.numEntities;
    numAttrs_ = h.numAttrs;
    numFloats_ = h.numFloats;
    stringBytes_ = h.stringBytes;
    numSpec_ = 0;
    while (numSpec_ < numEntities_ && !(entities_[numSpec_].flags & kLevelSynthetic)) numSpec_++;
    for (int i = 0; i < numEntities_; ++i) {
        const LevelEntity& e = entities_[i];
        if (uint64_t(e.firstAttr) + e.numAttrs > numAttrs_ || e.type[23] != 0) {
            unload();
            return false;
        }
    }
    for (uint32_t i = 0; i < numAttrs_; ++i) {
        const Attr& a = attrs_[i];
        const bool str = a.kind == uint8_t(LevelAttrKind::String);
        if (a.name >= stringBytes_ || (str ? uint64_t(a.data) + a.count >= stringBytes_
                                           : uint64_t(a.data) + a.count > numFloats_)) {
            unload();
            return false;
        }
    }
    if (stringBytes_ == 0 || strings_[stringBytes_ - 1] != 0) {
        unload();
        return false;
    }
    return true;
}

const LevelEntity* LevelSpec::find(uint32_t entId) const {
    int lo = 0, hi = numSpec_ - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const uint32_t id = entities_[mid].entId;
        if (id == entId) return &entities_[mid];
        if (id < entId) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

const LevelEntity* LevelSpec::findSynthetic(const char* type, uint32_t id) const {
    for (int i = numSpec_; i < numEntities_; ++i)
        if (entities_[i].entId == id && std::strcmp(entities_[i].type, type) == 0) return &entities_[i];
    return nullptr;
}

int LevelSpec::next(const char* type, int index) const {
    for (int i = index + 1; i < numEntities_; ++i)
        if (std::strcmp(entities_[i].type, type) == 0) return i;
    return -1;
}

const LevelSpec::Attr* LevelSpec::attr(const LevelEntity& e, const char* name) const {
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
    *out = floats_[a->data];
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
    for (int i = 0; i < a->count && i < capacity; ++i) out[i] = floats_[a->data + i];
    return a->count;
}

const char* LevelSpec::string(const LevelEntity& e, const char* name) const {
    const Attr* a = attr(e, name);
    return a && a->kind == uint8_t(LevelAttrKind::String) ? strings_ + a->data : nullptr;
}

void LevelSpec::toWorld(const LevelEntity& e, const float in[3], float out[3]) {
    const float* m = e.world;
    for (int c = 0; c < 3; ++c) out[c] = in[0] * m[c] + in[1] * m[3 + c] + in[2] * m[6 + c] + m[9 + c];
}

void LevelSpec::toWorldDir(const LevelEntity& e, const float in[3], float out[3]) {
    const float* m = e.world;
    for (int c = 0; c < 3; ++c) out[c] = in[0] * m[c] + in[1] * m[3 + c] + in[2] * m[6 + c];
}

float LevelSpec::worldHeading(const LevelEntity& e) {
    // Panda heading: +Y forward, H rotates counter-clockwise about +Z.
    return std::atan2(-e.world[3], e.world[4]) * 57.29577951f;
}

} // namespace tt::world
