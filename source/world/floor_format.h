// Allocation-free validation for baked facility-floor containers (TLF1).
#pragma once

#include <cstdint>

namespace tt::world::floorfmt {

struct Header {
    char magic[4];
    uint16_t version, kind;
    uint32_t facilityId;
    uint16_t layoutIndex, floor, numPieces, numDoorFrames;
    uint32_t numFloorTris, piecesOffset, doorFramesOffset, floorTrisOffset, collisionOffset, collisionSize,
        fileSize;
};
static_assert(sizeof(Header) == 48, "must match bake_facility_floors.py HEADER");

struct Piece {
    char name[48];
    uint16_t roomNum;
    uint8_t isHallway, pad;
    int16_t roomId;
    uint16_t pad2;
    float mat[12], lo[3], hi[3];
};
static_assert(sizeof(Piece) == 128, "must match bake_facility_floors.py PIECE");

struct Door { uint16_t roomNum, style; float mat[12]; };
static_assert(sizeof(Door) == 52, "must match bake_facility_floors.py DOOR");

struct FloorTri { uint16_t roomNum, pad; float v[9]; };
static_assert(sizeof(FloorTri) == 40, "must match bake_facility_floors.py FLOOR_TRI");

struct Expected {
    uint16_t kind;
    uint32_t facilityId;
    uint16_t layoutIndex, floor, numRooms;
};

// Returns nullptr on success, otherwise a stable diagnostic. Checks every record
// before Level mutates live floor state and performs no allocations.
const char* validate(const void* data, uint32_t size, const Expected& expected);

} // namespace tt::world::floorfmt
