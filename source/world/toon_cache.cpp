#include "world/toon_cache.h"

#include <cstdlib>
#include <initializer_list>
#include <cstring>
#include <sys/stat.h>

#include "core/log.h"
#include "core/memory.h"

namespace tt::world {

namespace {

const char* const kDirs[] = {"sdmc:/3ds/toontown3ds/data/toon", "romfs:/baked/toon"};
// Clip names as baked; order matches ToonAnim (toon_avatar.h).
const char* const kClipNames[kToonAnimCount] = {"neutral", "walk", "run", "jump-idle", "wave"};

struct TtxHeader {
    char magic[4];
    u16 version;
    u16 numTextures;
    u16 counts[4];
    u32 mapOffset;
    u32 texOffset;
    u32 reserved[2];
};
static_assert(sizeof(TtxHeader) == 32, "must match bake_toon.py write_ttx");

bool fileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

struct Lock {
    explicit Lock(LightLock& l) : l_(l) { LightLock_Lock(&l_); }
    ~Lock() { LightLock_Unlock(&l_); }
    LightLock& l_;
};

u32 msSince(u64 t0) { return static_cast<u32>((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000)); }

} // namespace

bool ToonLook::ready() const {
    if (!valid()) return false;
    for (const ToonPart* p : {legs, torso, head}) {
        if (p->state != SlotState::Ready) return false;
    }
    if (lashes && lashes->state == SlotState::Queued) return false;
    for (const ToonClothing* c : clothes) {
        if (c && c->state == SlotState::Queued) return false;
    }
    return true;
}

void ToonLook::slotTextures(const C3D_Tex* out[render::kTexSlotCount]) const {
    for (u32 s = 0; s < render::kTexSlotCount; s++) {
        out[s] = clothes[s] && clothes[s]->state == SlotState::Ready ? &clothes[s]->tex : nullptr;
    }
}

bool ToonCache::findFile(const char* file, char* out, size_t outSize) const {
    for (const char* dir : kDirs) {
        snprintf(out, outSize, "%s/%s", dir, file);
        if (fileExists(out)) return true;
    }
    return false;
}

bool ToonCache::init() {
    LightLock_Init(&lock_);
    LightEvent_Init(&wake_, RESET_ONESHOT);
    quit_ = false;

    char path[128];
    if (findFile("clothes.ttx", path, sizeof(path))) {
        clothesFile_ = fopen(path, "rb");
        TtxHeader h;
        bool ok = clothesFile_ && fread(&h, sizeof(h), 1, clothesFile_) == 1 && !memcmp(h.magic, "TTX1", 4) &&
                  h.version == 1;
        if (ok) {
            const u32 entries = h.counts[0] + h.counts[1] + h.counts[2] + h.counts[3];
            memcpy(clothesCounts_, h.counts, sizeof(clothesCounts_));
            numClothesTex_ = h.numTextures;
            clothesMap_ = static_cast<u16*>(malloc(entries * sizeof(u16)));
            clothesEntries_ = static_cast<render::TexEntry*>(malloc(numClothesTex_ * sizeof(render::TexEntry)));
            ok = clothesMap_ && clothesEntries_ && fseek(clothesFile_, h.mapOffset, SEEK_SET) == 0 &&
                 fread(clothesMap_, sizeof(u16), entries, clothesFile_) == entries &&
                 fseek(clothesFile_, h.texOffset, SEEK_SET) == 0 &&
                 fread(clothesEntries_, sizeof(render::TexEntry), numClothesTex_, clothesFile_) == numClothesTex_;
        }
        if (ok) {
            TT_LOG("toon: clothes pack %s (%u textures)", path, numClothesTex_);
        } else {
            TT_LOG("toon: bad clothes pack %s; models keep their own clothing", path);
            if (clothesFile_) fclose(clothesFile_);
            clothesFile_ = nullptr;
            numClothesTex_ = 0;
        }
    } else {
        TT_LOG("toon: no clothes.ttx; models keep their own clothing");
    }

    // Main thread runs at 0x30; 0x31 only runs while the main thread waits (vsync, GPU),
    // so loading never takes frame time. Core 0 = the Old 3DS application core.
    loader_ = threadCreate(loaderMain, this, 32 * 1024, 0x31, 0, false);
    if (!loader_) TT_LOG("toon: loader thread failed; parts load synchronously");
    return true;
}

bool ToonCache::initFallback() {
    if (!acquire(defaultToonDna(), ToonLod::High, &fallback_)) return false;
    const u64 t0 = svcGetSystemTick();
    while (!fallback_.ready()) {
        for (const ToonPart* p : {fallback_.legs, fallback_.torso, fallback_.head}) {
            if (p->state == SlotState::Failed) return false;
        }
        if (!loader_) loaderStep();
        svcSleepThread(2 * 1000000LL);
    }
    TT_LOG("toon: default toon ready in %lu ms", static_cast<unsigned long>(msSince(t0)));
    return true;
}

void ToonCache::shutdown() {
    if (loader_) {
        quit_ = true;
        LightEvent_Signal(&wake_);
        threadJoin(loader_, U64_MAX);
        threadFree(loader_);
        loader_ = nullptr;
    }
    for (ToonPart& p : parts_) {
        p.model.unload();
        for (auto& a : p.anim) a.unload();
        p.key[0] = '\0';
        p.refs = 0;
        p.bytes = 0;
        p.state = SlotState::Free;
    }
    for (ToonClothing& c : clothes_) {
        if (c.state == SlotState::Ready) {
            mem::LinearGuard g;
            C3D_TexDelete(&c.tex);
        }
        c = ToonClothing{};
    }
    fallback_ = ToonLook{};
    if (clothesFile_) fclose(clothesFile_);
    clothesFile_ = nullptr;
    free(clothesMap_);
    free(clothesEntries_);
    clothesMap_ = nullptr;
    clothesEntries_ = nullptr;
}

u32 ToonCache::linearBytes() const {
    u32 total = 0;
    for (const ToonPart& p : parts_) total += p.state == SlotState::Ready ? p.bytes : 0;
    for (const ToonClothing& c : clothes_) {
        if (c.state == SlotState::Ready) total += C3D_TexCalcTotalSize(c.tex.size, c.tex.maxLevel);
    }
    return total;
}

int ToonCache::partsLoaded() const {
    int n = 0;
    for (const ToonPart& p : parts_) n += p.state == SlotState::Ready;
    return n;
}

int ToonCache::pendingLoads() const {
    int n = 0;
    for (const ToonPart& p : parts_) n += p.state == SlotState::Queued;
    for (const ToonClothing& c : clothes_) n += c.state == SlotState::Queued;
    return n;
}

// ---- main thread: booking ----

ToonPart* ToonCache::bookPart(const char* key, bool animated) {
    Lock l(lock_);
    clock_++;
    for (ToonPart& p : parts_) {
        if (p.state != SlotState::Free && !strcmp(p.key, key)) {
            if (p.state == SlotState::Failed) return nullptr;
            p.refs++;
            p.lastUsed = clock_;
            return &p;
        }
    }
    // A free slot, else recycle the least recently used unreferenced one (the loader
    // frees its old data before loading the new part into it).
    ToonPart* slot = nullptr;
    for (ToonPart& p : parts_) {
        if (p.state == SlotState::Free) {
            slot = &p;
            break;
        }
    }
    if (!slot) {
        for (ToonPart& p : parts_) {
            if (p.refs == 0 && p.state != SlotState::Queued && (!slot || p.lastUsed < slot->lastUsed)) slot = &p;
        }
    }
    if (!slot) {
        TT_LOG("toon: part table full (%d)", kMaxParts);
        return nullptr;
    }
    snprintf(slot->key, sizeof(slot->key), "%s", key);
    slot->refs = 1;
    slot->lastUsed = clock_;
    slot->animated = animated;
    __sync_synchronize();
    slot->state = SlotState::Queued;
    LightEvent_Signal(&wake_);
    return slot;
}

ToonClothing* ToonCache::bookClothing(u8 table, u8 index) {
    if (!clothesFile_ || table >= 4) return nullptr;
    u32 base = 0;
    for (u8 t = 0; t < table; t++) base += clothesCounts_[t];
    if (index >= clothesCounts_[table]) index = 0; // upstream falls back to entry 0
    u16 texIndex = clothesMap_[base + index];
    if (texIndex == 0xFFFF) texIndex = clothesMap_[base]; // not in this (boot) pack
    if (texIndex == 0xFFFF || texIndex >= numClothesTex_) return nullptr;

    Lock l(lock_);
    clock_++;
    ToonClothing* slot = nullptr;
    for (ToonClothing& c : clothes_) {
        if (c.state != SlotState::Free && c.texIndex == texIndex) {
            if (c.state == SlotState::Failed) return nullptr;
            c.refs++;
            c.lastUsed = clock_;
            return &c;
        }
        if (c.state == SlotState::Free && !slot) slot = &c;
    }
    if (!slot) {
        for (ToonClothing& c : clothes_) {
            if (c.refs == 0 && c.state != SlotState::Queued && (!slot || c.lastUsed < slot->lastUsed)) slot = &c;
        }
    }
    if (!slot) return nullptr;
    slot->texIndex = texIndex;
    slot->refs = 1;
    slot->lastUsed = clock_;
    __sync_synchronize();
    slot->state = SlotState::Queued;
    LightEvent_Signal(&wake_);
    return slot;
}

void ToonCache::release(ToonLook* look) {
    Lock l(lock_);
    for (ToonPart* p : {look->legs, look->torso, look->head, look->lashes}) {
        if (p && p->refs > 0) p->refs--;
    }
    for (ToonClothing* c : look->clothes) {
        if (c && c->refs > 0) c->refs--;
    }
    *look = ToonLook{};
}

bool ToonCache::acquire(const ToonDna& dna, ToonLod lod, ToonLook* out) {
    ToonLook next;
    ToonPartCodes codes;
    partCodes(dna, &codes);
    const char* lodName = lod == ToonLod::High ? "500" : "250";
    char key[24];
    snprintf(key, sizeof(key), "legs_%s_%s", codes.legs, lodName);
    next.legs = bookPart(key, true);
    snprintf(key, sizeof(key), "torso_%s_%s", codes.torso, lodName);
    next.torso = bookPart(key, true);
    snprintf(key, sizeof(key), "head_%s_%s", codes.head, lodName);
    next.head = bookPart(key, codes.headAnimated);
    if (codes.hasLashes) {
        snprintf(key, sizeof(key), "lashes_%s", codes.lashes);
        next.lashes = bookPart(key, false); // optional
    }
    if (!next.valid()) {
        release(&next);
        return false;
    }
    float tints[kDnaTintCount][4];
    tintColors(dna, tints);
    static_assert(kDnaTintCount == render::kTintCount, "tint slot count");
    memcpy(next.tints.rgba, tints, sizeof(tints));
    ClothingTex cloth[kTexSlotCount];
    clothingTextures(dna, cloth);
    for (int s = 1; s < kTexSlotCount; s++) next.clothes[s] = bookClothing(cloth[s].table, cloth[s].index);
    next.scale = bodyScale(dna);
    next.nametagHeight = toonHeight(dna) + 0.5f;
    release(out);
    *out = next;
    if (!loader_) {
        while (loaderStep()) {
        }
    }
    return true;
}

// ---- loader thread: all linear-memory work ----

void ToonCache::loaderMain(void* arg) {
    ToonCache& self = *static_cast<ToonCache*>(arg);
    while (!self.quit_) {
        if (!self.loaderStep()) LightEvent_Wait(&self.wake_);
    }
}

bool ToonCache::loaderStep() {
    ToonPart* part = nullptr;
    ToonClothing* cloth = nullptr;
    for (ToonPart& p : parts_) {
        if (p.state == SlotState::Queued) {
            part = &p;
            break;
        }
    }
    if (!part) {
        for (ToonClothing& c : clothes_) {
            if (c.state == SlotState::Queued) {
                cloth = &c;
                break;
            }
        }
    }
    if (!part && !cloth) return false;
    enforceBudget();
    if (part) loadPart(*part);
    if (cloth) loadClothing(*cloth);
    return true;
}

void ToonCache::enforceBudget() {
    // Free unreferenced, least recently used parts until under budget. The victim is
    // taken out of the table under the lock (so the main thread can't re-acquire it),
    // and its memory is freed afterwards.
    while (linearBytes() > kBudgetBytes) {
        ToonPart* victim = nullptr;
        {
            Lock l(lock_);
            for (ToonPart& p : parts_) {
                if (p.state == SlotState::Ready && p.refs == 0 && (!victim || p.lastUsed < victim->lastUsed)) {
                    victim = &p;
                }
            }
            if (!victim) return; // everything is in use: over budget rather than missing toons
            victim->state = SlotState::Free;
            victim->key[0] = '\0';
        }
        victim->model.unload();
        for (auto& a : victim->anim) a.unload();
        victim->bytes = 0;
    }
}

void ToonCache::loadPart(ToonPart& p) {
    const u64 t0 = svcGetSystemTick();
    // Recycled slot: free whatever it held before.
    p.model.unload();
    for (auto& a : p.anim) a.unload();
    char key[sizeof(p.key)];
    {
        Lock l(lock_);
        memcpy(key, p.key, sizeof(key));
    }
    char file[48], path[128];
    snprintf(file, sizeof(file), "%s.ttm", key);
    bool ok = findFile(file, path, sizeof(path)) && p.model.load(path);
    if (ok && p.animated) {
        for (int a = 0; a < kToonAnimCount; a++) {
            snprintf(file, sizeof(file), "%s_%s.tta", key, kClipNames[a]);
            if (findFile(file, path, sizeof(path))) p.anim[a].load(path);
        }
    }
    u32 bytes = p.model.linearBytes();
    for (const auto& a : p.anim) bytes += a.linearBytes();
    p.bytes = bytes;
    __sync_synchronize(); // model/clip data visible before the state flips
    p.state = ok ? SlotState::Ready : SlotState::Failed;
    if (ok) {
        TT_LOG("toon: loaded %s in %lu ms (%lu KB)", key, static_cast<unsigned long>(msSince(t0)),
               static_cast<unsigned long>(bytes / 1024));
    } else {
        TT_LOG("toon: missing part %s", key);
    }
}

void ToonCache::loadClothing(ToonClothing& c) {
    if (c.tex.data) {
        mem::LinearGuard g;
        C3D_TexDelete(&c.tex); // recycled slot
    }
    c.tex = C3D_Tex{};
    const bool ok = render::loadTexture(clothesFile_, clothesEntries_[c.texIndex], &c.tex);
    __sync_synchronize();
    c.state = ok ? SlotState::Ready : SlotState::Failed;
}

} // namespace tt::world
