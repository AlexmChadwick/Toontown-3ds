#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "net/boss_state.h"
#include "world/boss_logic.h"
#include "world/cfo_logic.h"
#include "world/cj_logic.h"
#include "net/cj_state.h"

namespace dc = tt::dc;
using namespace tt::net;
using namespace tt::world;

static void parse(BossState& state, uint16_t field, const DatagramWriter& writer) {
    DatagramReader reader(writer.data(), writer.size());
    assert(applyBossField(state, field, reader, 1000, 42));
    assert(reader.remaining() == 0);
    for (uint32_t size = 0; size < writer.size(); ++size) {
        const BossState before = state;
        DatagramReader truncated(writer.data(), size);
        assert(!applyBossField(state, field, truncated, 2000, 42));
        assert(state.revision == before.revision);
        assert(!std::strcmp(state.phase, before.phase));
        assert(state.damage == before.damage);
    }
}

static void referenceVectors(const char* path) {
    FILE* file = std::fopen(path, "rb");
    assert(file);
    unsigned count = 0;
    uint16_t classId = 0;
    while (std::fread(&classId, 2, 1, file) == 1) {
        uint16_t field = 0;
        uint32_t size = 0;
        assert(std::fread(&field, 2, 1, file) == 1);
        assert(std::fread(&size, 4, 1, file) == 1);
        uint8_t buffer[16384];
        assert(size <= sizeof(buffer));
        assert(std::fread(buffer, 1, size, file) == size);
        BossState state;
        state.classId = classId;
        state.kind = bossKind(classId);
        DatagramReader reader(buffer, size);
        if (cjKind(classId) != CjKind::None) {
            CjChild child;
            child.classId = classId;
            child.kind = cjKind(classId);
            assert(applyCjField(child, field, reader, 1000));
            for (uint32_t length = 0; length < size; ++length) {
                CjChild truncatedChild = child;
                DatagramReader truncated(buffer, length);
                assert(!applyCjField(truncatedChild, field, truncated, 2000));
                assert(std::memcmp(&child, &truncatedChild, sizeof(child)) == 0);
            }
        } else if (cfoKind(classId) == CfoKind::None) {
            assert(applyBossField(state, field, reader, 1000, 42));
        } else {
            CfoChild child;
            child.classId = classId;
            child.kind = cfoKind(classId);
            assert(applyCfoField(child, field, reader, 1000));
            for (uint32_t length = 0; length < size; ++length) {
                CfoChild truncatedChild = child;
                DatagramReader truncated(buffer, length);
                assert(!applyCfoField(truncatedChild, field, truncated, 2000));
                assert(std::memcmp(&child, &truncatedChild, sizeof(child)) == 0);
            }
            if (field == dc::DistributedCashbotBossCrane::k_setCablePos) {
                assert(child.sequence == 255 && child.armY == 24.5f && child.links[2][2] == -20);
            }
        }
        assert(reader.remaining() == 0);
        ++count;
    }
    std::fclose(file);
    assert(count > 50);
    std::printf("%u Panda boss vectors: PASS\n", count);
}

