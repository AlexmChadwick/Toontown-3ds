#include "world/cj_assets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "core/log.h"

namespace tt::world {
namespace {
constexpr const char* kDirectory = "sdmc:/3ds/toontown3ds/data/boss/";
constexpr uint32_t kGeometryLimit = 6 * 1024 * 1024;
const char* const kNames[] = {
    "cj_legs", "cj_torso", "cj_head", "cj_treads", "cj_arena", "cj_jury_box",
    "cj_scale_stand", "cj_scale_beam", "cj_scale_defense", "cj_scale_prosecution",
    "cj_cannon_base", "cj_cannon_barrel", "cj_chair", "cj_gavel", "cj_evidence", "cj_lawbook"
};
bool clipBudget(const char* name, const render::Model* models) {
    uint64_t total = 0;
    for (int part = 0; part < 4; ++part) {
        char path[160];
        std::snprintf(path, sizeof(path), "%s%s_%s.tta", kDirectory, kNames[part], name);
        FILE* file = std::fopen(path, "rb");
        uint32_t header[16] = {};
        const bool read = file && std::fread(header, 1, 64, file) == 64;
        long size = 0;
        if (file) {
            if (std::fseek(file, 0, SEEK_END) == 0) size = std::ftell(file);
            std::fclose(file);
        }
        uint16_t shorts[32];
        float floats[16];
        std::memcpy(shorts, header, sizeof(header));
        std::memcpy(floats, header, sizeof(header));
        const uint32_t vertices = header[2];
        const uint32_t frames = shorts[6];
        const uint64_t end = uint64_t(header[11]) + uint64_t(frames) * header[12];
        const bool attachment = (shorts[3] & 1) != 0;
        if (!read || std::memcmp(header, "TTA1", 4) || shorts[2] != 1 || shorts[3] > 1 ||
            shorts[7] || vertices != models[part].numVertices() || !vertices || vertices > 4096 ||
            !frames || frames > 72 || !std::isfinite(floats[4]) || floats[4] <= 0 || floats[4] > 24 ||
            header[12] != vertices * 8 || header[11] < 64 || size < 0 || end > uint64_t(size) ||
            (attachment && (header[13] < end || uint64_t(header[13]) + frames * 48 > uint64_t(size))) ||
            (part < 2 && !attachment)) return false;
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(floats[5 + axis]) || floats[5 + axis] <= 0 ||
                !std::isfinite(floats[8 + axis])) return false;
        }
        total += uint64_t(frames) * vertices * 8;
    }
    return total <= CjAssets::kClipLimit;
}

bool modelBudgets() {
    uint64_t geometry = 0;
    uint64_t textures = 0;
    for (int index = 0; index < CjAssets::kModels; ++index) {
        char path[160];
        std::snprintf(path, sizeof(path), "%s%s.ttm", kDirectory, kNames[index]);
        FILE* file = std::fopen(path, "rb");
        uint32_t header[16] = {};
        if (!file) return false;
        bool valid = std::fread(header, 1, 64, file) == 64;
        uint16_t version = 0;
        uint16_t textureCount = 0;
        uint16_t batchCount = 0;
        std::memcpy(&version, reinterpret_cast<const uint8_t*>(header) + 4, 2);
        std::memcpy(&textureCount, reinterpret_cast<const uint8_t*>(header) + 8, 2);
        std::memcpy(&batchCount, reinterpret_cast<const uint8_t*>(header) + 10, 2);
        valid &= !std::memcmp(header, "TTM1", 4) && version == 1 && textureCount <= 64 && batchCount <= 150;
        valid &= header[3] > 0 && header[3] <= 65535 && header[4] % 3 == 0;
        geometry += uint64_t(header[3]) * 16 + uint64_t(header[4]) * 2;
        if (std::fseek(file, 0, SEEK_END)) valid = false;
        const long size = std::ftell(file);
        valid &= size >= 64 && uint64_t(header[14]) + uint64_t(header[4]) * 2 <= uint64_t(std::max(0L, size));
        if (std::fseek(file, header[11], SEEK_SET)) valid = false;
        for (int index = 0; valid && index < textureCount; ++index) {
            render::TexEntry texture{};
            valid = std::fread(&texture, 1, sizeof(texture), file) == sizeof(texture);
            valid &= texture.width > 0 && texture.width <= 128 && texture.height > 0 && texture.height <= 128;
            valid &= uint64_t(texture.dataOffset) + texture.dataSize <= uint64_t(size);
            textures += texture.dataSize;
        }
        std::fclose(file);
        if (!valid) return false;
    }
    return geometry + 2 * CjAssets::kClipLimit <= kGeometryLimit && textures <= 8 * 1024 * 1024;
}

} // namespace

