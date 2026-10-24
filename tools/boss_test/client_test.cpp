// Exercise the real Client send guards with a recording transport. Private access
// is test-only so no production injection/debug API can alter boss membership.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#define private public
#include "net/client.h"
#undef private
#include "net/connection.h"
#include "net/msg_types.h"

namespace dc = tt::dc;
using namespace tt::net;

class RecordingTransport final : public Transport {
public:
    void connect(const char*, uint16_t) override { connected = true; }
    void disconnect() override { connected = false; }
    Status status() const override { return connected ? Status::Connected : Status::Disconnected; }
    uint32_t session() const override { return 1; }
    const char* lastError() const override { return ""; }
    void setHeartbeat(bool) override {}
    int recvMessage(uint8_t*, uint32_t) override { return 0; }
    bool sendMessage(const uint8_t* body, uint32_t length) override {
        assert(length <= sizeof(bytes));
        std::memcpy(bytes, body, length);
        size = length;
        ++messages;
        return true;
    }
    DatagramReader update(uint32_t object, uint16_t field) const {
        DatagramReader reader(bytes, size);
        assert(reader.u16() == msg::CLIENT_OBJECT_SET_FIELD);
        assert(reader.u32() == object);
        assert(reader.u16() == field);
        return reader;
    }
    bool connected = false;
    uint8_t bytes[256] = {};
    uint32_t size = 0;
    unsigned messages = 0;
};

#include "cfo_client.inc"

int main() {
    RecordingTransport transport;
    Client client;
    ClientConfig config;
    assert(client.start(transport, config));
    client.state_ = State::InZone;
    client.avatarId_ = 42;
    client.now_ = 1000;
    assert(!client.sendVpHit());
    client.boss_.active = true;
    client.boss_.doId = 100;
    client.boss_.classId = dc::DistributedSellbotBoss::kClassId;
    client.boss_.kind = BossKind::Sellbot;
    client.boss_.numToons = 2;
    client.boss_.toons[0] = 42;
    client.boss_.toons[1] = 43;
    std::strcpy(client.boss_.phase, "BattleThree");
    assert(!client.sendVpHit()); // a body pie cannot deal damage while raised
    client.boss_.attack = kBossDizzyNow;
    assert(client.sendVpHit());
    DatagramReader hit = transport.update(100, dc::DistributedSellbotBoss::k_hitBoss);
    assert(hit.u8() == 1 && hit.remaining() == 0);
    assert(client.sendVpHitInsides());
    assert(transport.update(100, dc::DistributedSellbotBoss::k_hitBossInsides).remaining() == 0);
    const unsigned before = transport.messages;
    assert(!client.sendVpFinalPie());
    assert(!client.sendVpHitToon(42));
    assert(!client.sendVpHitToon(99));
    assert(!client.sendBossBarrier(7));
    assert(transport.messages == before);
    assert(client.sendVpHitToon(43));
    DatagramReader heal = transport.update(100, dc::DistributedSellbotBoss::k_hitToon);
    assert(heal.u32() == 43 && heal.remaining() == 0);
    client.bossPies_ = 65535;
    assert(client.spendBossPie() && client.bossPies() == 65535);
    client.bossPies_ = 1;
    assert(client.spendBossPie() && client.bossPies() == 0);
    assert(!client.spendBossPie());
    assert(!client.sendBossZap(NAN, 0, 0, 0, 0, 0, kBossAreaAttack));
    assert(!client.sendBossZap(0, 0, 0, 0, 0, 0, kBossDizzyNow));
    assert(client.sendBossZap(1, 2, 3, 90, .6f, .8f, kBossAreaAttack));
    assert(transport.update(100, dc::DistributedBossCog::k_zapToon).remaining() == 17);
    client.boss_.numBarriers = 1;
    client.boss_.barriers[0].context = 65535;
    client.boss_.barriers[0].waitingForUs = true;
    assert(client.sendBossBarrier(65535));
    DatagramReader barrier = transport.update(100, dc::DistributedObject::k_setBarrierReady);
    assert(barrier.u16() == 65535 && barrier.remaining() == 0);
    Battle& round = client.battles_[0];
    round = {};
    round.active = true;
    round.doId = 200;
    round.bossId = 100;
    round.bossRound = 1;
    round.numToons = 1;
    round.toons[0] = 42;
    round.numBossBarriers = 1;
    round.bossBarriers[0].context = 7;
    round.bossBarriers[0].waitingForUs = true;
    std::strcpy(round.bossBarriers[0].name, "ReservesJoining");
    assert(client.sendBossBattleBarrier(200, 7));
    assert(!client.sendBossBattleBarrier(200, 8));
    assert(transport.update(200, dc::DistributedObject::k_setBarrierReady).u16() == 7);
    client.positionBossBattle(round);
    assert(round.x == -25 && round.y == -35 && round.z == 0 && round.sx == -24);
    round.bossRound = 2;
    round.bossSide = 1;
    client.positionBossBattle(round);
    assert(round.x == 25 && round.y == 60 && round.z == 18 && round.sx == 24);
    std::strcpy(client.boss_.phase, "NearVictory");
    assert(client.sendVpFinalPie());
    assert(!client.sendVpHit());
    assert(!client.sendVpHitInsides());
    assert(!client.sendBossZap(0, 0, 0, 0, 0, 0, kBossAreaAttack));
    testCfo(client, transport);
    client.cfg_.zoneId = 61001;
    client.forceReconnect("boss test disconnect");
    assert(client.zoneId() == 11100 && !client.boss());
    assert(!client.sendVpHit());
    client.state_ = State::InZone;
    client.boss_.active = true;
    client.boss_.kind = BossKind::Cashbot;
    client.cfg_.zoneId = 61002;
    client.forceReconnect("CFO test disconnect");
    assert(client.zoneId() == 12100 && !client.boss() && !client.findCfoChild(300));
    client.stop();
    std::puts("real boss Client: clsend payloads, membership/phase guards, pie stock, reserve barrier, round poses, reconnect: PASS");
}