int main(int argc, char** argv) {
    if (argc > 1) referenceVectors(argv[1]);
    assert(cjScaleTilt(0) == -40 && cjScaleTilt(1350) == 0 && cjScaleTilt(2700) == 40);
    assert(cjScaleTilt(9999) == 40);
    assert(std::fabs(cjChairPosition(0, 0).x - 29.3f) < .001f);
    assert(std::fabs(cjChairPosition(11, 70).z - 28.2f) < .001f);
    assert(cjBarrierDelay("BattleTwo") > 70 && cjBarrierDelay("BattleThree") < 0);
    const CjFlight flight = cjCannonFlight({-80, 10, 0}, -90, 0, 30, {0, 0, 1});
    const Vec3 position = cjFlightPosition(flight, 1);
    assert(position.x > -10 && position.x < 0 && std::fabs(position.y - 10) < .001f);
    assert(std::fabs(position.z - (flight.origin.z + 40 - 16)) < .001f);
    assert(std::fabs(cjGavelPose(0, .3f).pitch + 80) < .001f);
    CfoCable cable;
    cable.reset();
    for (int frame = 0; frame < 900; ++frame) cable.update(1, 1, 1.0f / 30, false);
    assert(cable.y == 25 && cable.heading == 45);
    Vec3 magnet = cable.magnet(0);
    assert(std::isfinite(magnet.x) && magnet.z > 0 && magnet.z < 40);
    assert(cfoImpact({0, 10, 0}, 0) == 1);
    assert(cfoImpact({0, -10, 0}, 0) == 0);
    assert(cfoImpact({0, 0, 0}, 0) == 0);
    CfoChild goon;
    goon.scale = 1;
    assert(cfoGoonStun(goon, {0, 0, 0}, {0, 0, 3.8f}));
    assert(!cfoGoonStun(goon, {0, 0, 0}, {0, 0, 0}));
    assert(cfoGoonDetect(goon, {0, 0, 0}, {0, 10, 0}));
    assert(!cfoGoonDetect(goon, {0, 0, 0}, {0, -10, 0}));
    static_assert(dc::kHash == 547155168, "stock DC hash");
    BossState state;
    state.kind = BossKind::Sellbot;
    state.classId = dc::DistributedSellbotBoss::kClassId;
    uint8_t buffer[1024];
    DatagramWriter writer(buffer, sizeof(buffer));
    writer.string("BattleThree");
    parse(state, dc::DistributedSellbotBoss::k_setState, writer);
    assert(state.phaseMs == 1000);
    DatagramReader repeated(buffer, writer.size());
    assert(applyBossField(state, dc::DistributedSellbotBoss::k_setState, repeated, 3000, 42));
    assert(state.phaseMs == 1000);
    writer.reset();
    writer.u16(37);
    writer.u8(60);
    writer.i16(-32760);
    parse(state, dc::DistributedSellbotBoss::k_setBossDamage, writer);
    assert(state.damageStamp == -32760);
    assert(std::fabs(currentBossDamage(state, 31000) - 7) < .001f);
    assert(currentBossDamage(state, 41000) == 0);
    writer.reset();
    writer.u8(12);
    writer.u32(42);
    parse(state, dc::DistributedBossCog::k_setAttackCode, writer);
    assert(state.attack == 12 && state.attackToon == 42);
    writer.reset();
    for (int side = 0; side < 3; ++side) {
        writer.u16(8);
        writer.u32(42);
        writer.u32(43);
    }
    parse(state, dc::DistributedBossCog::k_setToonIds, writer);
    assert(state.numToons == 2 && state.sideB[1] == 43);
    writer.reset();
    writer.u16(36); // nine members: reject before copying into an eight-slot array
    for (int i = 0; i < 9; ++i) writer.u32(i);
    DatagramReader excessive(buffer, writer.size());
    assert(!applyBossField(state, dc::DistributedBossCog::k_setToonIds, excessive, 1000, 42));
    assert(state.numToons == 2);
    uint8_t barrierBuffer[256];
    DatagramWriter barrier(barrierBuffer, sizeof(barrierBuffer));
    barrier.u16(65535);
    barrier.string("Introduction");
    barrier.u16(8);
    barrier.u32(42);
    barrier.u32(43);
    writer.reset();
    writer.blob(barrier.data(), barrier.size());
    parse(state, dc::DistributedObject::k_setBarrierData, writer);
    assert(state.numBarriers == 1 && state.barriers[0].waitingForUs);
    assert(state.barriers[0].context == 65535);
    assert(bossPhase("BattleOne") == BossPhase::Round);
    assert(bossPhase("BattleTwo") == BossPhase::Round);
    assert(bossPhase("BattleThree") == BossPhase::Pies);
    assert(bossPhase("NearVictory") == BossPhase::FinalPie);
    assert(bossPhase("Epilogue") == BossPhase::Epilogue);
    assert(bossPhase("bogus") == BossPhase::Off);
    assert(bossBarrierDelay("BattleThree") < 0);
    assert(bossBarrierDelay("Introduction") < 45);
    std::strcpy(state.phase, "BattleOne");
    state.battleA = 1;
    state.battleB = 2;
    assert(vpCageHeight(state, 1000) == 100);
    state.battleB = 0;
    assert(vpCageHeight(state, 1000) == 81);
    std::strcpy(state.phase, "BattleTwo");
    assert(vpCageHeight(state, 1000) == 44);
    std::strcpy(state.phase, "Epilogue");
    state.phaseMs = 1000;
    assert(vpCageHeight(state, 1250) == 21.5f);
    assert(vpCageHeight(state, 2000) == 18);
    assert(vpCageDoorAngle(state, 1500) == 0);
    assert(vpCageDoorAngle(state, 2000) == 90);
    const Vec3 top = vpPosition(0);
    const Vec3 bottom = vpPosition(100);
    assert(top.y == 60 && top.z == 18);
    assert(std::fabs(bottom.y + 175) < .001f);
    assert(vpPosition(40).y < vpPosition(30).y);
    assert(segmentBox({0, -100, 0}, {0, 100, 0}, {-1, -1, -1}, {1, 1, 1}));
    assert(!segmentBox({5, -100, 0}, {5, 100, 0}, {-1, -1, -1}, {1, 1, 1}));
    assert(vpPieHit({0, -30, 2}, {0, 30, 2}, {0, 0, 0}, true) == VpPieHit::Insides);
    assert(vpPieHit({0, -30, 2}, {0, 30, 2}, {0, 0, 0}, false) == VpPieHit::None);
    assert(vpPieHit({0, -30, 15}, {0, 30, 15}, {0, 0, 0}, true) == VpPieHit::Body);
    assert(std::fabs(piePosition({0, 0, 0}, 0, 0, 0, 1).y - 100) < .001f);
    assert(std::fabs(piePosition({0, 0, 0}, 0, 0, 0, 1).z) < .001f);
    std::puts("boss phase, damage recovery, trajectory, swept hits, bounded parsing and truncations: PASS");
}
