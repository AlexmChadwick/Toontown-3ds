#include "world/level_draw_data.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
using namespace tt::world;
static volatile float sink = 0;
int main() {
    LevelSpec spec;
    assert(spec.load("sdmc-out/factory-test/sellbot_factory.tlv"));
    LevelDrawData cached[128];
    uint32_t ids[128];
    int count = 0;
    for (int i = 0; i < spec.numEntities() && count < 128; ++i) {
        const auto &e = spec.entity(i);
        if (e.flags & kLevelSynthetic)
            continue;
        ids[count] = e.entId;
        cached[count++] = prepareLevelDraw(spec, e);
    }
    for (int room = -1; room < 3; ++room)
        for (uint32_t id = 0; id < 5000; ++id)
            assert(spec.find(room, id) == spec.findReference(room, id));
    auto run = [&](bool cache) {
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 10000; ++frame)
            for (int i = 0; i < count; ++i) {
                const auto *e = cache ? spec.find(0, ids[i]) : spec.findReference(0, ids[i]);
                assert(e);
                const auto draw = cache ? cached[i] : prepareLevelDraw(spec, *e);
                sink = draw.matrix[0] + draw.scale + draw.modelType;
            }
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() /
               10000;
    };
    const double before = run(false), after = run(true);
    for (int i = 0; i < count; ++i) {
        const auto data = prepareLevelDraw(spec, *spec.find(0, ids[i]));
        assert(!std::memcmp(data.matrix, cached[i].matrix, sizeof(data.matrix)));
        assert(data.scale == cached[i].scale && data.modelType == cached[i].modelType);
    }
    const auto revision = spec.revision();
    assert(spec.append("sdmc-out/factory-test/CashbotMintLobby_Battle00.tlv", nullptr) == 1);
    assert(spec.revision() != revision);
    for (int room = 0; room < 2; ++room)
        for (uint32_t id = 0; id < 5000; ++id)
            assert(spec.find(room, id) == spec.findReference(room, id));
    spec.unload();
    assert(!spec.find(0, ids[0]));
    assert(spec.load("sdmc-out/factory-test/sellbot_factory.tlv"));
    assert(spec.find(0, ids[0]) == spec.findReference(0, ids[0]));
    std::printf("Facility 128-entity draw preparation ns/frame: before %.1f after %.1f speedup %.2fx; exact "
                "lookup/collision/matrix/reload/append PASS; draw data %zu B/entity, LevelSpec %zu B\n",
                before, after, before / after, sizeof(LevelDrawData), sizeof(LevelSpec));
}
