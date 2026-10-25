// Included by client_test.cpp: exercise the real Client with CEO protocol.
static void testCeo(Client& client, RecordingTransport& transport) {
    client.boss_.kind = BossKind::Bossbot;
    client.boss_.classId = dc::DistributedBossbotBoss::kClassId;
    std::strcpy(client.boss_.phase, "BattleTwo");

    // --- Food belt & serving ---
    CeoChild& belt = client.ceoChildren_[0];
    belt.active = true;
    belt.doId = 800;
    belt.bossId = 100;
    belt.classId = dc::DistributedFoodBelt::kClassId;
    belt.kind = CeoKind::Belt;
    belt.index = 0;
    belt.state = 'N';

    CeoChild& table = client.ceoChildren_[1];
    table.active = true;
    table.doId = 801;
    table.bossId = 100;
    table.classId = dc::DistributedBanquetTable::kClassId;
    table.kind = CeoKind::Table;
    table.index = 0;
    table.numDiners = 8;
    table.state = 'N';
    table.diners[2] = 1; // Hungry diner

    // Cannot serve when not holding food:
    assert(!client.sendCeoServe(801, 2));

    // Request food from belt:
    assert(client.sendCeoFood(0, 1, 10, false));
    DatagramReader foodMsg = transport.update(100, dc::DistributedBossbotBoss::k_requestGetFood);
    assert(foodMsg.i8() == 0 && foodMsg.i8() == 1 && foodMsg.u32() == 10 && !foodMsg.remaining());

    // Invalid belt/slot requests rejected:
    assert(!client.sendCeoFood(2, 0, 1, false));
    assert(!client.sendCeoFood(0, 4, 1, false));

    // Receive toonGotFood confirmation:
    uint8_t foodBytes[32];
    DatagramWriter foodWriter(foodBytes, sizeof(foodBytes));
    foodWriter.u32(42); // ownAvatarId
    foodWriter.i8(0);
    foodWriter.i8(1);
    foodWriter.u32(10);
    DatagramReader foodReader(foodBytes, foodWriter.size());
    assert(applyBossField(client.boss_, dc::DistributedBossbotBoss::k_toonGotFood, foodReader, 1000, 42));
    assert(client.boss_.heldFood && client.boss_.foodBelt == 0 && client.boss_.foodSlot == 1);

    // Cannot take more food while holding food:
    assert(!client.sendCeoFood(0, 2, 11, false));

    // Serve the hungry diner at seat 2:
    assert(client.sendCeoServe(801, 2));
    DatagramReader serveMsg = transport.update(100, dc::DistributedBossbotBoss::k_requestServeFood);
    assert(serveMsg.i8() == 0 && serveMsg.i8() == 2 && !serveMsg.remaining());

    // Serving non-hungry seat or invalid seat fails:
    assert(!client.sendCeoServe(801, 0));
    assert(!client.sendCeoServe(801, 9));

    // Receive toonServeFood confirmation:
    DatagramWriter serveWriter(foodBytes, sizeof(foodBytes));
    serveWriter.u32(42);
    serveWriter.i8(0);
    serveWriter.i8(2);
    DatagramReader serveReader(foodBytes, serveWriter.size());
    assert(applyBossField(client.boss_, dc::DistributedBossbotBoss::k_toonServeFood, serveReader, 1000, 42));
    assert(!client.boss_.heldFood);

    // --- Phase BattleFour: water, golf, attacks ---
    std::strcpy(client.boss_.phase, "BattleFour");

    // In BattleFour, toonup food belt is available:
    belt.state = 'T';
    assert(client.sendCeoFood(0, 2, 20, true));
    DatagramReader toonupMsg = transport.update(100, dc::DistributedBossbotBoss::k_requestGetToonup);
    assert(toonupMsg.i8() == 0 && toonupMsg.i8() == 2 && toonupMsg.u32() == 20 && !toonupMsg.remaining());

    // Banquet table control:
    table.state = 'R'; // Free
    table.owner = 0;
    assert(client.sendCeoControl(801, true));
    assert(transport.update(801, dc::DistributedBanquetTable::k_requestControl).remaining() == 0);

    // Another toon owns it:
    table.state = 'C';
    table.owner = 43;
    assert(!client.sendCeoControl(801, false));

    // We own the table:
    table.owner = 42;
    assert(client.sendCeoClear(801));
    assert(transport.update(801, dc::DistributedBanquetTable::k_clearSmoothing).i8() == 0);

    assert(client.sendCeoAim(801, 1, 180.0f));
    DatagramReader aim = transport.update(801, dc::DistributedBanquetTable::k_setPitcherPos);
    assert(aim.u8() == 1 && aim.u16() == 18000 && aim.remaining() == 2);

    // Aim with invalid heading fails:
    assert(!client.sendCeoAim(801, 2, NAN));
    assert(!client.sendCeoAim(801, 2, 400.0f));

    // Water firing:
    float endpoints[6] = {0.0f, 10.0f, 2.0f, 0.0f, 50.0f, 2.0f};
    assert(client.sendCeoWater(801, endpoints));
    DatagramReader water = transport.update(801, dc::DistributedBanquetTable::k_firingWater);
    assert(water.i32() == 0 && water.i32() == 1000 && water.i32() == 200 &&
           water.i32() == 0 && water.i32() == 5000 && water.i32() == 200 && !water.remaining());

    // Rate-limited water firing (< 500 ms) fails:
    assert(!client.sendCeoWater(801, endpoints));

    // Water hit boss:
    assert(client.sendCeoWaterHit(801, 3));
    DatagramReader bossHit = transport.update(100, dc::DistributedBossbotBoss::k_hitBoss);
    assert(bossHit.u8() == 3 && !bossHit.remaining());

    // Subsequent hit without new firing is rejected (impactReported):
    assert(!client.sendCeoWaterHit(801, 3));

    // Release table with gotHitByBoss=true:
    assert(client.sendCeoControl(801, false, true));
    DatagramReader freeTable = transport.update(801, dc::DistributedBanquetTable::k_requestFree);
    assert(freeTable.i8() == 1 && !freeTable.remaining());
    table.state = 'R';
    table.owner = 0;

    // --- Golf spot control ---
    CeoChild& golf = client.ceoChildren_[2];
    golf.active = true;
    golf.doId = 802;
    golf.bossId = 100;
    golf.classId = dc::DistributedGolfSpot::kClassId;
    golf.kind = CeoKind::Golf;
    golf.index = 1;
    golf.state = 'F'; // Free

    assert(client.sendCeoControl(802, true));
    assert(transport.update(802, dc::DistributedGolfSpot::k_requestControl).remaining() == 0);

    golf.state = 'C';
    golf.owner = 42;
    assert(client.sendCeoClear(802));
    assert(transport.update(802, dc::DistributedGolfSpot::k_clearSmoothing).i8() == 0);

    assert(client.sendCeoAim(802, 5, 45.0f));
    DatagramReader golfAim = transport.update(802, dc::DistributedGolfSpot::k_setGolfSpotPos);
    assert(golfAim.u8() == 5 && golfAim.u16() == 4500 && golfAim.remaining() == 2);

    // Golf swing:
    assert(client.sendCeoGolf(802, 85, 45.0f, 6));
    DatagramReader swing = transport.update(802, dc::DistributedGolfSpot::k_setSwingInfo);
    assert(swing.u8() == 85 && swing.i16() == 450 && swing.u8() == 6 && !swing.remaining());

    // Ball hit boss (speed damage):
    assert(client.sendCeoBallHit(802, 1));
    DatagramReader ballHit = transport.update(100, dc::DistributedBossbotBoss::k_ballHitBoss);
    assert(ballHit.u8() == 1 && !ballHit.remaining());

    // Duplicate ball hit rejected:
    assert(!client.sendCeoBallHit(802, 1));

    // Release golf spot without hit:
    assert(client.sendCeoControl(802, false, false));
    DatagramReader freeGolf = transport.update(802, dc::DistributedGolfSpot::k_requestFree);
    assert(freeGolf.i8() == 0 && !freeGolf.remaining());
    golf.state = 'F';
    golf.owner = 0;

    // --- Table attacks (BossCogMoveAttack = 16) ---
    // Outside of attack 16 or targeting another table fails:
    client.boss_.attack = 4;
    client.boss_.attackToon = 0;
    assert(!client.sendCeoTable(0, Client::CeoTableAction::Reached));

    client.boss_.attack = 16;
    client.boss_.attackToon = 0;
    assert(client.sendCeoTable(0, Client::CeoTableAction::Hit));
    DatagramReader hitTable = transport.update(100, dc::DistributedBossbotBoss::k_hitTable);
    assert(hitTable.u8() == 0 && !hitTable.remaining());

    assert(client.sendCeoTable(0, Client::CeoTableAction::Reached));
    DatagramReader reachedTable = transport.update(100, dc::DistributedBossbotBoss::k_reachedTable);
    assert(reachedTable.u8() == 0 && !reachedTable.remaining());

    assert(client.sendCeoTable(0, Client::CeoTableAction::Away));
    DatagramReader awayTable = transport.update(100, dc::DistributedBossbotBoss::k_awayFromTable);
    assert(awayTable.u8() == 0 && !awayTable.remaining());

    // --- CEO zap attacks ---
    assert(client.sendCeoZap(10, 20, 0, 90, 16));
    assert(transport.update(100, dc::DistributedBossCog::k_zapToon).remaining() == 17);

    // Invalid attack code rejected:
    assert(!client.sendCeoZap(10, 20, 0, 90, 99));

    // --- Battle round positions ---
    Battle battle{};
    battle.bossId = 100;
    battle.bossRound = 1;
    battle.bossSide = 0;
    client.positionBossBattle(battle);
    assert(battle.x == 20 && battle.y == -45 && battle.z == 0);

    battle.bossSide = 1;
    client.positionBossBattle(battle);
    assert(battle.x == -20 && battle.y == -45 && battle.z == 0);

    battle.bossRound = 2;
    battle.bossSide = 0;
    client.positionBossBattle(battle);
    assert(battle.x == 20 && battle.y == 115 && battle.z == 0);

    // --- Pink slip rewards ---
    uint8_t pinkSlipBuf[2] = {5};
    DatagramReader slipReader(pinkSlipBuf, 1);
    assert(client.bossOwnField(slipReader, dc::DistributedToon::k_setPinkSlips));
    assert(client.bossPinkSlips() == 5);

    // Truncated pink slips rejected:
    DatagramReader badSlip(pinkSlipBuf, 0);
    assert(client.bossOwnField(badSlip, dc::DistributedToon::k_setPinkSlips));
    assert(!badSlip.ok() && client.bossPinkSlips() == 5);

    // --- Enters binary vector file ---
    FILE* vectors = std::fopen("sdmc-out/boss-test/ceo_enters.bin", "rb");
    assert(vectors);
    uint32_t objectId = 0;
    int count = 0;
    while (std::fread(&objectId, 4, 1, vectors) == 1) {
        uint16_t classId = 0;
        uint32_t size = 0;
        uint8_t payload[1024];
        assert(std::fread(&classId, 2, 1, vectors) == 1);
        assert(std::fread(&size, 4, 1, vectors) == 1 && size <= sizeof(payload));
        assert(std::fread(payload, 1, size, vectors) == size);
        const dc::ClassDesc* cls = findClass(classId);
        assert(cls);
        DatagramReader enter(payload, size);
        assert(client.handleBossEnter(objectId, classId, enter, requiredOther(*cls), cls->otherCount, true));
        assert(enter.ok() && !enter.remaining());
        assert(client.findCeoChild(objectId));
        const CeoChild before = *client.findCeoChild(objectId);
        for (uint32_t length = 0; length < size; ++length) {
            DatagramReader bad(payload, length);
            assert(client.handleBossEnter(objectId, classId, bad, requiredOther(*cls), cls->otherCount, true));
            assert(std::memcmp(client.findCeoChild(objectId), &before, sizeof(before)) == 0);
        }
        client.bossLeaving(objectId);
        assert(!client.findCeoChild(objectId));
        ++count;
    }
    std::fclose(vectors);
    assert(count == 3);

    client.boss_.kind = BossKind::Sellbot;
    std::puts("CEO real Client: food belt/serving, table pitcher/water, golf spot/swing, attacks, pink slips: PASS");
}
