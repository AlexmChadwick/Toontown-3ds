#include "world/floor_format.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace tt::world::floorfmt;

namespace {
std::vector<uint8_t> fixture() {
    Header h{};
    std::memcpy(h.magic, "TLF1", 4);
    h.version = 1; h.kind = 1; h.facilityId = 12600; h.layoutIndex = 3; h.floor = 1;
    h.numPieces = 3; h.numDoorFrames = 1; h.numFloorTris = 1;
    h.piecesOffset = sizeof(Header);
    h.doorFramesOffset = h.piecesOffset + h.numPieces * sizeof(Piece);
    h.floorTrisOffset = h.doorFramesOffset + sizeof(Door);
    h.collisionOffset = h.floorTrisOffset + sizeof(FloorTri);
    h.collisionSize = 8; h.fileSize = h.collisionOffset + h.collisionSize;
    std::vector<uint8_t> out(h.fileSize);
    std::memcpy(out.data(), &h, sizeof(h));
    for (uint16_t i = 0; i < h.numPieces; ++i) {
        Piece p{};
        std::snprintf(p.name, sizeof(p.name), "piece_%u", unsigned(i));
        p.roomNum = i; p.isHallway = i & 1; p.roomId = p.isHallway ? -1 : int16_t(100 + i);
        std::memcpy(out.data() + h.piecesOffset + i * sizeof(Piece), &p, sizeof(p));
    }
    Door d{}; d.roomNum = 1; d.style = 1;
    std::memcpy(out.data() + h.doorFramesOffset, &d, sizeof(d));
    FloorTri t{}; t.roomNum = 2;
    std::memcpy(out.data() + h.floorTrisOffset, &t, sizeof(t));
    std::memcpy(out.data() + h.collisionOffset, "TTC1test", 8);
    return out;
}

template <typename F> void rejects(const std::vector<uint8_t>& good, F mutate) {
    auto bad = good;
    mutate(bad, *reinterpret_cast<Header*>(bad.data()));
    assert(validate(bad.data(), bad.size(), {1, 12600, 3, 1, 2}));
}
} // namespace

int main() {
    const auto good = fixture();
    const Expected expected{1, 12600, 3, 1, 2};
    assert(!validate(good.data(), good.size(), expected));
    assert(validate(nullptr, 0, expected));
    assert(validate(good.data(), sizeof(Header) - 1, expected));
    for (int field = 0; field < 4; ++field) {
        Expected wrong = expected;
        if (field == 0) ++wrong.kind;
        if (field == 1) ++wrong.facilityId;
        if (field == 2) ++wrong.layoutIndex;
        if (field == 3) ++wrong.floor;
        assert(validate(good.data(), good.size(), wrong));
    }
    rejects(good, [](auto&, Header& h) { h.fileSize--; });
    rejects(good, [](auto&, Header& h) { h.numPieces = 4; });
    rejects(good, [](auto&, Header& h) { h.numDoorFrames = 33; });
    rejects(good, [](auto&, Header& h) { h.numFloorTris = 0xffffffffu; });
    rejects(good, [](auto&, Header& h) { h.piecesOffset = 0xfffffff0u; });
    rejects(good, [](auto&, Header& h) { h.doorFramesOffset = h.piecesOffset; });
    rejects(good, [](auto&, Header& h) { h.floorTrisOffset = h.doorFramesOffset; });
    rejects(good, [](auto&, Header& h) { h.collisionOffset = h.floorTrisOffset; });
    rejects(good, [](auto&, Header& h) { h.collisionSize = 0; });
    rejects(good, [](auto& b, Header& h) {
        auto* p = reinterpret_cast<Piece*>(b.data() + h.piecesOffset); p[0].roomNum = 2;
    });
    rejects(good, [](auto& b, Header& h) {
        auto* p = reinterpret_cast<Piece*>(b.data() + h.piecesOffset); p[1].isHallway = 0;
    });
    rejects(good, [](auto& b, Header& h) {
        auto* p = reinterpret_cast<Piece*>(b.data() + h.piecesOffset); std::memset(p[2].name, 'x', sizeof(p[2].name));
    });
    rejects(good, [](auto& b, Header& h) {
        auto* d = reinterpret_cast<Door*>(b.data() + h.doorFramesOffset); d->roomNum = 3;
    });
    rejects(good, [](auto& b, Header& h) {
        auto* t = reinterpret_cast<FloorTri*>(b.data() + h.floorTrisOffset); t->roomNum = 3;
    });
    std::puts("TLF allocation-free identity/section/count/record validation PASS");
}
