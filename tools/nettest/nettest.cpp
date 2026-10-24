// Host-side test harness for source/net (builds with clang++ on macOS/Linux,
// no libctru). See tools/nettest/run_tests.sh.
//
//   nettest unit <dc_vectors.bin>         datagram + descriptor-table tests
//   nettest client [options]              run the real Client/Connection
//       --host H --port P --token T --seconds S
//       --expect-zone        fail unless we reached InZone and saw a moving toon
//       --expect-reconnect   fail unless we got back InZone after a drop
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/log.h"
#include "net/client.h"
#include "net/connection.h"
#include "net/datagram.h"
#include "net/net_platform.h"

using namespace tt::net;

static int gFailures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            ++gFailures;                                                         \
        }                                                                        \
    } while (0)

// ---------------------------------------------------------------- unit tests
namespace {

struct CountingVisitor : FieldVisitor {
    int ints = 0, uints = 0, floats = 0, bytes = 0, arrays = 0;
    void onInt(int64_t) override { ++ints; }
    void onUint(uint64_t) override { ++uints; }
    void onFloat(double) override { ++floats; }
    void onBytes(uint8_t, Bytes) override { ++bytes; }
    void onArrayBegin() override { ++arrays; }
};

void testDatagram() {
    uint8_t buf[64];
    DatagramWriter w(buf, sizeof(buf));
    w.u8(0x12);
    w.u16(0x3456);
    w.u32(0x789abcde);
    w.i16(-2);
    w.u64(0x0102030405060708ull);
    w.string("hi");
    w.f64(1.5);
    CHECK(w.ok());
    CHECK(w.size() == 1 + 2 + 4 + 2 + 8 + 4 + 8);
    CHECK(buf[1] == 0x56 && buf[2] == 0x34); // little endian

    DatagramReader r(buf, w.size());
    CHECK(r.u8() == 0x12);
    CHECK(r.u16() == 0x3456);
    CHECK(r.u32() == 0x789abcde);
    CHECK(r.i16() == -2);
    CHECK(r.u64() == 0x0102030405060708ull);
    char s[8];
    r.string(s, sizeof(s));
    CHECK(strcmp(s, "hi") == 0);
    CHECK(r.f64() == 1.5);
    CHECK(r.ok() && r.remaining() == 0);
    CHECK(r.u8() == 0 && !r.ok()); // underflow is sticky
    CHECK(r.u32() == 0 && !r.ok());

    // Writer overflow is sticky too.
    DatagramWriter small(buf, 3);
    small.u16(1);
    small.u16(2);
    CHECK(!small.ok());
    CHECK(small.size() == 2);

    // A truncated string length must not read past the end.
    uint8_t bad[] = {10, 0, 'a', 'b'};
    DatagramReader rb(bad, sizeof(bad));
    Bytes b = rb.blob();
    CHECK(!rb.ok() && b.data == nullptr);
}

void testTables() {
    // Required lists come straight from Panda; sanity check the ones we rely on.
    const tt::dc::ClassDesc* toon = findClass(tt::dc::DistributedToon::kClassId);
    CHECK(toon != nullptr);
    if (toon) {
        CHECK(toon->otherCount > 0 && toon->ownerCount >= toon->otherCount);
        CHECK(requiredOther(*toon)[0] == tt::dc::DistributedToon::k_setName);
        bool hasDna = false;
        for (int i = 0; i < toon->otherCount; ++i) hasDna |= requiredOther(*toon)[i] == tt::dc::DistributedToon::k_setDNAString;
        CHECK(hasDna);
        CHECK(classHasField(*toon, tt::dc::DistributedSmoothNode::k_setSmPosHpr));
        CHECK(!classHasField(*toon, tt::dc::AstronLoginManager::k_requestLogin));
        printf("DistributedToon: %d fields, %d required (other view), %d required (owner view)\n",
               toon->memberCount, toon->otherCount, toon->ownerCount);
    }
    for (int i = 1; i < tt::dc::kNumFields; ++i) CHECK(tt::dc::kFields[i - 1].id < tt::dc::kFields[i].id);
    CHECK(findField(65535) == nullptr);
    uint8_t junk[4] = {};
    DatagramReader r(junk, 4);
    CHECK(!skipField(r, 65535) && !r.ok());
}

int testVectors(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("cannot open %s\n", path);
        return 1;
    }
    std::vector<uint8_t> data;
    uint8_t chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) data.insert(data.end(), chunk, chunk + n);
    fclose(f);

    DatagramReader all(data.data(), uint32_t(data.size()));
    int count = 0, bad = 0;
    while (all.ok() && all.remaining() > 0) {
        uint16_t id = all.u16();
        uint16_t len = all.u16();
        Bytes v = all.fixed(len);
        if (!all.ok()) break;
        // skip, with exact length, and readField agrees
        DatagramReader r(v.data, v.len);
        bool ok = skipField(r, id) && r.ok() && r.remaining() == 0;
        DatagramReader r2(v.data, v.len);
        CountingVisitor cv;
        ok = ok && readField(r2, id, cv) && r2.remaining() == 0;
        // one byte short must fail cleanly (when non-empty)
        if (len > 0) {
            DatagramReader r3(v.data, uint32_t(len - 1));
            skipField(r3, id);
            ok = ok && !r3.ok();
        }
        if (!ok) {
            printf("vector FAIL field %u (%s) len %u\n", id, fieldName(id), len);
            ++bad;
        }
        ++count;
    }
    printf("descriptor vectors: %d checked, %d failed\n", count, bad);
    gFailures += bad;
    CHECK(count > 300);
    return 0;
}

