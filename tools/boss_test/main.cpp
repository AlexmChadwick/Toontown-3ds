#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "net/boss_state.h"
#include "world/boss_logic.h"

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

int main() {
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
