#include "world/toon_cache.h"

#include "core/crash.h"

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
    frame_ = 1;
    for (auto& stream : streams_) for (int p=0;p<3;p++) {
        if (!stream.active[p].reserveWindow(kStreamVertices) || !stream.staging[p].reserveWindow(kStreamVertices)) {
            for(auto& allocated:streams_) for(int i=0;i<3;i++) { allocated.active[i].unload();allocated.staging[i].unload(); }
            streamBytes_=0;return false;
        }
        streamBytes_ += kStreamVertices*32;
    }

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
    C3D_FrameSync();
    for (auto& stream : streams_) for (int p=0;p<3;p++) {
        if(stream.files[p]) { fclose(stream.files[p]);stream.files[p]=nullptr; }
        stream.active[p].unload(); stream.staging[p].unload();
    }
    streamBytes_ = 0;
    for (ToonPart& p : parts_) {
        p.model.unload();
        p.neutral.unload();
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
    Lock lock(lock_);
    u32 total = streamBytes_;
    for (const auto& p : parts_) total += p.bytes;
    for (const auto& c : clothes_) total += c.bytes;
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
            if (p.refs == 0 && p.retiredFrame < frame_ && p.state != SlotState::Queued && (!slot || p.lastUsed < slot->lastUsed)) slot = &p;
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
            if (c.refs == 0 && c.retiredFrame < frame_ && c.state != SlotState::Queued && (!slot || c.lastUsed < slot->lastUsed)) slot = &c;
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

void ToonCache::pin(const ToonLook* look) {
    Lock l(lock_);
    for (ToonPart& p : parts_) p.pinned = false;
    for (ToonClothing& c : clothes_) c.pinned = false;
    if (!look) return;
    for (ToonPart* p : {look->legs, look->torso, look->head, look->lashes}) {
        if (p) p->pinned = true;
    }
    for (ToonClothing* c : look->clothes) {
        if (c) c->pinned = true;
    }
}

void ToonCache::release(ToonLook* look) {
    Lock l(lock_);
    for (ToonPart* p : {look->legs, look->torso, look->head, look->lashes}) {
        if (p && p->refs > 0 && --p->refs==0) p->retiredFrame=frame_;
    }
    for (ToonClothing* c : look->clothes) {
        if (c && c->refs > 0 && --c->refs==0) c->retiredFrame=frame_;
    }
    *look = ToonLook{};
}

bool ToonCache::acquire(const ToonDna& dna, ToonLod lod, ToonLook* out) {
    ToonLook next;
    next.cache = this;
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
    tt::crash::installForThisThread(); // a fault here shows the crash screen, not a silent exit
    ToonCache& self = *static_cast<ToonCache*>(arg);
    while (!self.quit_) {
        if (!self.loaderStep()) LightEvent_Wait(&self.wake_);
    }
}

bool ToonCache::loaderStep() {
    ToonPart* part = nullptr;
    ToonClothing* cloth = nullptr;
    {
    Lock lock(lock_);
    for (ToonPart& p : parts_) {
        if (p.state == SlotState::Queued && p.retiredFrame < frame_) {
            part = &p;
            break;
        }
    }
    if (!part) {
        for (ToonClothing& c : clothes_) {
            if (c.state == SlotState::Queued && c.retiredFrame < frame_) {
                cloth = &c;
                break;
            }
        }
    }
    }
    if (!part && !cloth) return streamStep();
    if (part) loadPart(*part);
    if (cloth) loadClothing(*cloth);
    return true;
}

bool ToonCache::admit(u32 bytes) {
    while (linearBytes()+bytes > kBudgetBytes) {
        ToonPart* victim = nullptr;
        ToonClothing* cloth = nullptr;
        {
            Lock lock(lock_);
            for (auto& p : parts_) if (p.state==SlotState::Ready && !p.refs && !p.pinned && p.retiredFrame<frame_ &&
                (!victim || p.lastUsed<victim->lastUsed)) victim=&p;
            if (victim) { victim->state=SlotState::Free; victim->key[0]=0; }
            else for (auto& c : clothes_) if (c.state==SlotState::Ready && !c.refs && !c.pinned && c.retiredFrame<frame_ &&
                (!cloth || c.lastUsed<cloth->lastUsed)) cloth=&c;
            if (cloth) cloth->state=SlotState::Free;
        }
        if (victim) {
            victim->model.unload(); victim->neutral.unload();
            Lock lock(lock_); victim->bytes=0;
        } else if (cloth) {
            { mem::LinearGuard guard; C3D_TexDelete(&cloth->tex); }
            Lock lock(lock_); cloth->bytes=0;
        } else { TT_LOG("toon: admission denied (%lu + %lu bytes)",static_cast<unsigned long>(linearBytes()),static_cast<unsigned long>(bytes)); return false; }
    }
    return true;
}

namespace {
u32 alignLinear(u32 bytes) { return (bytes+127)&~127u; }
u32 textureCost(const render::TexEntry& entry) {
    if (entry.width<8 || entry.height<8 || entry.width>256 || entry.height>256 ||
        (entry.width&(entry.width-1)) || (entry.height&(entry.height-1)) ||
        !entry.mipLevels || entry.mipLevels>6 || entry.format>13) return 0;
    const u32 bits[14]={32,24,16,16,16,16,16,8,8,8,4,4,4,8};
    u32 expected=0,w=entry.width,hgt=entry.height;
    for(u32 m=0;m<entry.mipLevels;m++) {
        if(w<8 || hgt<8) return 0;
        expected+=w*hgt*bits[entry.format]/8;w/=2;hgt/=2;
    }
    return entry.dataSize==expected?alignLinear(expected):0;
}
// Preflight the exact baked buffer sizes, including every mip, before allocating.
u32 partCost(const char* path,bool animated) {
    FILE* f=fopen(path,"rb");
    u8 h[64];
    if (!f || fread(h,1,64,f)!=64 || memcmp(h,"TTM1",4)) { if (f) fclose(f); return 0; }
    u16 textures=0; u32 nv=0,ni=0,table=0;
    memcpy(&textures,h+8,2); memcpy(&nv,h+12,4); memcpy(&ni,h+16,4); memcpy(&table,h+44,4);
    if (!nv || nv>512 || ni>65535 || textures>64 || fseek(f,table,SEEK_SET)) { fclose(f); return 0; }
    u32 cost=alignLinear(nv*16)+alignLinear(ni*2)+(animated?alignLinear(nv*16):0);
    for (u16 i=0;i<textures;i++) {
        render::TexEntry entry{};
        if (fread(&entry,sizeof(entry),1,f)!=1 || entry.dataSize>ToonCache::kBudgetBytes) { fclose(f); return 0; }
        if (!textureCost(entry)) { fclose(f);return 0; }
        cost+=alignLinear(entry.dataSize);
    }
    fclose(f); return cost;
}
}

void ToonCache::loadPart(ToonPart& p) {
    const u64 t0 = svcGetSystemTick();
    // Recycled slot: free whatever it held before.
    p.model.unload();
    p.neutral.unload();
    char key[sizeof(p.key)];
    {
        Lock l(lock_);
        memcpy(key, p.key, sizeof(key));
    }
    char file[48], path[128];
    snprintf(file, sizeof(file), "%s.ttm", key);
    { Lock lock(lock_); p.bytes=0; }
    bool ok = findFile(file,path,sizeof(path));
    const u32 cost=ok?partCost(path,p.animated):0;
    ok = cost && admit(cost) && p.model.load(path);
    if (ok && p.animated) {
        snprintf(file,sizeof(file),"%s_neutral.tta",key);
        FILE* f=findFile(file,path,sizeof(path))?fopen(path,"rb"):nullptr;
        ok=p.neutral.reserveWindow(p.model.numVertices()) && p.neutral.readWindow(f,0,false,p.model.numVertices());
        if (f) fclose(f);
    }
    if (!ok) { p.model.unload(); p.neutral.unload(); }
    const u32 bytes=ok?cost:0;
    { Lock lock(lock_); p.bytes=bytes; }
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
    { Lock lock(lock_); c.bytes=0; }
    const u32 cost=textureCost(clothesEntries_[c.texIndex]);
    const bool ok = cost && admit(cost) && render::loadTexture(clothesFile_, clothesEntries_[c.texIndex], &c.tex);
    { Lock lock(lock_); c.bytes=ok?cost:0; }
    __sync_synchronize();
    c.state = ok ? SlotState::Ready : SlotState::Failed;
}

void ToonCache::pump() {
    // The caller's C3D_FrameBegin(SYNCDRAW) fenced the previous frame's GPU work: all frees,
    // recycling and staging publication follow it.
    {
    Lock lock(lock_);
    ++frame_;
    for (auto& s : streams_) if (s.state==StreamState::Staged) {
        if (s.loadedGeneration==s.generation) {
            for (int p=0;p<3;p++) { s.active[p].swap(s.staging[p]); s.activeFallback[p]=s.stagedFallback[p]; }
            s.valid=true;
            s.shownTime=s.time; s.shownLoop=s.loop;
            s.state=StreamState::Idle;
        } else s.state=s.owner?StreamState::Queued:StreamState::Idle;
    }
    LightEvent_Signal(&wake_);
    }
    if(!loader_) while(loaderStep()) {}
}

void ToonCache::forget(const void* owner) {
    Lock lock(lock_);
    for (auto& s : streams_) if (s.owner==owner) {
        s.owner=nullptr; ++s.generation; s.valid=false;
        if(s.state!=StreamState::Loading) s.state=StreamState::Idle;
    }
}

bool ToonCache::sample(const void* owner,const ToonLook& look,int clip,float time,bool loop,Pose* pose) {
    if (!owner || clip<0 || clip>=kToonAnimCount) return false;
    Lock lock(lock_);
    // Reuse an already published sample for equal part keys and source frames.
    // Publication/reuse is fenced by pump(), so another owner cannot overwrite it
    // during this GPU frame. Different blends can still share the same two keys.
    const ToonPart* requested[3]={look.legs,look.torso,look.head};
    for (auto& shared:streams_) if (shared.valid && shared.clip==clip) {
        bool match=true;
        for(int p=0;p<3;p++) {
            match=match && !strcmp(shared.key[p],requested[p]->key);
            if(shared.animated[p] && !shared.activeFallback[p])
                match=match && shared.active[p].windowMatches(time,loop,nullptr);
        }
        if(!match) continue;
        shared.usedFrame=frame_;
        for(int p=0;p<3;p++) {
            pose->parts[p]=shared.active[p].loaded()?&shared.active[p]:nullptr;
            pose->blend[p]=0;
            if(!shared.activeFallback[p]) shared.active[p].windowMatches(time,loop,&pose->blend[p]);
        }
        return true;
    }
    Stream* slot=nullptr;
    for (auto& s : streams_) if (s.owner==owner) { slot=&s; break; }
    if (!slot) for (auto& s : streams_) if (s.state==StreamState::Idle &&
        (!s.owner || s.usedFrame+2<frame_) && (!slot || s.usedFrame<slot->usedFrame)) slot=&s;
    if (!slot) return false;
    auto& s=*slot;
    const ToonPart* parts[3]={look.legs,look.torso,look.head};
    bool changed=s.owner!=owner || s.clip!=clip;
    for (int p=0;p<3;p++) changed=changed || strcmp(s.key[p],parts[p]->key)!=0;
    if (changed) {
        ++s.generation; s.valid=false; s.owner=owner; s.clip=clip; s.time=time; s.loop=loop;
        for (int p=0;p<3;p++) { snprintf(s.key[p],sizeof(s.key[p]),"%s",parts[p]->key); s.vertices[p]=parts[p]->model.numVertices(); s.animated[p]=parts[p]->animated; }
    }
    s.usedFrame=frame_;
    bool matches=s.valid;
    for (int p=0;p<3;p++) if (s.animated[p] && !s.activeFallback[p])
        matches=matches && s.active[p].windowMatches(time,loop,nullptr);
    if (!matches || changed) {
        if (s.state==StreamState::Idle) { s.time=time; s.loop=loop; s.state=StreamState::Queued; LightEvent_Signal(&wake_); }
    }
    if (!s.valid) return false;
    for (int p=0;p<3;p++) {
        pose->parts[p]=s.active[p].loaded()?&s.active[p]:nullptr;
        pose->blend[p]=0;
        if (!s.activeFallback[p]) s.active[p].windowMatches(matches?time:s.shownTime,matches?loop:s.shownLoop,&pose->blend[p]);
    }
    return true;
}

bool ToonCache::streamStep() {
    Stream* slot=nullptr;
    char keys[3][24]; u32 vertices[3]; bool animated[3];
    float time=0; bool loop=false; int clip=0; u32 generation=0;
    {
        Lock lock(lock_);
        for (auto& s : streams_) if (s.state==StreamState::Queued) { slot=&s; break; }
        if (!slot) return false;
        slot->state=StreamState::Loading;
        memcpy(keys,slot->key,sizeof(keys)); memcpy(vertices,slot->vertices,sizeof(vertices));
        memcpy(animated,slot->animated,sizeof(animated));
        time=slot->time; loop=slot->loop; clip=slot->clip; generation=slot->generation;
    }
    bool fallback[3]={};
    for (int p=0;p<3;p++) {
        slot->staging[p].clearWindow();
        if (!animated[p]) continue;
        char file[72],path[160];
        snprintf(file,sizeof(file),"%.23s_%.31s.tta",keys[p],kToonAnimNames[clip]);
        if(strcmp(file,slot->fileNames[p])) {
            if(slot->files[p]) fclose(slot->files[p]);
            snprintf(slot->fileNames[p],sizeof(slot->fileNames[p]),"%s",file);
            slot->files[p]=findFile(file,path,sizeof(path))?fopen(path,"rb"):nullptr;
            slot->fileFallback[p]=false;
        }
        bool ok=!slot->fileFallback[p] && slot->staging[p].readWindow(slot->files[p],time,loop,vertices[p],
            toonDuration(static_cast<ToonAnim>(clip),keys[p]));
        if (!ok) {
            fallback[p]=true;
            if(!slot->fileFallback[p]) {
                // Once per clip file: the part shows its neutral pose instead (emotes look frozen).
                TT_LOG("toon: clip %s %s, using neutral", file, slot->files[p] ? "read failed" : "missing");
                if(slot->files[p]) fclose(slot->files[p]);
                snprintf(file,sizeof(file),"%.23s_neutral.tta",keys[p]);
                slot->files[p]=findFile(file,path,sizeof(path))?fopen(path,"rb"):nullptr;
                slot->fileFallback[p]=true;
            }
            slot->staging[p].readWindow(slot->files[p],0,false,vertices[p]);
        }
    }
    { Lock lock(lock_); memcpy(slot->stagedFallback,fallback,sizeof(fallback)); slot->loadedGeneration=generation; slot->state=StreamState::Staged; }
    return true;
}

} // namespace tt::world
