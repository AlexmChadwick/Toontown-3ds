#include "world/floor_format.h"

#include <cstring>

namespace tt::world::floorfmt {
namespace {
constexpr uint16_t kMaxRooms = 16, kMaxPieces = 32, kMaxDoors = 32;
struct Range { uint32_t begin, end; };

bool makeRange(uint32_t offset, uint64_t bytes, uint32_t fileSize, Range& out) {
    const uint64_t end = uint64_t(offset) + bytes;
    if (offset < sizeof(Header) || end > fileSize) return false;
    out = {offset, static_cast<uint32_t>(end)};
    return true;
}
bool overlap(const Range& a, const Range& b) { return a.begin < b.end && b.begin < a.end; }
} // namespace

const char* validate(const void* data, uint32_t size, const Expected& e) {
    if (!data || size < sizeof(Header)) return "truncated header";
    Header h;
    std::memcpy(&h, data, sizeof(h));
    if (std::memcmp(h.magic, "TLF1", 4) || h.version != 1) return "bad magic/version";
    if (h.fileSize != size) return "bad file size";
    if (e.numRooms == 0 || e.numRooms > kMaxRooms || h.numPieces != uint16_t(2 * e.numRooms - 1) ||
        h.numPieces > kMaxPieces || h.numDoorFrames > kMaxDoors)
        return "bad counts";
    if (h.kind != e.kind || h.facilityId != e.facilityId || h.layoutIndex != e.layoutIndex || h.floor != e.floor)
        return "wrong floor identity";

    Range ranges[4];
    if (!makeRange(h.piecesOffset, uint64_t(h.numPieces) * sizeof(Piece), size, ranges[0]) ||
        !makeRange(h.doorFramesOffset, uint64_t(h.numDoorFrames) * sizeof(Door), size, ranges[1]) ||
        !makeRange(h.floorTrisOffset, uint64_t(h.numFloorTris) * sizeof(FloorTri), size, ranges[2]) ||
        !makeRange(h.collisionOffset, h.collisionSize, size, ranges[3]))
        return "section outside file";
    if (!h.collisionSize) return "empty collision";
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
            if (overlap(ranges[i], ranges[j])) return "overlapping sections";

    const auto* bytes = static_cast<const uint8_t*>(data);
    for (uint16_t i = 0; i < h.numPieces; ++i) {
        Piece p;
        std::memcpy(&p, bytes + h.piecesOffset + i * sizeof(Piece), sizeof(p));
        const bool hallway = (i & 1) != 0;
        if (p.roomNum != i || p.isHallway > 1 || bool(p.isHallway) != hallway ||
            (!hallway && p.roomId < 0) || (hallway && p.roomId != -1) || !std::memchr(p.name, '\0', sizeof(p.name)))
            return "bad piece record";
    }
    for (uint16_t i = 0; i < h.numDoorFrames; ++i) {
        Door d;
        std::memcpy(&d, bytes + h.doorFramesOffset + i * sizeof(Door), sizeof(d));
        if (d.roomNum >= h.numPieces || d.style > 1) return "bad door record";
    }
    for (uint32_t i = 0; i < h.numFloorTris; ++i) {
        FloorTri t;
        std::memcpy(&t, bytes + h.floorTrisOffset + i * sizeof(FloorTri), sizeof(t));
        if (t.roomNum >= h.numPieces) return "bad floor triangle record";
    }
    return nullptr;
}

} // namespace tt::world::floorfmt