CjAssets::~CjAssets() {
    release();
}
void CjAssets::release() {
    if (modelTicket_) AssetLoader::cancel(modelTicket_);
    modelTicket_ = 0;
    for (int part = 0; part < 4; ++part) {
        if (clipTickets_[part]) AssetLoader::cancel(clipTickets_[part]);
        clipTickets_[part] = 0;
        animations[part].unload();
    }
    for (render::Model& model : models) model.unload();
    collision.unload();
    ready_ = false;
    failed_ = false;
    checked_ = false;
    modelIndex_ = 0;
    currentClip[0] = pendingClip_[0] = failedClip_[0] = 0;
    clipTime = 0;
}
void CjAssets::pump(const char* clip) {
    if (failed_) return;
    if (!checked_) {
        checked_ = true;
        failed_ = !modelBudgets();
        if (failed_) TT_LOG("CJ: model header or aggregate memory budget rejected");
        if (failed_) return;
    }
    if (!ready_) {
        if (modelTicket_) {
            const LoadStatus status = AssetLoader::poll(modelTicket_);
            if (status == LoadStatus::Pending) return;
            if (status != LoadStatus::Ready || !AssetLoader::takeModel(modelTicket_, models[modelIndex_])) {
                AssetLoader::cancel(modelTicket_);
                modelTicket_ = 0;
                failed_ = true;
                return;
            }
            modelTicket_ = 0;
            ++modelIndex_;
        }
        if (modelIndex_ < kModels) {
            char path[160];
            std::snprintf(path, sizeof(path), "%s%s.ttm", kDirectory, kNames[modelIndex_]);
            modelTicket_ = AssetLoader::requestModel(path);
            return;
        }
        char path[160];
        std::snprintf(path, sizeof(path), "%scj_arena.ttc", kDirectory);
        if (!collision.load(path)) {
            failed_ = true;
            return;
        }
        std::snprintf(path, sizeof(path), "%scj_scene.bin", kDirectory);
        FILE* file = std::fopen(path, "rb");
        char magic[4] = {};
        uint16_t version = 0;
        uint16_t count = 0;
        Vec3 vectors[4] = {};
        bool valid = file && std::fread(magic, 1, 4, file) == 4 && !std::memcmp(magic, "CJS1", 4) &&
            std::fread(&version, 2, 1, file) == 1 && version == 1 &&
            std::fread(&count, 2, 1, file) == 1 && count == 4 &&
            std::fread(vectors, sizeof(Vec3), 4, file) == 4;
        if (file) std::fclose(file);
        for (Vec3 vector : vectors) valid &= std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
        if (!valid) {
            failed_ = true;
            return;
        }
        barrelPivot = vectors[0];
        beamPivot = vectors[1];
        defenseLocal = vectors[2];
        prosecutionLocal = vectors[3];
        ready_ = true;
        TT_LOG("CJ: models/collision loaded, four-part clip cap %lu", static_cast<unsigned long>(kClipLimit));
    }
    if (!pendingClip_[0] && std::strcmp(currentClip, clip) && std::strcmp(failedClip_, clip)) {
        if (!clipBudget(clip, models)) {
            failed_ = true;
            std::snprintf(failedClip_, sizeof(failedClip_), "%s", clip);
            TT_LOG("CJ: missing, malformed or over-budget clip %s", clip);
        } else {
            std::snprintf(pendingClip_, sizeof(pendingClip_), "%s", clip);
        }
    }
    if (!pendingClip_[0]) return;
    bool ready = true;
    for (int part = 0; part < 4; ++part) {
        if (!clipTickets_[part]) {
            char path[160];
            std::snprintf(path, sizeof(path), "%s%s_%s.tta", kDirectory, kNames[part], pendingClip_);
            clipTickets_[part] = AssetLoader::requestAnim(path);
        }
        const LoadStatus status = AssetLoader::poll(clipTickets_[part]);
        if (status == LoadStatus::Failed) {
            for (LoadTicket& ticket : clipTickets_) {
                if (ticket) AssetLoader::cancel(ticket);
                ticket = 0;
            }
            failed_ = true;
            return;
        }
        ready &= status == LoadStatus::Ready;
    }
    if (!ready) return;
    for (int part = 0; part < 4; ++part) {
        animations[part].unload();
        AssetLoader::takeAnim(clipTickets_[part], animations[part]);
        clipTickets_[part] = 0;
    }
    std::snprintf(currentClip, sizeof(currentClip), "%s", pendingClip_);
    pendingClip_[0] = 0;
    clipTime = 0;
}
} // namespace tt::world
