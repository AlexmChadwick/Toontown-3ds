// Runtime side of TLV1, the baked level spec (writer + format: tools/pipeline/tlv.py, baker:
// tools/pipeline/bake_level.py). One file per level spec (factory, mint room, stage room,
// country club room): sdmc:/3ds/toontown3ds/data/level/<spec>.tlv.
//
// The file is read into one regular-heap block and queried in place; lookups never
// allocate. Free of libctru so host tests can build it.
#pragma once

#include <cstdint>

namespace tt::world {

struct LevelEntity {
    uint32_t entId;
    uint32_t parentEntId;
    uint16_t zoneNum; // owning zone entity (0 = UberZone)
    uint16_t flags;   // kLevelSynthetic
    char type[24];
    float world[12]; // 3x4 row-major, entity -> level root: p' = x*r0 + y*r1 + z*r2 + r3
    float pos[3], hpr[3], scale[3]; // local spec values
    uint32_t firstAttr;
    uint16_t numAttrs;
    uint16_t pad;
};
static_assert(sizeof(LevelEntity) == 128, "must match tlv.py ENTITY");

constexpr uint16_t kLevelSynthetic = 1;

enum class LevelAttrKind : uint8_t { Missing = 0, Number = 1, Vector = 2, String = 3, List = 4, None = 5 };

class LevelSpec {
public:
    LevelSpec() = default;
    ~LevelSpec();
    LevelSpec(const LevelSpec&) = delete;
    LevelSpec& operator=(const LevelSpec&) = delete;

    bool load(const char* path);
    void unload();
    bool loaded() const { return data_ != nullptr; }

    int numEntities() const { return numEntities_; }
    const LevelEntity& entity(int index) const { return entities_[index]; }
    // Spec entities only (binary search); synthetic ones via findSynthetic.
    const LevelEntity* find(uint32_t entId) const;
    const LevelEntity* findSynthetic(const char* type, uint32_t id) const;
    // Iterate entities of one type: start with index -1; returns -1 when done.
    int next(const char* type, int index) const;

    // Attribute access. Numbers include ints and bools (exact floats).
    LevelAttrKind kind(const LevelEntity& e, const char* name) const;
    bool getNumber(const LevelEntity& e, const char* name, float* out) const;
    float number(const LevelEntity& e, const char* name, float fallback = 0.0f) const;
    int32_t integer(const LevelEntity& e, const char* name, int32_t fallback = 0) const;
    // Vector or list: copies up to capacity floats, returns the stored count (0 if missing).
    int getFloats(const LevelEntity& e, const char* name, float* out, int capacity) const;
    // NUL-terminated pointer into the file, or nullptr.
    const char* string(const LevelEntity& e, const char* name) const;

    // Transforms a point/direction from entity space to level-root space.
    static void toWorld(const LevelEntity& e, const float in[3], float out[3]);
    static void toWorldDir(const LevelEntity& e, const float in[3], float out[3]);
    // World heading (degrees) of the entity's +Y axis.
    static float worldHeading(const LevelEntity& e);

private:
    struct Attr {
        uint32_t name;
        uint8_t kind, pad;
        uint16_t count;
        uint32_t data;
    };
    const Attr* attr(const LevelEntity& e, const char* name) const;

    uint8_t* data_ = nullptr;
    const LevelEntity* entities_ = nullptr;
    const Attr* attrs_ = nullptr;
    const float* floats_ = nullptr;
    const char* strings_ = nullptr;
    int numEntities_ = 0;
    int numSpec_ = 0; // spec entities come first, sorted by entId
    uint32_t numAttrs_ = 0, numFloats_ = 0, stringBytes_ = 0;
};

} // namespace tt::world
