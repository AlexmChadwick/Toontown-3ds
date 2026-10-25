// Runtime side of TLV1, the baked level spec (writer + format: tools/pipeline/tlv.py, baker:
// tools/pipeline/bake_level.py). One file per level spec (factory, mint room, stage room,
// country club room): sdmc:/3ds/toontown3ds/data/level/<spec>.tlv.
//
// A facility floor (bake_facility_floors.py) appends one file per room, each placed by its
// room -> floor transform, so every entity lives in one floor space and carries its room
// index. Each file is read into one regular-heap block and queried in place; lookups never
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
    float world[12]; // 3x4 row-major, entity -> level root (floor space once appended):
                     // p' = x*r0 + y*r1 + z*r2 + r3
    float pos[3], hpr[3], scale[3]; // local spec values
    uint32_t firstAttr;
    uint16_t numAttrs;
    uint16_t room; // the file's index in its LevelSpec (0 in the file; set by append)
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

    static constexpr int kMaxRooms = 16;

    bool load(const char* path); // one level: unload, then append(path, nullptr)
    // Adds a room's file. placement: room -> floor 3x4 (rows as LevelEntity::world), or
    // nullptr for none. Entity transforms and the level-root-space attributes (cog
    // 'pathWorld' points, spin prop 'world') are moved to floor space. Returns the room
    // index, or -1 on a bad file or when kMaxRooms are loaded.
    int append(const char* path, const float placement[12]);
    void unload();
    bool loaded() const { return numRooms_ > 0; }
    int numRooms() const { return numRooms_; }

    // Entities of every room, in room order (an index is valid across rooms).
    int numEntities() const { return numEntities_; }
    const LevelEntity& entity(int index) const;
    // Spec entities of one room (binary search); synthetic ones via findSynthetic. The
    // overloads without a room look in room 0 (single-level specs).
    const LevelEntity* find(int room, uint32_t entId) const;
    const LevelEntity *findReference(int room, uint32_t entId) const;
    uint32_t revision() const {
        return revision_;
    }
    const LevelEntity* find(uint32_t entId) const { return find(0, entId); }
    const LevelEntity* findSynthetic(int room, const char* type, uint32_t id) const;
    const LevelEntity* findSynthetic(const char* type, uint32_t id) const { return findSynthetic(0, type, id); }
    // Iterate entities of one type over all rooms: start with index -1; returns -1 when done.
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
    struct Room {
        uint8_t* data = nullptr;
        LevelEntity* entities = nullptr;
        const Attr* attrs = nullptr;
        float* floats = nullptr;
        const char* strings = nullptr;
        int first = 0;       // index of its first entity in the whole spec
        int numEntities = 0;
        int numSpec = 0;     // spec entities come first, sorted by entId
        uint32_t numAttrs = 0, numFloats = 0, stringBytes = 0;
    };
    const Attr* attr(const LevelEntity& e, const char* name) const;
    void place(Room& r, const float placement[12]);

    struct FindEntry {
        uint32_t entity = 0;
        int room = -1;
        const LevelEntity *result = nullptr;
    };
    static constexpr int kFindEntries = 256;
    mutable FindEntry findCache_[kFindEntries];
    uint32_t revision_ = 0;
    Room rooms_[kMaxRooms];
    int numRooms_ = 0;
    int numEntities_ = 0;
};

} // namespace tt::world
