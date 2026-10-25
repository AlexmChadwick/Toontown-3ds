#include "world/clerk_kart_preview.h"
#include "ui/clerk_tables.h"
#include <cstdio>
#include <cstring>
namespace tt::world {
namespace {
constexpr uint64_t kGeometryLimit = 512 * 1024;
constexpr uint64_t kTextureLimit = 1024 * 1024;
} // namespace
void ClerkKartPreview::path(int code, char *output, int capacity) const {
    const int body = code / 1000, part = code % 1000;
    if (!part)
        std::snprintf(output, capacity, "sdmc:/3ds/toontown3ds/data/clerks/karts/kart_%d_body.ttm", body);
    else
        std::snprintf(output, capacity, "sdmc:/3ds/toontown3ds/data/clerks/karts/kart_%d_accessory_%d.ttm",
                      body, part - 1);
}
void ClerkKartPreview::select(const int8_t *dna) {
    active_ = dna != nullptr;
    if (dna)
        std::memcpy(dna_, dna, 9);
    for (int index = 0; index < 7; ++index) {
        int next = -1;
        if (dna && dna[0] >= 0 && dna[0] < 3) {
            const int part = index == 0 ? 0 : (index == 5 && dna[7] < 0) ? 41 : dna[index + 2] + 1;
            if (index == 0 || part > 0)
                next = dna[0] * 1000 + part;
        }
        if (wanted_[index] != next) {
            wanted_[index] = next;
            changed_ = true;
        }
    }
}
bool ClerkKartPreview::budgets() const {
    uint64_t geometry = 0, textures = 0;
    for (int code : wanted_) {
        if (code < 0)
            continue;
        char filename[160];
        path(code, filename, sizeof(filename));
        FILE *file = std::fopen(filename, "rb");
        uint32_t header[16] = {};
        bool valid = file && std::fread(header, 1, 64, file) == 64;
        if (valid) {
            uint16_t version = 0, count = 0;
            std::memcpy(&version, reinterpret_cast<const uint8_t *>(header) + 4, 2);
            std::memcpy(&count, reinterpret_cast<const uint8_t *>(header) + 8, 2);
            valid = !std::memcmp(header, "TTM1", 4) && version == 1 && count <= 64;
            geometry += uint64_t(header[3]) * 16 + uint64_t(header[4]) * 2;
            valid &= std::fseek(file, header[11], SEEK_SET) == 0;
            for (int index = 0; valid && index < count; ++index) {
                render::TexEntry entry{};
                valid = std::fread(&entry, sizeof(entry), 1, file) == 1 && entry.width <= 128 &&
                        entry.height <= 128;
                textures += entry.dataSize;
            }
        }
        if (file)
            std::fclose(file);
        if (!valid)
            return false;
    }
    return geometry <= kGeometryLimit && textures <= kTextureLimit;
}
void ClerkKartPreview::pump() {
    if (changed_) {
        if (ticket_)
            AssetLoader::cancel(ticket_);
        ticket_ = 0;
        for (int index = 0; index < 7; ++index) {
            if (loaded_[index] != wanted_[index]) {
                models_[index].unload();
                loaded_[index] = -1;
            }
        }
        failed_ = active_ && !budgets();
        changed_ = false;
    }
    if (!active_ || failed_)
        return;
    if (ticket_) {
        const auto status = AssetLoader::poll(ticket_);
        if (status == LoadStatus::Pending)
            return;
        if (status != LoadStatus::Ready || !AssetLoader::takeModel(ticket_, models_[ticketIndex_])) {
            AssetLoader::cancel(ticket_);
            ticket_ = 0;
            failed_ = true;
            return;
        }
        ticket_ = 0;
        loaded_[ticketIndex_] = wanted_[ticketIndex_];
    }
    for (int index = 0; index < 7; ++index) {
        if (wanted_[index] < 0 || wanted_[index] == loaded_[index])
            continue;
        char filename[160];
        path(wanted_[index], filename, sizeof(filename));
        ticket_ = AssetLoader::requestModel(filename);
        ticketIndex_ = index;
        return;
    }
}
void ClerkKartPreview::draw(render::Renderer &renderer, float x, float y, float z, float heading) {
    if (!active_ || failed_)
        return;
    render::Tints tints;
    tints.setAll(1, 1, 1);
    for (int slot = 1; slot <= 2; ++slot) {
        const int color = dna_[slot];
        if (color < 65 || color >= 92)
            continue;
        for (int channel = 0; channel < 4; ++channel)
            tints.rgba[slot][channel] = clerks::kKartAccessories[color].color[channel];
    }
    C3D_Mtx matrix;
    Mtx_Identity(&matrix);
    Mtx_Translate(&matrix, x, y, z, true);
    Mtx_RotateZ(&matrix, heading * .01745329252f, true);
    for (auto &model : models_)
        if (model.loaded())
            renderer.drawModel(model, matrix, &tints);
}
void ClerkKartPreview::shutdown() {
    if (ticket_)
        AssetLoader::cancel(ticket_);
    ticket_ = 0;
    for (auto &model : models_)
        model.unload();
    for (int index = 0; index < 7; ++index)
        wanted_[index] = loaded_[index] = -1;
    active_ = changed_ = failed_ = false;
}
} // namespace tt::world
