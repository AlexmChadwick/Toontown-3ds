#include "game/two_d_game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace tt::game {

bool TwoDGame::init(const char* root, unsigned hood) {
    hood_ = hood;
    timer_ = 0.0f;
    score_ = treasureCount_ = 0;
    completed_ = isDone_ = courseAssembled_ = false;
    const char* names[] = { "twod_block00", "twod_block01", "twod_block02", "twod_block03" };
    char path[256];
    for (int i = 0; i < 4; ++i) {
        std::snprintf(path, sizeof(path), "%s%s.ttm", root, names[i]);
        assetBatch_.load(blockModels_[i], path);
    }
    std::snprintf(path, sizeof(path), "%stwod_wall.ttm", root); assetBatch_.load(wallModel_, path);
    std::snprintf(path, sizeof(path), "%stwod_pipe.ttm", root); assetBatch_.load(pipeModel_, path);
    std::snprintf(path, sizeof(path), "%stwod_elevator.ttm", root); assetBatch_.load(elevatorModel_, path);
    std::snprintf(path, sizeof(path), "%stwod_stomper.ttm", root); assetBatch_.load(stomperModel_, path);
    std::snprintf(path, sizeof(path), "%stwod_arrow.ttm", root); assetBatch_.load(arrowModel_, path);
    std::snprintf(path, sizeof(path), "%stwod_treasure.ttm", root); assetBatch_.load(treasureModel_, path);
    assetBatch_.load(suitPlaceholder_, "romfs:/baked/suit_placeholder.ttm");
    TT_LOG("mgpass: twod start hood=%u waiting for setSectionsSelected", hood_);
    return true;
}

void TwoDGame::shutdown() {
    assetBatch_.clear();
    for (auto& model : blockModels_) model.unload();
    wallModel_.unload(); pipeModel_.unload(); elevatorModel_.unload(); stomperModel_.unload();
    arrowModel_.unload(); treasureModel_.unload(); suitPlaceholder_.unload();
    isDone_ = true;
}

void TwoDGame::handleSetSections(const net::Client& client) {
    const int count = client.minigameTwoDSectionCount();
    if (count <= 0 || count > twodgame::kMaxSections) return;
    twodgame::SectionSelection selections[twodgame::kMaxSections] = {};
    for (int i = 0; i < count; ++i) {
        const auto& src = client.minigameTwoDSection(i);
        auto& dst = selections[i];
        dst.sectionIndex = src.sectionIndex;
        dst.numEnemies = std::min<int>(src.numEnemies, twodgame::kMaxSectionEnemies);
        dst.numTreasures = std::min<int>(src.numTreasures, twodgame::kMaxSectionTreasures);
        dst.numSpawnPoints = std::min<int>(src.numSpawns, twodgame::kMaxSectionSpawnPoints);
        dst.numStompers = std::min<int>(src.numStompers, twodgame::kMaxSectionStompers);
        std::memcpy(dst.enemies, src.enemyIndices, dst.numEnemies);
        std::memcpy(dst.treasures, src.treasureIndices, dst.numTreasures);
        std::memcpy(dst.treasureValues, src.treasureValues, dst.numTreasures);
        std::memcpy(dst.spawnPoints, src.spawnIndices, dst.numSpawnPoints);
        std::memcpy(dst.stompers, src.stomperIndices, dst.numStompers);
    }
    const net::Minigame* mg = client.minigame();
    int localIndex = 0;
    if (mg) for (int i = 0; i < mg->numParticipants; ++i) if (mg->participants[i] == client.avatarId()) localIndex = i;
    if (!course_.build(selections, count, mg ? mg->numParticipants : 1)) {
        TT_LOG("mgpass: twod invalid setSectionsSelected count=%d", count);
        return;
    }
    sim_.init(&course_, localIndex, 3.0f);
    courseAssembled_ = true;
    TT_LOG("mgpass: twod sections=%d blocks=%d enemies=%d treasures=%d", count, course_.numBlocks, course_.numEnemies, course_.numTreasures);
}

void TwoDGame::handleTreasureGrabbed(uint32_t, uint8_t section, uint8_t treasure) {
    if (courseAssembled_) sim_.hideTreasure(course_.findTreasure(section, treasure));
}

void TwoDGame::handleEveryoneDone() {
    isDone_ = true;
    TT_LOG("mgpass: twod AI end score=%d", score_);
}

void TwoDGame::handleEnemyShot(uint8_t section, uint8_t enemy, uint32_t health) {
    if (!courseAssembled_ || health != 0) return;
    sim_.enemyKilled(course_.findEnemy(section, enemy), timer_);
}

void TwoDGame::handleVictoryScore(uint8_t score) {
    score_ += score;
    TT_LOG("mgpass: twod AI victory score=%u", score);
}

void TwoDGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    if (isDone_) return;
    if (!courseAssembled_) { handleSetSections(client); return; }
    timer_ += dt;
    twodgame::Input input;
    input.move = pad.dx > 20 ? 1 : pad.dx < -20 ? -1 : 0;
    input.jump = (keysDown & KEY_A) != 0;
    input.shoot = (keysDown & KEY_B) != 0;
    if (autoPlay_) input = twodgame::autopilot(sim_, timer_, dt);
    twodgame::Events events;
    sim_.step(input, timer_, dt, &events);
    const int16_t stamp = static_cast<int16_t>((nowMs / 10) & 0xffff);
    if (input.shoot && sim_.shooting(timer_)) client.sendTwoDShootGun(client.avatarId(), stamp);
    for (int i = 0; i < events.treasureGrabs; ++i) {
        client.sendTwoDClaimTreasure(events.treasureSection[i], events.treasureIndex[i]);
        ++treasureCount_;
        TT_LOG("mgpass: twod treasure section=%u index=%u", events.treasureSection[i], events.treasureIndex[i]);
    }
    for (int i = 0; i < events.enemyShots; ++i) {
        client.sendTwoDClaimEnemyShot(events.shotSection[i], events.shotIndex[i]);
        TT_LOG("mgpass: twod enemy shot section=%u index=%u", events.shotSection[i], events.shotIndex[i]);
    }
    if (events.fellDown) client.sendTwoDFellDown(client.avatarId(), stamp);
    if (events.hitByEnemy) client.sendTwoDHitByEnemy(client.avatarId(), stamp);
    if (events.squished) client.sendTwoDSquished(client.avatarId(), stamp);
    if (events.victory && !completed_) {
        completed_ = true;
        client.sendTwoDVictory(client.avatarId(), stamp);
        client.sendTwoDReportDone();
        TT_LOG("mgpass: twod victory t=%.1f", timer_);
    }
    if (timer_ >= twodgame::hoodRules(hood_).gameDuration && !completed_) {
        client.sendTwoDReportDone();
        isDone_ = true;
        TT_LOG("mgpass: twod timer end t=%.1f", timer_);
    }
}

void TwoDGame::draw(render::Renderer& renderer) {
    if (!courseAssembled_) return;
    C3D_Mtx model;
    for (int i = 0; i < course_.numBlocks; ++i) {
        float x, z;
        course_.blockOffset(i, timer_, &x, &z);
        const auto& type = twodgame::kBlockTypes[course_.blocks[i].def->type];
        if (type.model >= 4 || !blockModels_[type.model].loaded()) continue;
        Mtx_Identity(&model);
        Mtx_Translate(&model, x, 0.0f, z, false);
        Mtx_RotateY(&model, type.hpr[0] * 0.0174532925f, false);
        renderer.drawModel(blockModels_[type.model], model);
    }
    if (treasureModel_.loaded()) for (int i = 0; i < course_.numTreasures; ++i) {
        if (!sim_.treasureVisible(i, timer_)) continue;
        Mtx_Identity(&model);
        Mtx_Translate(&model, sim_.treasureX(i), 0.0f, sim_.treasureZ(i), false);
        renderer.drawModel(treasureModel_, model);
    }
    if (suitPlaceholder_.loaded()) for (int i = 0; i < course_.numEnemies; ++i) {
        if (!sim_.enemyAlive(i)) continue;
        float x, z, h;
        course_.enemyPose(i, timer_, &x, &z, &h);
        Mtx_Identity(&model);
        Mtx_Translate(&model, x, 0.0f, z, false);
        Mtx_RotateZ(&model, h * 0.0174532925f, false);
        renderer.drawModel(suitPlaceholder_, model);
    }
}

void TwoDGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (!eye || !target) return;
    eye->x = sim_.cameraX(); eye->y = -53.0f; eye->z = 17.3f; eye->w = 1.0f;
    target->x = sim_.cameraX(); target->y = 0.0f; target->z = 17.3f; target->w = 1.0f;
}

void TwoDGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = sim_.x();
    if (y) *y = sim_.y();
    if (z) *z = sim_.z();
    if (h) *h = sim_.heading();
}

const char* TwoDGame::line(int i) const {
    const int left = courseAssembled_ ? std::max(0, int(twodgame::hoodRules(hood_).gameDuration - timer_)) : 0;
    if (i == 0) std::snprintf(lines_[0], sizeof(lines_[0]), "Treasures: %d  Time: %d s", treasureCount_, left);
    else if (i == 1) std::snprintf(lines_[1], sizeof(lines_[1]), "%s", courseAssembled_ ? "Pad: move  A: jump  B: squirt" : "Receiving AI course...");
    else if (i == 2) std::snprintf(lines_[2], sizeof(lines_[2]), "Progress: %d%%", courseAssembled_ && course_.gameLength > 0.0f ? int(100.0f * (sim_.x() - twodgame::kGameStartX) / course_.gameLength) : 0);
    return i >= 0 && i < 3 ? lines_[i] : "";
}

} // namespace tt::game
