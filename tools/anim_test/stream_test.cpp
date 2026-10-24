// Compile the real Anim window reader and ToonCache against an instrumented host GPU/allocator.
#include "world/toon_cache.h"
#include "world/toon_anim.h"
#include "core/memory.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <map>
#include <mutex>
#include <vector>
static std::map<void*,size_t> blocks;
static size_t live=0,peak=0,allocations=0,syncs=0;
static bool gpuBusy=false;
void* linearAlloc(size_t n) {n=(n+127)&~size_t(127);void* p=std::malloc(n);assert(p);blocks[p]=n;live+=n;if(live>peak)peak=live;++allocations;return p;}
void linearFree(void* p) {if(!p)return;assert(!gpuBusy && blocks.count(p));live-=blocks[p];blocks.erase(p);std::free(p);}
void C3D_FrameSync() {gpuBusy=false;++syncs;}
void C3D_TexDelete(C3D_Tex* t) {linearFree(t->data);*t={};}
namespace tt::mem {std::mutex lock;void linearLock(){lock.lock();}void linearUnlock(){lock.unlock();}}
namespace tt::log {void write(const char*,...) {}}
namespace tt::render {
Model::~Model(){unload();}
void Model::unload(){linearFree(vertices_);vertices_=nullptr;numVertices_=linearBytes_=0;}
bool Model::load(const char* path) {
    unload();FILE* f=std::fopen(path,"rb");if(!f)return false;
    unsigned char h[64];assert(std::fread(h,1,64,f)==64);std::memcpy(&numVertices_,h+12,4);
    u16 count=0;u32 table=0;std::memcpy(&count,h+8,2);std::memcpy(&table,h+44,4);
    u32 bytes=numVertices_*16+6;std::fseek(f,table,SEEK_SET);
    for(u16 i=0;i<count;i++){TexEntry e{};assert(std::fread(&e,sizeof(e),1,f)==1);bytes+=e.dataSize;}
    std::fclose(f);vertices_=linearAlloc(bytes);linearBytes_=bytes;return true;
}
bool loadTexture(FILE*,const TexEntry&,C3D_Tex*){return false;}
}
int main() {
    using namespace tt::world;
    ToonCache cache;assert(cache.init());assert(cache.linearBytes()==589824);
    assert(cache.initFallback());const auto& look=cache.fallback();assert(look.ready());
    const size_t initial=allocations;
    int owner=0;ToonCache::Pose pose;
    assert(!cache.sample(&owner,look,kAnimThrow,.25f,false,&pose));
    cache.pump();assert(!cache.sample(&owner,look,kAnimThrow,.25f,false,&pose));
    cache.pump();assert(cache.sample(&owner,look,kAnimThrow,.25f,false,&pose));
    for(int p=0;p<3;p++) {assert(pose.parts[p] && pose.parts[p]->numFrames()==2);const auto* frame=static_cast<const int16_t*>(pose.parts[p]->frame(0));assert(frame[0]==3);}
    int clone=0;ToonCache::Pose shared;
    assert(cache.sample(&clone,look,kAnimThrow,.26f,false,&shared));
    for(int p=0;p<3;p++) assert(shared.parts[p]==pose.parts[p]);
    assert(allocations==initial);
    C3D_Mtx attach;pose.parts[0]->attach(0,1,0,&attach);assert(attach.r[0].w==3);
    // Queue one clip, then replace it before publication: its generation must never draw.
    assert(!cache.sample(&owner,look,kAnimWalk,.25f,true,&pose));cache.pump();
    assert(!cache.sample(&owner,look,kAnimRun,.5f,true,&pose));cache.pump();cache.pump();
    assert(cache.sample(&owner,look,kAnimRun,.5f,true,&pose));
    assert(static_cast<const int16_t*>(pose.parts[0]->frame(0))[0]==6);
    // A missing torso/head Curtsy must retain neutral attachments, not draw at the feet.
    assert(!cache.sample(&owner,look,kAnimCurtsy,.5f,false,&pose));cache.pump();cache.pump();
    assert(cache.sample(&owner,look,kAnimCurtsy,.5f,false,&pose));
    assert(pose.parts[1] && pose.parts[1]->hasAttach());pose.parts[1]->attach(0,1,0,&attach);assert(attach.r[0].w==0);
    for(int i=0;i<200;i++) {
        const float t=(i%31)/60.0f;
        cache.sample(&owner,look,kAnimRun,t,true,&pose);gpuBusy=true;
        cache.pump();cache.pump();cache.sample(&owner,look,kAnimRun,t,true,&pose);
        assert(cache.linearBytes()<=ToonCache::kBudgetBytes);
    }
    assert(allocations==initial); // no frame-time linear allocations
    // Corrupt/truncated windows never escape a read or invalidate the neutral buffers.
    tt::render::Anim bad;assert(bad.reserveWindow(3));
    FILE* f=std::fopen("bad.tta","rb");assert(f && !bad.readWindow(f,0,false,3));std::fclose(f);bad.unload();
    cache.forget(&owner);
    // Hold different, expensive heads referenced until the hard cap refuses admission.
    ToonLook held[32];int denied=0;
    for(int i=4;i<32;i++) {auto dna=defaultToonDna();dna.head=static_cast<uint8_t>(i);cache.acquire(dna,ToonLod::High,&held[i]);cache.pump();if(!held[i].ready())++denied;assert(cache.linearBytes()<=ToonCache::kBudgetBytes);}
    assert(denied>0 && peak<=ToonCache::kBudgetBytes);
    for(auto& heldLook:held)cache.release(&heldLook);
    cache.pump();cache.shutdown();assert(live==0 && blocks.empty() && syncs>200);
    std::printf("PASS real stream/cache: atomic generations, coherent vertex/attach pairs, sparse fallback, no frame allocations, cap admission; peak=%zu bytes\n",peak);
}