void testClock() {
    Client c; // unsynced: delta 0
    uint32_t now = 123456;
    int16_t ts = c.networkTime(now);
    CHECK(ts == int16_t(12346 & 0xffff));
    CHECK(c.networkToLocal(ts, now) == now);
    // 1.5 s in the past, across a 16-bit wrap
    uint32_t later = 700000;
    int16_t past = c.networkTime(later - 1500);
    uint32_t local = c.networkToLocal(past, later);
    CHECK(local + 1500 >= later - 10 && local + 1500 <= later + 10);
}

// ---------------------------------------------------------------- client run
struct Options {
    const char* host = "127.0.0.1";
    int port = 7298;
    const char* token = "nettest";
    int seconds = 15;
    bool expectZone = false;
    bool expectReconnect = false;
};

int runClient(const Options& o) {
    Connection conn;
    if (!conn.init()) {
        printf("connection init failed: %s\n", conn.lastError());
        return 1;
    }
    Client client;
    ClientConfig cfg;
    cfg.host = o.host;
    cfg.port = uint16_t(o.port);
    cfg.playToken = o.token;
    client.start(conn, cfg);

    uint32_t start = plat::nowMs();
    uint32_t lastSend = 0;
    int spawns = 0, despawns = 0, poses = 0, anims = 0, chats = 0, zoneEntries = 0, leftZone = 0;
    bool sentAnim = false;
    float angle = 0.0f;
    uint32_t maxUpdateUs = 0;
    while (plat::nowMs() - start < uint32_t(o.seconds) * 1000u) {
        uint32_t now = plat::nowMs();
        uint32_t t0 = now;
        client.update(now);
        uint32_t dt = plat::nowMs() - t0;
        if (dt * 1000 > maxUpdateUs) maxUpdateUs = dt * 1000;

        Event ev;
        while (client.pollEvent(ev)) {
            const RemoteToon* t = client.findToon(ev.doId);
            switch (ev.type) {
            case EventType::Spawn:
                ++spawns;
                if (t) {
                    TT_LOG("test: SPAWN %u \"%s\" dna[0..3]=%c,%u,%u,%u pos (%.1f, %.1f, %.1f) h %.1f anim %s x%.2f parent %u",
                           unsigned(t->doId), t->name, t->dna[0], t->dna[1], t->dna[2], t->dna[3], t->x, t->y,
                           t->z, t->h, t->animState, t->animRate, unsigned(t->parentToken));
                }
                break;
            case EventType::Despawn: ++despawns; TT_LOG("test: DESPAWN %u", unsigned(ev.doId)); break;
            case EventType::Pose:
                if (++poses % 25 == 1 && t) {
                    TT_LOG("test: pose #%d %u (%.1f, %.1f, %.1f) h %.1f sample age %d ms", poses,
                           unsigned(t->doId), t->x, t->y, t->z, t->h, int(now - t->sampleMs));
                }
                break;
            case EventType::Anim:
                ++anims;
                if (t) TT_LOG("test: anim %u %s x%.2f", unsigned(t->doId), t->animState, t->animRate);
                break;
            case EventType::Chat:
                ++chats;
                if (t) TT_LOG("test: chat %u \"%s\": %s", unsigned(t->doId), t->name, t->chat);
                break;
            case EventType::EnteredZone:
                ++zoneEntries;
                TT_LOG("test: ENTERED ZONE as %u \"%s\" in \"%s\", %d toons, time synced %d",
                       unsigned(client.avatarId()), client.avatarName(), client.districtName(),
                       client.numRemoteToons(), int(client.timeSynced()));
                sentAnim = false;
                break;
            case EventType::LeftZone: ++leftZone; TT_LOG("test: LEFT ZONE"); break;
            default: break;
            }
        }

        // Our toon walks a small circle at 5 Hz.
        if (client.inZone()) {
            if (!sentAnim) sentAnim = client.sendAnimState("run", 1.0f);
            if (now - lastSend >= 200) {
                lastSend = now;
                angle += 0.1f;
                client.sendPosHpr(5.0f * std::cos(angle), 5.0f * std::sin(angle), 4.0f,
                                  angle * 57.29578f + 90.0f, 0.0f, 0.0f);
            }
        }
        plat::sleepMs(33); // ~30 Hz like the game loop
    }
    if (client.inZone()) {
        client.sendStop();
        client.sendAnimState("neutral", 1.0f);
    }
    client.update(plat::nowMs());
    plat::sleepMs(100);
    TT_LOG("test: summary state=%s zoneEntries=%d leftZone=%d spawns=%d despawns=%d poses=%d anims=%d chats=%d "
           "toons=%d msgsIn=%u bytesIn=%u msgsOut=%u bytesOut=%u droppedEvents=%u maxUpdate=%u us err=\"%s\"",
           stateName(client.state()), zoneEntries, leftZone, spawns, despawns, poses, anims, chats,
           client.numRemoteToons(), client.messagesIn(), client.bytesIn(), client.messagesOut(), client.bytesOut(),
           client.droppedEvents(), maxUpdateUs, client.lastError());
    client.stop();
    plat::sleepMs(100);
    conn.shutdown();

    int rc = 0;
    if (o.expectZone && (zoneEntries < 1 || spawns < 1 || poses < 10)) {
        printf("FAIL: expected to enter the zone and see a moving toon\n");
        rc = 1;
    }
    if (o.expectReconnect && (zoneEntries < 2 || leftZone < 1)) {
        printf("FAIL: expected a drop and a successful re-entry\n");
        rc = 1;
    }
    return rc;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && strcmp(argv[1], "unit") == 0) {
        testDatagram();
        testTables();
        testClock();
        testVectors(argv[2]);
        printf("unit tests: %s (%d failures)\n", gFailures ? "FAIL" : "PASS", gFailures);
        return gFailures ? 1 : 0;
    }
    if (argc >= 2 && strcmp(argv[1], "client") == 0) {
        Options o;
        for (int i = 2; i < argc; ++i) {
            if (!strcmp(argv[i], "--host") && i + 1 < argc) o.host = argv[++i];
            else if (!strcmp(argv[i], "--port") && i + 1 < argc) o.port = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--token") && i + 1 < argc) o.token = argv[++i];
            else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) o.seconds = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--expect-zone")) o.expectZone = true;
            else if (!strcmp(argv[i], "--expect-reconnect")) o.expectReconnect = true;
        }
        int rc = runClient(o);
        printf("client test: %s\n", rc ? "FAIL" : "PASS");
        return rc;
    }
    printf("usage: nettest unit <dc_vectors.bin> | nettest client [--host H] [--port P] [--token T] "
           "[--seconds S] [--expect-zone] [--expect-reconnect]\n");
    return 2;
}
