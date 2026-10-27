#include "render/model.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "core/log.h"
#include "core/memory.h"

namespace tt::render {

namespace {

constexpr u16 kVersion = 1;
// v2: identical layout, but texture data lives in the directory's shared pool file
// (textures.ttp, tools/pipeline/ttm_pool.py) and dataOffset is an offset into that file.
constexpr u16 kPooledVersion = 2;
constexpr u32 kVertexSize = 16;

struct Header {
    char magic[4];
    u16 version;
    u16 flags;
    u16 numTextures;
    u16 numBatches;
    u32 numVertices;
    u32 numIndices;
    float posScale[3];
    float posOffset[3];
    u32 texTableOffset;
    u32 batchTableOffset;
    u32 vertexOffset;
    u32 indexOffset;
    u32 reserved;
};
static_assert(sizeof(Header) == 64, "must match ttm.py header");


bool readAt(FILE* f, u32 offset, void* dst, u32 size) {
    return fseek(f, offset, SEEK_SET) == 0 && fread(dst, 1, size, f) == size;
}

} // namespace

bool loadTexture(FILE* f, const TexEntry& e, C3D_Tex* tex) {
    C3D_TexInitParams p{};
    p.width = e.width;
    p.height = e.height;
    p.maxLevel = e.mipLevels ? e.mipLevels - 1 : 0;
    p.format = static_cast<GPU_TEXCOLOR>(e.format);
    p.type = GPU_TEX_2D;
    p.onVram = false;
    bool inited;
    {
        mem::LinearGuard g;
        inited = C3D_TexInitWithParams(tex, nullptr, p);
    }
    if (!inited) {
        TT_LOG("texture: out of linear memory for %.32s", e.name);
        *tex = C3D_Tex{};
        return false;
    }
    const u32 total = C3D_TexCalcTotalSize(tex->size, tex->maxLevel);
    if (total != e.dataSize || !readAt(f, e.dataOffset, tex->data, total)) {
        TT_LOG("texture: %.32s size mismatch or short read (%lu != %lu)", e.name, (unsigned long)total,
               (unsigned long)e.dataSize);
        {
            mem::LinearGuard g;
            C3D_TexDelete(tex);
        }
        *tex = C3D_Tex{};
        return false;
    }
    C3D_TexFlush(tex);
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
    if (tex->maxLevel > 0) C3D_TexSetFilterMipmap(tex, GPU_LINEAR);
    C3D_TexSetWrap(tex, static_cast<GPU_TEXTURE_WRAP_PARAM>(e.wrapS), static_cast<GPU_TEXTURE_WRAP_PARAM>(e.wrapT));
    return true;
}

void Tints::setAll(float r, float g, float b) {
    for (auto& t : rgba) {
        t[0] = r;
        t[1] = g;
        t[2] = b;
        t[3] = 1.0f;
    }
}

Model::~Model() { unload(); }

void Model::unload() {
    mem::LinearGuard g;
    if (textures_) {
        for (u16 i = 0; i < numTextures_; i++) {
            if (textures_[i].data) C3D_TexDelete(&textures_[i]);
        }
        free(textures_);
    }
    free(batches_);
    if (vertices_) linearFree(vertices_);
    if (indices_) linearFree(indices_);
    textures_ = nullptr;
    batches_ = nullptr;
    vertices_ = nullptr;
    indices_ = nullptr;
    numTextures_ = numBatches_ = 0;
    numVertices_ = numIndices_ = 0;
    linearBytes_ = 0;
    absoluteIndices_ = false;
}

bool Model::load(const char* path) {
    unload();
    FILE* f = fopen(path, "rb");
    if (!f) {
        TT_LOG("model: cannot open %s", path);
        return false;
    }
    // Large reads go straight to their destination; a bigger stdio buffer cuts
    // per-call overhead on RomFS/SD.
    setvbuf(f, nullptr, _IOFBF, 32 * 1024);
    // A pooled (v2) model reads its textures from textures.ttp next to it.
    FILE* pool = nullptr;
    Header h;
    if (readAt(f, 0, &h, sizeof(h)) && !memcmp(h.magic, "TTM1", 4) && h.version == kPooledVersion) {
        char poolPath[160];
        const char* slash = strrchr(path, '/');
        const int dir = slash ? static_cast<int>(slash - path + 1) : 0;
        snprintf(poolPath, sizeof(poolPath), "%.*stextures.ttp", dir, path);
        pool = fopen(poolPath, "rb");
        char magic[8] = {};
        if (pool && (fread(magic, 1, 8, pool) != 8 || memcmp(magic, "TTP1\x01\0\0\0", 8))) {
            fclose(pool);
            pool = nullptr;
        }
        if (!pool) TT_LOG("model: %s needs %s (missing or bad)", path, poolPath);
    }
    const bool ok = loadFrom(f, 0, path, pool);
    if (pool) fclose(pool);
    fclose(f);
    return ok;
}

void Model::swap(Model& o) {
    std::swap(numTextures_, o.numTextures_);
    std::swap(numBatches_, o.numBatches_);
    std::swap(numVertices_, o.numVertices_);
    std::swap(numIndices_, o.numIndices_);
    for (int i = 0; i < 3; i++) {
        std::swap(posScale_[i], o.posScale_[i]);
        std::swap(posOffset_[i], o.posOffset_[i]);
        std::swap(boundsMin_[i], o.boundsMin_[i]);
        std::swap(boundsMax_[i], o.boundsMax_[i]);
    }
    std::swap(textures_, o.textures_);
    std::swap(batches_, o.batches_);
    std::swap(vertices_, o.vertices_);
    std::swap(indices_, o.indices_);
    std::swap(linearBytes_, o.linearBytes_);
    std::swap(absoluteIndices_, o.absoluteIndices_);
}

bool Model::loadFrom(FILE* f, u32 base, const char* label, FILE* pool) {
    unload();
    const char* path = label ? label : "(embedded)";

    Header h;
    TexEntry* texEntries = nullptr;
    bool ok = readAt(f, base, &h, sizeof(h)) && memcmp(h.magic, "TTM1", 4) == 0 &&
              (h.version == kVersion || (h.version == kPooledVersion && pool));
    if (!ok) {
        TT_LOG("model: bad header in %s", path);
        return false;
    }

    numTextures_ = h.numTextures;
    numBatches_ = h.numBatches;
    numVertices_ = h.numVertices;
    numIndices_ = h.numIndices;
    memcpy(posScale_, h.posScale, sizeof(posScale_));
    memcpy(posOffset_, h.posOffset, sizeof(posOffset_));
    for (int i = 0; i < 3; i++) {
        boundsMin_[i] = posOffset_[i] - 32767.0f * posScale_[i];
        boundsMax_[i] = posOffset_[i] + 32767.0f * posScale_[i];
    }

    batches_ = static_cast<Batch*>(malloc(sizeof(Batch) * numBatches_));
    texEntries = static_cast<TexEntry*>(malloc(sizeof(TexEntry) * (numTextures_ ? numTextures_ : 1)));
    textures_ = static_cast<C3D_Tex*>(calloc(numTextures_ ? numTextures_ : 1, sizeof(C3D_Tex)));
    {
        mem::LinearGuard g;
        vertices_ = linearAlloc(numVertices_ * kVertexSize);
        indices_ = static_cast<u16*>(linearAlloc(numIndices_ * sizeof(u16)));
    }
    ok = batches_ && texEntries && textures_ && vertices_ && indices_;
    ok = ok && readAt(f, base + h.batchTableOffset, batches_, sizeof(Batch) * numBatches_);
    ok = ok && readAt(f, base + h.texTableOffset, texEntries, sizeof(TexEntry) * numTextures_);
    FILE* texFile = h.version == kPooledVersion ? pool : f;
    for (u16 i = 0; ok && texFile == f && i < numTextures_; i++) texEntries[i].dataOffset += base; // file-relative
    ok = ok && readAt(f, base + h.vertexOffset, vertices_, numVertices_ * kVertexSize);
    ok = ok && readAt(f, base + h.indexOffset, indices_, numIndices_ * sizeof(u16));
    if (ok && numVertices_ <= 0x10000) {
        // Rebase batch-relative indices to absolute so the renderer binds one vertex
        // buffer per model instead of one per batch (keeps the GPU command buffer small).
        // Also validates every index against its batch (cheap here, in memory).
        for (u16 b = 0; ok && b < numBatches_; b++) {
            const Batch& bt = batches_[b];
            ok = bt.firstVertex <= numVertices_ && bt.numVertices <= numVertices_ - bt.firstVertex &&
                 bt.firstIndex <= numIndices_ && bt.numIndices <= numIndices_ - bt.firstIndex;
            if (!ok) break;
            u16* idx = indices_ + bt.firstIndex;
            const u32 base = bt.firstVertex;
            for (u32 i = 0; i < bt.numIndices; i++) {
                if (idx[i] >= bt.numVertices) ok = false;
                idx[i] = static_cast<u16>(idx[i] + base);
            }
        }
        absoluteIndices_ = ok;
    }
    if (ok) {
        linearBytes_ = numVertices_ * kVertexSize + numIndices_ * sizeof(u16);
        GSPGPU_FlushDataCache(vertices_, numVertices_ * kVertexSize);
        GSPGPU_FlushDataCache(indices_, numIndices_ * sizeof(u16));
    }

    for (u16 i = 0; ok && i < numTextures_; i++) {
        ok = loadTexture(texFile, texEntries[i], &textures_[i]);
        if (ok) linearBytes_ += textures_[i].size;
    }

    free(texEntries);
    if (!ok) {
        TT_LOG("model: failed to load %s", path);
        unload();
        return false;
    }
    TT_LOG("model: %s: %lu tris, %u batches, %u tex, %lu KB linear", path, (unsigned long)numTriangles(),
           numBatches_, numTextures_, (unsigned long)(linearBytes_ / 1024));
    return true;
}

} // namespace tt::render
