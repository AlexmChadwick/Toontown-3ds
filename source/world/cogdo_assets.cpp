#include "world/cogdo_assets.h"
#include "core/log.h"
#include <cmath>
#include <cstdio>
#include <cstring>
namespace tt::world {
namespace {
constexpr const char *kDirectory = "sdmc:/3ds/toontown3ds/data/cogdo/";
const char *const kMaze[] = {"maze_frame",      "maze_quadrant_1",
                             "maze_quadrant_2", "maze_quadrant_3",
                             "maze_quadrant_4", "maze_quadrant_5",
                             "maze_quadrant_6", "maze_quadrant_7",
                             "maze_quadrant_8", "maze_quadrant_9",
                             "maze_divider",    "exit_door",
                             "balloon",         "memo",
                             "cabinet"};
const char *const kSuite[] = {"suite", "suite_cage", "suite_door", "elevator"};
const char *const kBarrels[] = {"barrel_room", "barrel", "elevator"};
const char *const kCrane[] = {"prototype_room", "crane_arm",   "crane_magnet",
                              "crane_controls", "crane_stick", "money_bag"};
const char *const kBoardroom[] = {"prototype_room", "elevator"};
const char *const kBattleFloor[] = {"battle_room", "elevator"};
} // namespace
CogdoAssets::~CogdoAssets() {
    release();
}
int CogdoAssets::count() const {
    switch (scene_) {
    case CogdoScene::Maze:
        return 15;
    case CogdoScene::Suite:
        return 4;
    case CogdoScene::Barrels:
        return 3;
    case CogdoScene::Crane:
        return 6;
    case CogdoScene::Boardroom:
        return 2;
    case CogdoScene::BattleFloor:
        return 2;
    default:
        return 0;
    }
}
const char *CogdoAssets::name(int index) const {
    switch (scene_) {
    case CogdoScene::Maze:
        return kMaze[index];
    case CogdoScene::Suite:
        return kSuite[index];
    case CogdoScene::Barrels:
        return kBarrels[index];
    case CogdoScene::Crane:
        return kCrane[index];
    case CogdoScene::Boardroom:
        return kBoardroom[index];
    case CogdoScene::BattleFloor:
        return kBattleFloor[index];
    default:
        return "";
    }
}
void CogdoAssets::release() {
    if (ticket_)
        AssetLoader::cancel(ticket_);
    ticket_ = 0;
    for (render::Model &model : models)
        model.unload();
    collision.unload();
    ready_ = failed_ = checked_ = false;
    index_ = 0;
    scene_ = CogdoScene::None;
}
void CogdoAssets::select(CogdoScene scene) {
    if (scene_ == scene)
        return;
    release();
    scene_ = scene;
    TT_LOG("cogdo: loading scene %u", unsigned(scene));
}
bool CogdoAssets::budgets() const {
    uint64_t geometry = 0;
    uint64_t textures = 0;
    for (int index = 0; index < count(); ++index) {
        char path[160];
        std::snprintf(path, sizeof(path), "%s%s.ttm", kDirectory, name(index));
        FILE *file = std::fopen(path, "rb");
        if (!file)
            return false;
        uint32_t header[16] = {};
        bool valid = std::fread(header, 1, 64, file) == 64;
        uint16_t version = 0, textureCount = 0, batchCount = 0;
        std::memcpy(&version, reinterpret_cast<const uint8_t *>(header) + 4, 2);
        std::memcpy(&textureCount, reinterpret_cast<const uint8_t *>(header) + 8, 2);
        std::memcpy(&batchCount, reinterpret_cast<const uint8_t *>(header) + 10, 2);
        valid &= !std::memcmp(header, "TTM1", 4) && version == 1 && textureCount <= 64 && batchCount <= 150 &&
                 header[3] > 0 && header[3] <= 65535 && header[4] % 3 == 0;
        geometry += uint64_t(header[3]) * 16 + uint64_t(header[4]) * 2;
        valid &= std::fseek(file, 0, SEEK_END) == 0;
        const long size = std::ftell(file);
        valid &= size >= 64 && uint64_t(header[14]) + uint64_t(header[4]) * 2 <= uint64_t(size);
        valid &= std::fseek(file, header[11], SEEK_SET) == 0;
        for (unsigned texture = 0; valid && texture < textureCount; ++texture) {
            render::TexEntry entry{};
            valid = std::fread(&entry, 1, sizeof(entry), file) == sizeof(entry);
            valid &= entry.width >= 8 && entry.width <= 128 && entry.height >= 8 && entry.height <= 128 &&
                     uint64_t(entry.dataOffset) + entry.dataSize <= uint64_t(size);
            textures += entry.dataSize;
        }
        std::fclose(file);
        if (!valid)
            return false;
    }
    return geometry <= 6 * 1024 * 1024 && textures <= 8 * 1024 * 1024;
}
bool CogdoAssets::points() {
    char path[160];
    std::snprintf(path, sizeof(path), "%spoints.bin", kDirectory);
    FILE *file = std::fopen(path, "rb");
    char magic[4];
    uint16_t version = 0, count = 0;
    Vec3 values[5] = {};
    bool valid = file && std::fread(magic, 1, 4, file) == 4 && !std::memcmp(magic, "CGP1", 4) &&
                 std::fread(&version, 2, 1, file) == 1 && version == 1 && std::fread(&count, 2, 1, file) == 1 &&
                 count == 5 && std::fread(values, sizeof(values), 1, file) == 1 && std::fgetc(file) == EOF;
    if (file)
        std::fclose(file);
    for (Vec3 value : values)
        valid &= std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    if (!valid)
        return false;
    exteriorDoor = values[0];
    mazeEntrance = values[1];
    mazeExit = values[2];
    cage = values[3];
    cageDoor = values[4];
    return true;
}
void CogdoAssets::pump() {
    if (scene_ == CogdoScene::None || ready_ || failed_)
        return;
    if (!checked_) {
        checked_ = true;
        failed_ = !budgets();
        if (failed_)
            TT_LOG("cogdo: missing/malformed/over-budget asset group");
        if (failed_)
            return;
    }
    if (ticket_) {
        const LoadStatus status = AssetLoader::poll(ticket_);
        if (status == LoadStatus::Pending)
            return;
        if (status != LoadStatus::Ready || !AssetLoader::takeModel(ticket_, models[index_])) {
            AssetLoader::cancel(ticket_);
            ticket_ = 0;
            failed_ = true;
            return;
        }
        ticket_ = 0;
        ++index_;
    }
    char path[160];
    if (index_ < count()) {
        std::snprintf(path, sizeof(path), "%s%s.ttm", kDirectory, name(index_));
        ticket_ = AssetLoader::requestModel(path);
        return;
    }
    const char *collisionName = scene_ == CogdoScene::Maze          ? "maze_frame"
                                : scene_ == CogdoScene::Suite       ? "suite"
                                : scene_ == CogdoScene::Barrels     ? "barrel_room"
                                : scene_ == CogdoScene::BattleFloor ? "battle_room"
                                                                    : "prototype_room";
    std::snprintf(path, sizeof(path), "%s%s.ttc", kDirectory, collisionName);
    failed_ = !collision.load(path) || !points();
    ready_ = !failed_;
    TT_LOG("cogdo: scene %u ready %u", unsigned(scene_), unsigned(ready_));
}
} // namespace tt::world
