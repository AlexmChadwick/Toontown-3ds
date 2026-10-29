#include "world/asset_loader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"

namespace tt::world {

namespace {

enum class SlotState : u8 { Free, Queued, Loading, Ready, Failed, Cancelled };
enum class Kind : u8 { Model, Anim, Blob };

struct Slot {
    SlotState state = SlotState::Free;
    Kind kind = Kind::Model;
    u16 gen = 1;
    u32 seq = 0; // request order (oldest first)
    char path[112] = {};
    render::Model model;
    render::Anim anim;
    LoadedBlob blob;
    u32 maxBytes = 0;
};

Slot gSlots[AssetLoader::kSlots];
LightLock gLock;
LightEvent gWake;
Thread gThread = nullptr;
volatile bool gQuit = false;
bool gInit = false;
u32 gSeq = 0;

LoadTicket makeTicket(int index) { return (static_cast<u32>(gSlots[index].gen) << 8) | static_cast<u32>(index + 1); }

Slot* slotFor(LoadTicket t) {
    const int idx = static_cast<int>(t & 0xFF) - 1;
    if (t == kNoTicket || idx < 0 || idx >= AssetLoader::kSlots) return nullptr;
    Slot& s = gSlots[idx];
    if (s.gen != (t >> 8) || s.state == SlotState::Free || s.state == SlotState::Cancelled) return nullptr;
    return &s;
}

void freeSlot(Slot& s) { // lock held; staging already empty
    s.state = SlotState::Free;
    if (++s.gen == 0) s.gen = 1;
}

// One queued job; false when none. The load runs without the lock.
bool step() {
    Slot* job = nullptr;
    LightLock_Lock(&gLock);
    for (Slot& s : gSlots) {
        if (s.state == SlotState::Queued && (!job || static_cast<s32>(s.seq - job->seq) < 0)) job = &s;
    }
    if (job) job->state = SlotState::Loading;
    LightLock_Unlock(&gLock);
    if (!job) return false;

    bool ok = false;
    if (job->kind == Kind::Model) ok = job->model.load(job->path);
    else if (job->kind == Kind::Anim) ok = job->anim.load(job->path);
    else {
        FILE* f = std::fopen(job->path, "rb");
        long size = -1;
        if (f && std::fseek(f, 0, SEEK_END) == 0) size = std::ftell(f);
        if (size > 0 && static_cast<unsigned long>(size) <= job->maxBytes && std::fseek(f, 0, SEEK_SET) == 0) {
            job->blob.data = std::malloc(static_cast<size_t>(size));
            if (job->blob.data && std::fread(job->blob.data, 1, static_cast<size_t>(size), f) == static_cast<size_t>(size)) {
                job->blob.size = static_cast<u32>(size);
                ok = true;
            } else {
                std::free(job->blob.data);
                job->blob = {};
            }
        }
        if (f) std::fclose(f);
    }

    LightLock_Lock(&gLock);
    if (job->state == SlotState::Cancelled) { // nobody wants it any more
        LightLock_Unlock(&gLock);
        job->model.unload(); // takes the linear guard itself
        job->anim.unload();
        std::free(job->blob.data);
        job->blob = {};
        LightLock_Lock(&gLock);
        freeSlot(*job);
    } else {
        job->state = ok ? SlotState::Ready : SlotState::Failed;
    }
    LightLock_Unlock(&gLock);
    return true;
}

void loaderMain(void*) {
    while (!gQuit) {
        if (!step()) LightEvent_Wait(&gWake);
    }
}

LoadTicket request(Kind kind, const char* path, u32 maxBytes = 0) {
    if (!gInit) AssetLoader::init();
    LoadTicket t = kNoTicket;
    LightLock_Lock(&gLock);
    for (int i = 0; i < AssetLoader::kSlots && t == kNoTicket; i++) {
        Slot& s = gSlots[i];
        if (s.state != SlotState::Free) continue;
        s.kind = kind;
        s.maxBytes = maxBytes;
        snprintf(s.path, sizeof(s.path), "%s", path);
        s.seq = ++gSeq;
        s.state = SlotState::Queued;
        t = makeTicket(i);
    }
    LightLock_Unlock(&gLock);
    if (t != kNoTicket) LightEvent_Signal(&gWake);
    return t;
}

} // namespace

void AssetLoader::init() {
    if (gInit) return;
    gInit = true;
    gQuit = false;
    LightLock_Init(&gLock);
    LightEvent_Init(&gWake, RESET_ONESHOT);
    // Main thread runs at 0x30; 0x31 only runs while it waits (vsync/GPU), so loading never
    // takes frame time. Core 0 = the Old 3DS application core.
    gThread = threadCreate(loaderMain, nullptr, 16 * 1024, 0x31, 0, false);
    if (!gThread) TT_LOG("assets: loader thread failed");
}

void AssetLoader::shutdown() {
    if (!gInit) return;
    if (gThread) {
        gQuit = true;
        LightEvent_Signal(&gWake);
        threadJoin(gThread, U64_MAX);
        threadFree(gThread);
        gThread = nullptr;
    }
    for (Slot& s : gSlots) {
        s.model.unload();
        s.anim.unload();
        std::free(s.blob.data);
        s.blob = {};
        s.state = SlotState::Free;
    }
    gInit = false;
}

LoadTicket AssetLoader::requestModel(const char* path) { return request(Kind::Model, path); }
LoadTicket AssetLoader::requestAnim(const char* path) { return request(Kind::Anim, path); }

LoadTicket AssetLoader::requestBlob(const char* path, u32 maxBytes) {
    return request(Kind::Blob, path, maxBytes);
}

LoadStatus AssetLoader::poll(LoadTicket t) {
    LightLock_Lock(&gLock);
    const Slot* s = slotFor(t);
    LoadStatus r = LoadStatus::Invalid;
    if (s) {
        r = s->state == SlotState::Ready ? LoadStatus::Ready
            : s->state == SlotState::Failed ? LoadStatus::Failed
                                            : LoadStatus::Pending;
    }
    LightLock_Unlock(&gLock);
    return r;
}

bool AssetLoader::takeModel(LoadTicket t, render::Model& dst) {
    LightLock_Lock(&gLock);
    Slot* s = slotFor(t);
    const bool ok = s && s->kind == Kind::Model && s->state == SlotState::Ready;
    if (ok) {
        dst.swap(s->model); // no allocation, no GPU work
        freeSlot(*s);
    }
    LightLock_Unlock(&gLock);
    return ok;
}

bool AssetLoader::takeAnim(LoadTicket t, render::Anim& dst) {
    LightLock_Lock(&gLock);
    Slot* s = slotFor(t);
    const bool ok = s && s->kind == Kind::Anim && s->state == SlotState::Ready;
    if (ok) {
        dst.swap(s->anim);
        freeSlot(*s);
    }
    LightLock_Unlock(&gLock);
    return ok;
}

bool AssetLoader::takeBlob(LoadTicket t, LoadedBlob& blob) {
    LightLock_Lock(&gLock);
    Slot* s = slotFor(t);
    const bool ok = s && s->kind == Kind::Blob && s->state == SlotState::Ready;
    if (ok) {
        blob = s->blob;
        s->blob = {};
        freeSlot(*s);
    }
    LightLock_Unlock(&gLock);
    return ok;
}

void AssetLoader::cancel(LoadTicket t) {
    LightLock_Lock(&gLock);
    Slot* s = slotFor(t);
    if (s) {
        if (s->state == SlotState::Loading) {
            s->state = SlotState::Cancelled; // the loader frees it
        } else if (s->state == SlotState::Queued || s->state == SlotState::Failed) {
            freeSlot(*s);
        } else if (s->state == SlotState::Ready) {
            LightLock_Unlock(&gLock);
            s->model.unload(); // staging only: never drawn, safe to free now
            s->anim.unload();
            std::free(s->blob.data);
            s->blob = {};
            LightLock_Lock(&gLock);
            freeSlot(*s);
        }
    }
    LightLock_Unlock(&gLock);
}

int AssetLoader::pending() {
    int n = 0;
    LightLock_Lock(&gLock);
    for (const Slot& s : gSlots) n += s.state == SlotState::Queued || s.state == SlotState::Loading;
    LightLock_Unlock(&gLock);
    return n;
}

} // namespace tt::world
