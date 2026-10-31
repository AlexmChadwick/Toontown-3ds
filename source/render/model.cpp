#include "render/model.h"
#include "render/upload_quota.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "core/load_profile.h"
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

void Model::setTextureLodBias(float bias) {
#ifdef __3DS__
    for (u16 i = 0; i < numTextures_; ++i)
        if (textures_[i].data) C3D_TexSetLodBias(&textures_[i], bias);
#else
    (void)bias;
#endif
}

u32 Model::moveTexturesToVram(u32 byteLimit) {
    mem::LinearGuard guard;
    u32 moved=0;
    for (u16 i=0; i<numTextures_; ++i) {
        C3D_Tex& old=textures_[i];
        const u32 bytes=C3D_TexCalcTotalSize(old.size, old.maxLevel);
        if (!old.data || bytes>byteLimit-moved || vramSpaceFree()<bytes+1024u*1024u) continue;
        C3D_Tex replacement{};
        C3D_TexInitParams p{};
        p.width=old.width; p.height=old.height; p.maxLevel=old.maxLevel;
        p.format=old.fmt; p.type=GPU_TEX_2D; p.onVram=true;
        if (!C3D_TexInitWithParams(&replacement, nullptr, p)) continue;
        for (int level=0; level<=old.maxLevel; ++level)
            C3D_TexLoadImage(&replacement, C3D_Tex2DGetImagePtr(&old, level, nullptr), GPU_TEXFACE_2D, level);
        replacement.param=old.param;
        replacement.border=old.border;
        replacement.lodParam=old.lodParam;
        // Existing linearBytes_ accounting includes a texture's base level.
        if (linearBytes_>=old.size) linearBytes_-=old.size;
        C3D_TexDelete(&old);
        old=replacement;
        moved+=bytes;
    }
    return moved;
}

