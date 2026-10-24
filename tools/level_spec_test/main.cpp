// Host test: TLV1 reader against baked level specs (run after bake_level.py --tlv-only).
#include <cmath>
#include <cstdio>
#include <cstring>
#include "world/level_spec.h"
#include "world/py_random.h"

using tt::world::LevelSpec;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "sdmc-out/3ds/toontown3ds/data/level";
    char path[256];
    std::snprintf(path, sizeof(path), "%s/sellbot_factory.tlv", dir);
    LevelSpec spec;
    CHECK(spec.load(path));
    if (!spec.loaded()) return 1;
    // SellbotLegFactorySpec: 10014 is the Lobby 'cogs' nodepath at (0, 34.07, 0) under zone 4.
    const auto* cogs = spec.find(10014);
    CHECK(cogs && std::strcmp(cogs->type, "nodepath") == 0 && cogs->zoneNum == 4);
    CHECK(cogs && std::fabs(cogs->pos[1] - 34.07f) < 1e-3f);
    const auto* mgr = spec.find(1000);
    CHECK(mgr && std::strcmp(spec.string(*mgr, "modelFilename"), "phase_9/models/cogHQ/SelbotLegFactory") == 0);
    const auto* lobby = spec.find(4);
    float vis[16];
    CHECK(lobby && spec.getFloats(*lobby, "visibility", vis, 16) == 2 && vis[0] == 113 && vis[1] == 114);
    // SellbotLegFactoryCogs.CogData[0]: cc level 3, battle cell 0, parent 10014, pos (0,0,0).
    const auto* cog0 = spec.findSynthetic("cogSpec", 0);
    CHECK(cog0 && spec.integer(*cog0, "level") == 3 && spec.integer(*cog0, "battleCell") == 0);
    CHECK(cog0 && std::strcmp(spec.string(*cog0, "type"), "cc") == 0 && cog0->parentEntId == 10014);
    float o[3] = {0, 0, 0}, w[3], wc[3];
    if (cog0 && cogs) {
        LevelSpec::toWorld(*cog0, o, w);
        LevelSpec::toWorld(*cogs, o, wc);
        CHECK(std::fabs(w[0] - wc[0]) < 1e-3f && std::fabs(w[1] - wc[1]) < 1e-3f);
    }
    CHECK(spec.find(123456) == nullptr);
    int n = 0;
    for (int i = spec.next("door", -1); i >= 0; i = spec.next("door", i)) n++;
    CHECK(n == 27);
    n = 0;
    for (int i = spec.next("cogSpec", -1); i >= 0; i = spec.next("cogSpec", i)) n++;
    CHECK(n == 35);
    // PyRandom == CPython random.Random (values printed by python3 for the same seeds).
    tt::world::PyRandom r(12345);
    CHECK(r.getrandbits(32) == 1789368711u && r.getrandbits(32) == 3146859322u && r.getrandbits(32) == 43676229u);
    tt::world::PyRandom b(uint64_t(20021) * 401006382ull);
    CHECK(b.randrange(0, 2) == 1);
    tt::world::PyRandom c(uint64_t(20021) * 401006382ull);
    static const int kTracks[9] = {0, 1, 2, 3, 4, 4, 5, 5, 6};
    CHECK(kTracks[c.randbelow(9)] == 5);
    std::printf("%s: %d entities, cog0 world (%.2f %.2f %.2f)\n", path, spec.numEntities(), w[0], w[1], w[2]);

    // A facility floor: the same room twice, the second turned 90 degrees and moved
    // (bake_facility_floors.py placements); entities keep their room and land in floor space.
    char room[256];
    std::snprintf(room, sizeof(room), "%s/CashbotMintLobby_Battle00.tlv", dir);
    LevelSpec floor;
    const float turn[12] = {0, 1, 0, -1, 0, 0, 0, 0, 1, 100, 200, 5}; // H 90 then (100, 200, 5)
    CHECK(floor.append(room, nullptr) == 0 && floor.append(room, turn) == 1);
    CHECK(floor.numRooms() == 2 && floor.numEntities() > 0 && floor.numEntities() % 2 == 0);
    const int half = floor.numEntities() / 2;
    const auto& first = floor.entity(1);
    const auto& second = floor.entity(half + 1);
    CHECK(first.room == 0 && second.room == 1 && first.entId == second.entId);
    float pa[3], pb[3];
    LevelSpec::toWorld(first, o, pa);
    LevelSpec::toWorld(second, o, pb);
    CHECK(std::fabs(pb[0] - (100 - pa[1])) < 1e-3f && std::fabs(pb[1] - (200 + pa[0])) < 1e-3f &&
          std::fabs(pb[2] - (pa[2] + 5)) < 1e-3f);
    CHECK(floor.find(1, first.entId) == &second && floor.find(0, first.entId) == &first);
    const auto* c0 = floor.findSynthetic(0, "cogSpec", 0);
    const auto* c1 = floor.findSynthetic(1, "cogSpec", 0);
    float pw0[48], pw1[48];
    const int np = c0 ? floor.getFloats(*c0, "pathWorld", pw0, 48) : 0;
    CHECK(c0 && c1 && floor.getFloats(*c1, "pathWorld", pw1, 48) == np);
    for (int k = 0; k + 2 < np && k + 2 < 48; k += 3)
        CHECK(std::fabs(pw1[k] - (100 - pw0[k + 1])) < 1e-3f && std::fabs(pw1[k + 1] - (200 + pw0[k])) < 1e-3f);
    int lobbies = 0;
    for (int i = floor.next("levelMgr", -1); i >= 0; i = floor.next("levelMgr", i)) lobbies++;
    CHECK(lobbies == 2);
    std::printf(failures ? "level_spec_test: %d FAILED\n" : "level_spec_test: ok\n", failures);
    return failures ? 1 : 0;
}