bool Model::loadFrom(FILE* f, u32 base, const char* label, FILE* pool) {
    unload();
    const char* path = label ? label : "(embedded)";

    Header h;
    TexEntry* texEntries = nullptr;
    bool ok = readAt(f, base, &h, sizeof(h)) && memcmp(h.magic, "TTM1", 4) == 0 &&
              (h.version == kVersion || (h.version == kPooledVersion && pool)) && (h.flags & ~u16{1}) == 0;
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
    // One read of the vertex, index and inline-texture span. Textures are already
    // Morton-tiled at bake time; this copies them into the C3D_Tex. Header flags
    // bit0 means indices were stored absolute, so the rebase is skipped.
    const u32 vertBytes = numVertices_ * kVertexSize;
    const u32 indexBytes = numIndices_ * sizeof(u16);
    u32 spanLo = base + h.vertexOffset;
    u32 spanHi = spanLo + vertBytes;
    const u32 indexAt = base + h.indexOffset;
    if (indexAt < spanLo) spanLo = indexAt;
    if (indexAt + indexBytes > spanHi) spanHi = indexAt + indexBytes;
    if (ok && h.version != kPooledVersion) {
        for (u16 i = 0; i < numTextures_; i++) {
            const u32 at = texEntries[i].dataOffset;
            if (at < spanLo) spanLo = at;
            if (at + texEntries[i].dataSize > spanHi) spanHi = at + texEntries[i].dataSize;
        }
    }
    uint8_t* blob = nullptr;
    const u32 spanBytes = spanHi > spanLo ? spanHi - spanLo : 0;
    constexpr u32 kOneReadCap = 6u * 1024u * 1024u;
    if (ok && spanBytes > 0 && spanBytes <= kOneReadCap) {
        blob = static_cast<uint8_t*>(malloc(spanBytes));
        const u64 t0 = loadprof::nowUs();
        ok = blob && readAt(f, spanLo, blob, spanBytes);
        loadprof::addRead(spanBytes, static_cast<u32>(loadprof::nowUs() - t0));
    }
    auto fromBlob = [&](u32 fileOff, u32 bytes) -> const uint8_t* {
        if (!blob || fileOff < spanLo || fileOff + bytes > spanHi) return nullptr;
        return blob + (fileOff - spanLo);
    };
    {
        const u64 t0 = loadprof::nowUs();
        if (const uint8_t* src = ok ? fromBlob(base + h.vertexOffset, vertBytes) : nullptr)
            memcpy(vertices_, src, vertBytes);
        else if (ok) {
            ok = readAt(f, base + h.vertexOffset, vertices_, vertBytes);
            loadprof::addRead(vertBytes, 0);
        }
        if (const uint8_t* src = ok ? fromBlob(base + h.indexOffset, indexBytes) : nullptr)
            memcpy(indices_, src, indexBytes);
        else if (ok) {
            ok = readAt(f, base + h.indexOffset, indices_, indexBytes);
            loadprof::addRead(indexBytes, 0);
        }
        const bool absolute = ok && (h.flags & 1);
        if (ok && numVertices_ <= 0x10000) {
            for (u16 b = 0; ok && b < numBatches_; b++) {
                const Batch& bt = batches_[b];
                ok = bt.firstVertex <= numVertices_ && bt.numVertices <= numVertices_ - bt.firstVertex &&
                     bt.firstIndex <= numIndices_ && bt.numIndices <= numIndices_ - bt.firstIndex;
                if (!ok) break;
                u16* idx = indices_ + bt.firstIndex;
                const u32 baseV = bt.firstVertex;
                for (u32 i = 0; i < bt.numIndices; i++) {
                    if (absolute) {
                        if (idx[i] < baseV || idx[i] >= baseV + bt.numVertices) ok = false;
                    } else {
                        if (idx[i] >= bt.numVertices) ok = false;
                        idx[i] = static_cast<u16>(idx[i] + baseV);
                    }
                }
            }
            absoluteIndices_ = ok;
        }
        loadprof::addParse(static_cast<u32>(loadprof::nowUs() - t0));
    }
    if (ok) {
        linearBytes_ = vertBytes + indexBytes;
        GSPGPU_FlushDataCache(vertices_, vertBytes);
        GSPGPU_FlushDataCache(indices_, indexBytes);
    }

    for (u16 i = 0; ok && i < numTextures_; i++) {
        const u64 t0 = loadprof::nowUs();
        const uint8_t* src = fromBlob(texEntries[i].dataOffset, texEntries[i].dataSize);
        if (src) {
            // loadTexture reads from the file; copy into a tiny memfile only when the
            // span missed. The span hit is a memcpy into the linear texture below.
            C3D_TexInitParams p{};
            p.width = texEntries[i].width;
            p.height = texEntries[i].height;
            p.maxLevel = texEntries[i].mipLevels ? texEntries[i].mipLevels - 1 : 0;
            p.format = static_cast<GPU_TEXCOLOR>(texEntries[i].format);
            p.type = GPU_TEX_2D;
            p.onVram = false;
            bool inited;
            {
                mem::LinearGuard g;
                inited = C3D_TexInitWithParams(&textures_[i], nullptr, p);
            }
            const u32 total = inited ? C3D_TexCalcTotalSize(textures_[i].size, textures_[i].maxLevel) : 0;
            if (!inited || total != texEntries[i].dataSize) {
                if (inited) {
                    mem::LinearGuard g;
                    C3D_TexDelete(&textures_[i]);
                }
                textures_[i] = C3D_Tex{};
                ok = false;
            } else {
                memcpy(textures_[i].data, src, total);
                C3D_TexFlush(&textures_[i]);
                C3D_TexSetFilter(&textures_[i], GPU_LINEAR, GPU_LINEAR);
                if (textures_[i].maxLevel > 0) C3D_TexSetFilterMipmap(&textures_[i], GPU_LINEAR);
                C3D_TexSetWrap(&textures_[i], static_cast<GPU_TEXTURE_WRAP_PARAM>(texEntries[i].wrapS),
                               static_cast<GPU_TEXTURE_WRAP_PARAM>(texEntries[i].wrapT));
            }
        } else {
            const u64 tr = loadprof::nowUs();
            ok = loadTexture(texFile, texEntries[i], &textures_[i]);
            loadprof::addRead(texEntries[i].dataSize, static_cast<u32>(loadprof::nowUs() - tr));
        }
        loadprof::addTexUpload(static_cast<u32>(loadprof::nowUs() - t0));
        if (ok) linearBytes_ += textures_[i].size;
    }
    free(blob);

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

namespace {

u32 elapsedUs(u64 t0) {
    return static_cast<u32>((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000000));
}

} // namespace

void ModelStaging::reset() {
    free(batches_);
    free(tex_);
    free(verts_);
    free(indices_);
    free(texBytes_);
    free(texOff_);
    batches_ = nullptr;
    tex_ = nullptr;
    verts_ = nullptr;
    indices_ = nullptr;
    texBytes_ = nullptr;
    texOff_ = nullptr;
    stage_ = Stage::Empty;
    version_ = numTextures_ = numBatches_ = 0;
    numVertices_ = numIndices_ = 0;
    vertBytes_ = indexBytes_ = copiedV_ = copiedI_ = flushedV_ = flushedI_ = 0;
    rebased_ = texDone_ = 0;
    rebasedAt_ = 0;
    texFilled_ = 0;
    texReady_ = false;
    rebaseOk_ = true;
    absolute_ = false;
}

ModelStaging::~ModelStaging() { reset(); }

bool ModelStaging::read(FILE* f, u32 base, FILE* pool) {
    reset();
    Header h;
    if (!readAt(f, base, &h, sizeof(h)) || memcmp(h.magic, "TTM1", 4) ||
        (h.version != kVersion && h.version != kPooledVersion) ||
        (h.version == kPooledVersion && !pool) || (h.flags & ~u16{1}) != 0) {
        return false;
    }
    version_ = h.version;
    numTextures_ = h.numTextures;
    numBatches_ = h.numBatches;
    numVertices_ = h.numVertices;
    numIndices_ = h.numIndices;
    absolute_ = (h.flags & 1) != 0;
    memcpy(posScale_, h.posScale, sizeof(posScale_));
    memcpy(posOffset_, h.posOffset, sizeof(posOffset_));
    vertBytes_ = numVertices_ * kVertexSize;
    indexBytes_ = numIndices_ * sizeof(u16);
    batches_ = static_cast<Batch*>(malloc(sizeof(Batch) * (numBatches_ ? numBatches_ : 1)));
    tex_ = static_cast<TexEntry*>(malloc(sizeof(TexEntry) * (numTextures_ ? numTextures_ : 1)));
    verts_ = static_cast<u8*>(malloc(vertBytes_ ? vertBytes_ : 1));
    indices_ = static_cast<u16*>(malloc(indexBytes_ ? indexBytes_ : 1));
    texOff_ = static_cast<u32*>(calloc(numTextures_ ? numTextures_ : 1, sizeof(u32)));
    if (!batches_ || !tex_ || !verts_ || !indices_ || !texOff_ ||
        !readAt(f, base + h.batchTableOffset, batches_, sizeof(Batch) * numBatches_) ||
        !readAt(f, base + h.texTableOffset, tex_, sizeof(TexEntry) * numTextures_)) {
        reset();
        return false;
    }
    FILE* texFile = h.version == kPooledVersion ? pool : f;
    u32 bytes = 0;
    for (u16 i = 0; i < numTextures_; i++) {
        if (texFile == f) tex_[i].dataOffset += base;
        texOff_[i] = bytes;
        bytes += tex_[i].dataSize;
    }
    texBytes_ = static_cast<u8*>(malloc(bytes ? bytes : 1));
    if (!texBytes_) {
        reset();
        return false;
    }
    // One read of the vertex, index and inline-texture span. GPU upload stays in pump().
    // A pooled model keeps its textures in another file, so those stay separate reads.
    u32 spanLo = base + h.vertexOffset;
    u32 spanHi = spanLo + vertBytes_;
    const u32 indexAt = base + h.indexOffset;
    if (indexAt < spanLo) spanLo = indexAt;
    if (indexAt + indexBytes_ > spanHi) spanHi = indexAt + indexBytes_;
    if (texFile == f) {
        for (u16 i = 0; i < numTextures_; i++) {
            const u32 at = tex_[i].dataOffset;
            if (at < spanLo) spanLo = at;
            if (at + tex_[i].dataSize > spanHi) spanHi = at + tex_[i].dataSize;
        }
    }
    constexpr u32 kOneReadCap = 6u * 1024u * 1024u;
    const u32 spanBytes = spanHi > spanLo ? spanHi - spanLo : 0;
    uint8_t* blob = nullptr;
    bool spanned = false;
    if (spanBytes > 0 && spanBytes <= kOneReadCap) {
        blob = static_cast<uint8_t*>(malloc(spanBytes));
        const u64 t0 = loadprof::nowUs();
        spanned = blob && readAt(f, spanLo, blob, spanBytes);
        loadprof::addRead(spanBytes, static_cast<u32>(loadprof::nowUs() - t0));
    }
    auto fromBlob = [&](u32 fileOff, u32 n) -> const uint8_t* {
        if (!spanned || fileOff < spanLo || fileOff + n > spanHi) return nullptr;
        return blob + (fileOff - spanLo);
    };
    bool ok = true;
    if (const uint8_t* src = fromBlob(base + h.vertexOffset, vertBytes_)) memcpy(verts_, src, vertBytes_);
    else ok = readAt(f, base + h.vertexOffset, verts_, vertBytes_);
    if (ok) {
        if (const uint8_t* src = fromBlob(base + h.indexOffset, indexBytes_)) memcpy(indices_, src, indexBytes_);
        else ok = readAt(f, base + h.indexOffset, indices_, indexBytes_);
    }
    for (u16 i = 0; ok && i < numTextures_; i++) {
        if (const uint8_t* src = fromBlob(tex_[i].dataOffset, tex_[i].dataSize))
            memcpy(texBytes_ + texOff_[i], src, tex_[i].dataSize);
        else
            ok = readAt(texFile, tex_[i].dataOffset, texBytes_ + texOff_[i], tex_[i].dataSize);
    }
    free(blob);
    if (!ok) {
        reset();
        return false;
    }
    stage_ = Stage::Copy;
    return true;
}

ModelStaging::Result ModelStaging::pump(Model& dst, u32 budgetUs, u32 byteBudget) {
    if (stage_ == Stage::Done) return Result::Complete;
    if (stage_ == Stage::Empty) return Result::Error;
    const u64 t0 = svcGetSystemTick();
    auto over = [&]() { return elapsedUs(t0) >= budgetUs; };
    const u32 kSlice = std::max<u32>(2, std::min<u32>(64u*1024u, byteBudget) & ~1u);

    if (stage_ == Stage::Copy && !dst.vertices_) {
        dst.unload();
        dst.numTextures_ = numTextures_;
        dst.numBatches_ = numBatches_;
        dst.numVertices_ = numVertices_;
        dst.numIndices_ = numIndices_;
        memcpy(dst.posScale_, posScale_, sizeof(posScale_));
        memcpy(dst.posOffset_, posOffset_, sizeof(posOffset_));
        for (int i = 0; i < 3; i++) {
            dst.boundsMin_[i] = posOffset_[i] - 32767.0f * posScale_[i];
            dst.boundsMax_[i] = posOffset_[i] + 32767.0f * posScale_[i];
        }
        dst.batches_ = static_cast<Batch*>(malloc(sizeof(Batch) * (numBatches_ ? numBatches_ : 1)));
        dst.textures_ = static_cast<C3D_Tex*>(calloc(numTextures_ ? numTextures_ : 1, sizeof(C3D_Tex)));
        {
            mem::LinearGuard g;
            dst.vertices_ = linearAlloc(vertBytes_ ? vertBytes_ : 1);
            dst.indices_ = static_cast<u16*>(linearAlloc(indexBytes_ ? indexBytes_ : 1));
        }
        if (!dst.batches_ || !dst.textures_ || !dst.vertices_ || !dst.indices_) {
            dst.unload();
            reset();
            return Result::Error;
        }
        memcpy(dst.batches_, batches_, sizeof(Batch) * numBatches_);
        // The buffers are the whole mesh. Copying vertices in this same call is
        // the post-card hitch: Azahar's tick does not move during memcpy, so a
        // time check never stops the loop. The next pump copies a bounded slice.
        return Result::Pending;
    }
    // Bytes copied or rewritten this call. A stuck tick still ends the pump
    // after the byte quota; a running tick can stop earlier at budgetUs.
    UploadQuota quota{kSlice};
    auto sliceDone = [&](u32 bytes) {
        const bool exhausted = quota.consume(bytes);
        return exhausted || over();
    };
    if (stage_ == Stage::Copy) {
        auto* dstV = static_cast<u8*>(dst.vertices_);
        while (copiedV_ < vertBytes_) {
            const u32 n = std::min<u32>(kSlice-quota.worked, vertBytes_-copiedV_);
            memcpy(dstV + copiedV_, verts_ + copiedV_, n);
            copiedV_ += n;
            if (sliceDone(n)) return Result::Pending;
        }
        auto* dstI = reinterpret_cast<u8*>(dst.indices_);
        while (copiedI_ < indexBytes_) {
            const u32 n = std::min<u32>(kSlice-quota.worked, indexBytes_-copiedI_);
            memcpy(dstI + copiedI_, reinterpret_cast<u8*>(indices_) + copiedI_, n);
            copiedI_ += n;
            if (sliceDone(n)) return Result::Pending;
        }
        stage_ = Stage::Rebase;
    }
    if (stage_ == Stage::Rebase) {
        if (numVertices_ <= 0x10000) {
            while (rebased_ < numBatches_) {
                const Batch& bt = dst.batches_[rebased_];
                if (rebasedAt_ == 0) {
                    rebaseOk_ = rebaseOk_ && bt.firstVertex <= numVertices_ &&
                                bt.numVertices <= numVertices_ - bt.firstVertex &&
                                bt.firstIndex <= numIndices_ && bt.numIndices <= numIndices_ - bt.firstIndex;
                }
                if (rebaseOk_) {
                    u16* idx = dst.indices_ + bt.firstIndex;
                    const u32 kIndexSlice = (kSlice-quota.worked) / sizeof(u16);
                    while (rebasedAt_ < bt.numIndices) {
                        const u32 left = bt.numIndices - rebasedAt_;
                        const u32 n = left > kIndexSlice ? kIndexSlice : left;
                        for (u32 i = 0; i < n; i++) {
                            const u32 at = rebasedAt_ + i;
                            if (absolute_) {
                                if (idx[at] < bt.firstVertex || idx[at] >= bt.firstVertex + bt.numVertices)
                                    rebaseOk_ = false;
                            } else {
                                if (idx[at] >= bt.numVertices) rebaseOk_ = false;
                                else idx[at] = static_cast<u16>(idx[at] + bt.firstVertex);
                            }
                        }
                        rebasedAt_ += n;
                        if (sliceDone(n * sizeof(u16))) return Result::Pending;
                    }
                }
                rebased_++;
                rebasedAt_ = 0;
                if (over()) return Result::Pending;
            }
            dst.absoluteIndices_ = rebaseOk_;
        }
        if (!rebaseOk_) {
            dst.unload();
            reset();
            return Result::Error;
        }
        stage_ = Stage::Flush;
    }
    if (stage_ == Stage::Flush) {
        while (flushedV_ < vertBytes_) {
            const u32 n = std::min<u32>(kSlice-quota.worked, vertBytes_-flushedV_);
            GSPGPU_FlushDataCache(static_cast<u8*>(dst.vertices_) + flushedV_, n);
            flushedV_ += n;
            if (sliceDone(n)) return Result::Pending;
        }
        while (flushedI_ < indexBytes_) {
            const u32 n = std::min<u32>(kSlice-quota.worked, indexBytes_-flushedI_);
            GSPGPU_FlushDataCache(reinterpret_cast<u8*>(dst.indices_) + flushedI_, n);
            flushedI_ += n;
            if (sliceDone(n)) return Result::Pending;
        }
        dst.linearBytes_ = vertBytes_ + indexBytes_;
        stage_ = Stage::Textures;
    }
    if (stage_ == Stage::Textures) {
        // One texture used to be copied and flushed whole. A 15-texture near
        // mesh is ~566 KB and that single pump was a 133 ms hitch. Copy and
        // flush 64 KB at a time; the model is not drawn until Complete.
        while (texDone_ < numTextures_) {
            if (over() && !texReady_) return Result::Pending;
            C3D_Tex* tex = &dst.textures_[texDone_];
            const TexEntry& e = tex_[texDone_];
            if (!texReady_) {
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
                    *tex = C3D_Tex{};
                    dst.unload();
                    reset();
                    return Result::Error;
                }
                const u32 total = C3D_TexCalcTotalSize(tex->size, tex->maxLevel);
                if (total != e.dataSize || !texBytes_) {
                    mem::LinearGuard g;
                    C3D_TexDelete(tex);
                    *tex = C3D_Tex{};
                    dst.unload();
                    reset();
                    return Result::Error;
                }
                texReady_ = true;
                texFilled_ = 0;
                return Result::Pending; // allocation never shares a frame with copying
            }
            const u8* src = texBytes_ + texOff_[texDone_];
            while (texFilled_ < e.dataSize) {
                const u32 n = std::min<u32>(kSlice-quota.worked, e.dataSize-texFilled_);
                memcpy(static_cast<u8*>(tex->data) + texFilled_, src + texFilled_, n);
                GSPGPU_FlushDataCache(static_cast<u8*>(tex->data) + texFilled_, n);
                texFilled_ += n;
                if (sliceDone(n)) return Result::Pending;
            }
            C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
            if (tex->maxLevel > 0) C3D_TexSetFilterMipmap(tex, GPU_LINEAR);
            C3D_TexSetWrap(tex, static_cast<GPU_TEXTURE_WRAP_PARAM>(e.wrapS),
                           static_cast<GPU_TEXTURE_WRAP_PARAM>(e.wrapT));
            dst.linearBytes_ += tex->size;
            texDone_++;
            texReady_ = false;
            texFilled_ = 0;
            if (over()) return Result::Pending;
        }
        TT_LOG("model: staged %lu tris, %u batches, %u tex, %lu KB linear",
               (unsigned long)(numIndices_ / 3), numBatches_, numTextures_,
               (unsigned long)(dst.linearBytes_ / 1024));
        reset(); // heap copy is in the model now
        stage_ = Stage::Done;
        return Result::Complete;
    }
    return Result::Error;
}

} // namespace tt::render
