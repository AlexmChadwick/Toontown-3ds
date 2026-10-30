#include "world/zone.h"

#include "core/crash.h"
#include "core/load_profile.h"
#include "render/quality.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#ifdef __3DS__
#include "core/gpu_sync.h"
#include "render/renderer.h"
#endif

namespace tt::world {
namespace {
struct Header {
    char magic[4];
    uint16_t version, flags;
    uint32_t branch, chunks, vis, floors, triggers;
    uint32_t chunkOff, visOff, floorOff, triggerOff, collisionOff, collisionSize, size;
    uint32_t poolOff, poolSize; // v3 shared texture pool (zero and reserved in v1/v2)
};
struct Chunk {
    uint32_t zone, firstVis, numVis, offset, size, geom, tex, draws, tris;
    float min[3], max[3];
    uint32_t firstFloor, numFloors;
    char name[12];
    uint32_t lowOffset, lowSize, lowGeom, lowTex, lowDraws, lowTris;
};
struct Floor { float p[9]; uint16_t cell, zone; };
static_assert(sizeof(Header) == 64 && sizeof(Chunk) == 104 && sizeof(Floor) == 40, "TZ layout");
static_assert(sizeof(Zone::Trigger) == 156, "TZ trigger layout");
bool span(uint32_t off, uint32_t count, uint32_t stride, uint32_t size) {
    return off >= 64 && off <= size && count <= (size - off) / stride;
}
bool section(uint32_t off, uint32_t count, uint32_t stride, uint32_t size) {
    return off % 128 == 0 && span(off, count, stride, size);
}
bool readAt(FILE* f, uint32_t off, void* dst, size_t bytes) {
    return fseek(f, static_cast<long>(off), SEEK_SET) == 0 && fread(dst, 1, bytes, f) == bytes;
}
bool bounds(const float* lo, const float* hi) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(lo[i]) || !std::isfinite(hi[i]) || lo[i] > hi[i]) return false;
    return true;
}
Chunk modelInfo(const Chunk& chunk, uint8_t lod) {
    Chunk c=chunk;
    if (lod==1) {
        c.offset=c.lowOffset; c.size=c.lowSize; c.geom=c.lowGeom;
        c.tex=c.lowTex; c.draws=c.lowDraws; c.tris=c.lowTris;
    }
    return c;
}
float distance2(const Vec3& p, const Chunk& c) {
    const float v[3]={p.x,p.y,p.z};
    float result=0;
    for (int a=0;a<3;++a) {
        const float d=std::max(std::max(c.min[a]-v[a],v[a]-c.max[a]),0.0f);
        result+=d*d;
    }
    return result;
}
// Validate every field trusted by Model::load before permitting GPU allocation. In v3 a
// texture's data may live in the zone's shared pool instead of the blob: its blob-relative
// offset then wraps (u32) to the pool, exactly as Model::loadFrom rebases it.
bool validModel(FILE* f, const Chunk& c, uint32_t poolOff, uint32_t poolSize) {
    struct ModelHeader {
        char magic[4]; uint16_t version, flags, textures, batches;
        uint32_t vertices, indices; float scale[3], offset[3];
        uint32_t texOff, batchOff, vertexOff, indexOff, reserved;
    } h;
    static_assert(sizeof(h) == 64, "TTM header");
    if (c.size < 64 || !readAt(f, c.offset, &h, sizeof(h)) || memcmp(h.magic,"TTM1",4) ||
        h.version != 1 || (h.flags & ~uint16_t{1}) || h.batches != c.draws || h.indices % 3 || h.indices / 3 != c.tris ||
        static_cast<uint64_t>(h.vertices)*16 + static_cast<uint64_t>(h.indices)*2 != c.geom) return false;
    for (int i=0;i<3;++i)
        if (!std::isfinite(h.scale[i]) || h.scale[i]<=0 || !std::isfinite(h.offset[i])) return false;
    if (!span(h.texOff,h.textures,48,c.size) || !span(h.batchOff,h.batches,24,c.size) ||
        !span(h.vertexOff,h.vertices,16,c.size) || !span(h.indexOff,h.indices,2,c.size)) return false;
    struct Tex { uint16_t w, h; uint8_t fmt, mips, ws, wt; uint32_t off, size; char name[32]; };
    uint64_t total=0;
    // Records are read 16 at a time: one seek + read each was an SD round trip per record.
    static_assert(sizeof(Tex) == 48, "TTM texture record");
    Tex texBlock[16];
    for (uint32_t i=0;i<h.textures;++i) {
        if (i%16==0 && !readAt(f,c.offset+h.texOff+i*48,texBlock,std::min<uint32_t>(16,h.textures-i)*48)) return false;
        const Tex& t=texBlock[i%16];
        if (t.w<8 || t.h<8 || t.w>256 || t.h>256 ||
            (t.w & (t.w-1)) || (t.h & (t.h-1)) || !t.mips || t.mips>6 ||
            (t.ws!=0 && t.ws!=2) || (t.wt!=0 && t.wt!=2)) return false;
        const uint32_t at=c.offset+t.off;
        const bool pooled=poolSize && at>=poolOff && t.size<=poolSize && at-poolOff<=poolSize-t.size;
        if (!pooled && !span(t.off,t.size,1,c.size)) return false;
        uint32_t bpp=0;
        switch(t.fmt) {
            case 0: bpp=32; break;
            case 2: case 3: case 4: case 5: bpp=16; break;
            case 7: case 8: case 9: case 13: bpp=8; break;
            case 12: bpp=4; break;
            default: return false;
        }
        uint32_t expected=0;
        for (uint32_t m=0;m<t.mips;++m) {
            if ((t.w>>m)<8 || (t.h>>m)<8) return false;
            expected+=(t.w>>m)*(t.h>>m)*bpp/8;
        }
        if (expected!=t.size) return false;
        total+=t.size;
    }
    if (total!=c.tex) return false;
    struct Batch { uint16_t texture; uint8_t blend,flags,tint,layer,slot,pad; uint32_t fv,nv,fi,ni; };
    uint16_t ids[512];
    static_assert(sizeof(Batch) == 24, "TTM batch record");
    Batch batchBlock[16];
    for (uint32_t i=0;i<h.batches;++i) {
        if (i%16==0 && !readAt(f,c.offset+h.batchOff+i*24,batchBlock,std::min<uint32_t>(16,h.batches-i)*24)) return false;
        const Batch& b=batchBlock[i%16];
        if ((b.texture!=65535 && b.texture>=h.textures) || b.blend>2 || b.flags>1 || b.tint ||
            b.layer>4 || b.slot || b.pad || b.fv>h.vertices || b.nv>h.vertices-b.fv ||
            b.fi>h.indices || b.ni>h.indices-b.fi || b.ni%3) return false;
        // Model::loadFrom checks indices in memory; only scan here when it cannot.
        // Header flags bit0: indices are already absolute (baked that way).
        const bool absolute = (h.flags & 1) != 0;
        for (uint32_t j=0;h.vertices>0x10000 && j<b.ni;) {
            const uint32_t n=std::min(b.ni-j,static_cast<uint32_t>(512));
            if (!readAt(f,c.offset+h.indexOff+(b.fi+j)*2,ids,n*2)) return false;
            for (uint32_t k=0;k<n;++k) {
                if (absolute) {
                    if (ids[k] < b.fv || ids[k] >= b.fv + b.nv) return false;
                } else if (ids[k] >= b.nv) return false;
            }
            j+=n;
        }
    }
    return true;
}
} // namespace


// Uniform XY grid over the floor triangles, so cellAt() (run every frame by update()) tests
// the few triangles that can contain the point instead of every floor of the zone. Each
// grid cell lists the floors whose padded XY bounding box touches it, in ascending floor
// index, so the first-best tie-break of the full scan is unchanged.
struct FloorGrid {
    float ox = 0, oy = 0, inv = 0;
    int w = 0, h = 0;
    uint32_t* start = nullptr; // w*h+1 offsets into items
    uint32_t* items = nullptr;
};
constexpr float kGridMinCell = 8.0f;
constexpr int kGridMaxDim = 64;
constexpr uint32_t kGridMaxItems = 1u << 20; // 4 MB; beyond this the full scan is used

// The barycentric test accepts a point up to 1e-5 of a triangle's size outside it.
void floorBounds(const Floor& f, float* x0, float* y0, float* x1, float* y1) {
    float lx=f.p[0],hx=f.p[0],ly=f.p[1],hy=f.p[1];
    for (int i=1;i<3;++i) {
        lx=std::min(lx,f.p[3*i]); hx=std::max(hx,f.p[3*i]);
        ly=std::min(ly,f.p[3*i+1]); hy=std::max(hy,f.p[3*i+1]);
    }
    const float pad=1e-4f*std::max(hx-lx,hy-ly)+0.01f;
    *x0=lx-pad; *y0=ly-pad; *x1=hx+pad; *y1=hy+pad;
}
void gridRange(const FloorGrid& g,float x0,float y0,float x1,float y1,int* cx0,int* cy0,int* cx1,int* cy1) {
    auto clampi=[](int v,int hi) { return v<0 ? 0 : (v>hi ? hi : v); };
    *cx0=clampi(static_cast<int>((x0-g.ox)*g.inv),g.w-1); *cx1=clampi(static_cast<int>((x1-g.ox)*g.inv),g.w-1);
    *cy0=clampi(static_cast<int>((y0-g.oy)*g.inv),g.h-1); *cy1=clampi(static_cast<int>((y1-g.oy)*g.inv),g.h-1);
}
// Same arithmetic as the original full scan; both paths call it.
inline void testFloor(const Floor& f,const Vec3& feet,float reach,float* best,int* result) {
    const float* a=f.p; const float* b=f.p+3; const float* c=f.p+6;
    const float den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1]);
    if (fabsf(den)<1e-8f) return;
    const float u=((b[1]-c[1])*(feet.x-c[0])+(c[0]-b[0])*(feet.y-c[1]))/den;
    const float v=((c[1]-a[1])*(feet.x-c[0])+(a[0]-c[0])*(feet.y-c[1]))/den;
    const float h=u*a[2]+v*b[2]+(1-u-v)*c[2];
    if (u>=-1e-5f && v>=-1e-5f && u+v<=1.00001f && h<=feet.z+reach && h>*best) {
        *best=h; *result=f.cell;
    }
}

// Chunk models are read by a loader thread (core 0, below the main thread's priority,
// like ToonCache) into staging models; the main thread swaps them in from update().
// The open .tz is only read by that thread once load() has returned.
struct Zone::Impl {
    FILE* file = nullptr;
    Header h{};
    Chunk* chunks = nullptr;
    uint16_t* vis = nullptr;
    Floor* floors = nullptr;
    FloorGrid grid;
    Trigger* triggers = nullptr;
    bool resident[kMaxChunks] = {};
    uint8_t lod[kMaxChunks] = {};
    // The current plan: which chunks should be resident, at which LOD, nearest first.
    bool desired[kMaxChunks] = {};
    bool inCone[kMaxChunks] = {}; // desired and inside the view this plan will submit
    uint8_t choice[kMaxChunks] = {};
    bool failed[kMaxChunks] = {}; // load failed for this plan; retried on the next one
    int retryCountdown = 0;       // updates until failed loads are retried anyway
    uint16_t order[kMaxChunks] = {};
    uint32_t numOrder = 0;
    bool planned = false;
    bool extra[kMaxChunks] = {}; // setExtraVisible: chunks shown beyond the cell's PVS
    bool extraDirty = false;
    Vec3 lastPlan{};
    bool haveCamera = false;
    bool plannedCam = false;
    Vec3 camEye{}, camTarget{}, planEye{}, planFwd{};
    int cell = -1;
    uint32_t geom = 0, tex = 0, tris = 0, draws = 0;
    // Retain budget. Defaults to the draw budget so a zone never holds more than the
    // Old 3DS sub-budget unless Place raises it on New 3DS. Hidden chunks and parked
    // high meshes count toward it. tris/draws count what draw() will submit.
    uint32_t retainGeom = kGeometryBudget, retainTex = kTextureBudget;
    uint32_t seenAt[kMaxChunks] = {};
    uint32_t tick = 1;
    bool parked[kMaxChunks] = {};   // high mesh kept while lod 1 is what's displayed
    bool keepHigh[kMaxChunks] = {}; // a far read is in flight; don't drop the high mesh
    bool warm[kMaxChunks] = {};
    uint8_t warmLod[kMaxChunks] = {};
    uint16_t warmOrder[kMaxChunks] = {};
    uint32_t numWarm = 0;
    bool preload = false;
    uint32_t detailReloads = 0, loadsQueued = 0;
    uint64_t inflightGeom = 0, inflightTex = 0;
#ifndef __3DS__
    // Host stand-in for the device job slots. update() queues; completeLoads() publishes.
    struct HostJob {
        uint16_t chunk = 0;
        uint8_t lod = 0;
        bool used = false;
    };
    static constexpr int kHostJobs = 4;
    HostJob hostJobs[kHostJobs] = {};
#endif
    float min[3] = {}, max[3] = {};
#ifdef __3DS__
    enum JobState : uint8_t { Free, Queued, Reading, Uploading, Done, Failed };
    struct Job {
        volatile JobState state = Free;
        uint16_t chunk = 0;
        uint8_t lod = 0;
        render::Model model;
        render::ModelStaging staging; // loader fills the bytes; the main thread uploads
    };
    static constexpr int kJobs = 4;
    render::Model* models = nullptr;
    render::Model* parkedModels = nullptr;
    Job jobs[kJobs];
    LightLock lock;
    LightEvent wake;
    Thread thread = nullptr;
    volatile bool quit = false;
#endif
};

#ifdef __3DS__
namespace {
void loaderMain(void* arg) {
    tt::crash::installForThisThread(); // a fault here shows the crash screen, not a silent exit
    Zone::Impl& z = *static_cast<Zone::Impl*>(arg);
    while (!z.quit) {
        Zone::Impl::Job* job = nullptr;
        LightLock_Lock(&z.lock);
        for (auto& j : z.jobs)
            if (j.state == Zone::Impl::Queued) { job = &j; j.state = Zone::Impl::Reading; break; }
        LightLock_Unlock(&z.lock);
        if (!job) { LightEvent_Wait(&z.wake); continue; }
        const Chunk d = modelInfo(z.chunks[job->chunk], job->lod);
        // Bytes only. GPU upload is sliced on the main thread so one chunk cannot stall a frame.
        const bool ok = job->staging.read(z.file, d.offset);
        LightLock_Lock(&z.lock);
        job->state = ok ? Zone::Impl::Uploading : Zone::Impl::Failed;
        LightLock_Unlock(&z.lock);
    }
}
} // namespace
#endif

namespace {
// Best effort: on any failure the grid stays empty and cellAt() scans every floor.
void buildFloorGrid(Zone::Impl& z) {
    const uint32_t n=z.h.floors;
    if (!n) return;
    float lx=INFINITY,ly=INFINITY,hx=-INFINITY,hy=-INFINITY;
    for (uint32_t i=0;i<n;++i) {
        float a,b,c,d;
        floorBounds(z.floors[i],&a,&b,&c,&d);
        lx=std::min(lx,a); ly=std::min(ly,b); hx=std::max(hx,c); hy=std::max(hy,d);
    }
    const float cell=std::max(kGridMinCell,std::max(hx-lx,hy-ly)/kGridMaxDim);
    FloorGrid g;
    g.ox=lx; g.oy=ly; g.inv=1.0f/cell;
    g.w=static_cast<int>((hx-lx)*g.inv)+1; g.h=static_cast<int>((hy-ly)*g.inv)+1;
    if (g.w<1 || g.h<1 || g.w>kGridMaxDim+1 || g.h>kGridMaxDim+1) return;
    const size_t cells=static_cast<size_t>(g.w)*g.h;
    uint32_t* start=static_cast<uint32_t*>(calloc(cells+1,sizeof(uint32_t)));
    if (!start) return;
    uint64_t total=0;
    for (uint32_t i=0;i<n;++i) {
        float a,b,c,d; int x0,y0,x1,y1;
        floorBounds(z.floors[i],&a,&b,&c,&d);
        gridRange(g,a,b,c,d,&x0,&y0,&x1,&y1);
        for (int y=y0;y<=y1;++y) for (int x=x0;x<=x1;++x) { ++start[static_cast<size_t>(y)*g.w+x+1]; ++total; }
    }
    uint32_t* items=total<=kGridMaxItems ? static_cast<uint32_t*>(malloc(static_cast<size_t>(total ? total : 1)*sizeof(uint32_t))) : nullptr;
    if (!items) { free(start); return; }
    for (size_t i=0;i<cells;++i) start[i+1]+=start[i];
    // Fill with a moving cursor per cell, then restore the offsets (shift back by one).
    for (uint32_t i=0;i<n;++i) {
        float a,b,c,d; int x0,y0,x1,y1;
        floorBounds(z.floors[i],&a,&b,&c,&d);
        gridRange(g,a,b,c,d,&x0,&y0,&x1,&y1);
        for (int y=y0;y<=y1;++y) for (int x=x0;x<=x1;++x) items[start[static_cast<size_t>(y)*g.w+x]++]=i;
    }
    for (size_t i=cells;i>0;--i) start[i]=start[i-1];
    start[0]=0;
    g.start=start; g.items=items;
    z.grid=g;
}
} // namespace

Zone::~Zone() { unload(); }
bool Zone::fail(const char* message) {
    snprintf(error_,sizeof(error_),"%s",message);
    return false;
}
void Zone::unload() {
    if (!impl_) return;
#ifdef __3DS__
    if (impl_->thread) {
        impl_->quit = true;
        LightEvent_Signal(&impl_->wake);
        if (!::tt::gpu::joinBounded(impl_->thread, "zone loader", 2000ULL * 1000 * 1000)) {
            impl_ = nullptr; // still reading chunks: leak the zone rather than free under it
            return;
        }
    }
    ::tt::gpu::frameSync();
    delete[] impl_->parkedModels;
    delete[] impl_->models;
#endif
    collision_.unload();
    if (impl_->file) fclose(impl_->file);
    free(impl_->chunks); free(impl_->vis); free(impl_->floors); free(impl_->triggers);
    free(impl_->grid.start); free(impl_->grid.items);
    delete impl_;
    impl_ = nullptr;
}
bool Zone::load(const char* path) {
    const uint64_t metaT0 = tt::loadprof::nowUs();
    unload();
    error_[0]=0;
    if (!path || !*path) return fail("zone: invalid path");
    impl_ = new(std::nothrow) Impl;
    if (!impl_) return fail("zone: out of heap");
    Impl& z=*impl_;
    z.file=fopen(path,"rb");
    if (z.file) setvbuf(z.file, nullptr, _IOFBF, 128 * 1024);
    bool ok=z.file && readAt(z.file,0,&z.h,sizeof(z.h));
    Header& h=z.h;
    ok=ok && !memcmp(h.magic,"TZ01",4) && h.version>=1 && h.version<=3 && !h.flags &&
       (h.version==3 || (!h.poolOff && !h.poolSize)) &&
       h.branch>0 && h.branch<=65535 && h.chunks>0 && h.chunks<=kMaxChunks &&
       h.vis<=kMaxChunks*kMaxChunks && h.floors<=100000 && h.triggers<=4096 && h.size<=128u*1024*1024;
    const uint32_t stride=h.version>=2 ? 104 : 80;
    if (ok) {
        ok=fseek(z.file,0,SEEK_END)==0 && ftell(z.file)==static_cast<long>(h.size) &&
           section(h.chunkOff,h.chunks,stride,h.size) && section(h.visOff,h.vis,2,h.size) &&
           section(h.floorOff,h.floors,40,h.size) && section(h.triggerOff,h.triggers,156,h.size) &&
           h.collisionSize>=96 && h.collisionSize<=8u*1024*1024 && section(h.collisionOff,h.collisionSize,1,h.size) &&
           (!h.poolSize || section(h.poolOff,h.poolSize,1,h.size));
    }
    if (ok) {
        z.chunks=static_cast<Chunk*>(calloc(h.chunks,sizeof(Chunk)));
        z.vis=static_cast<uint16_t*>(calloc(h.vis ? h.vis : 1,2));
        z.floors=static_cast<Floor*>(calloc(h.floors ? h.floors : 1,40));
        z.triggers=static_cast<Trigger*>(calloc(h.triggers ? h.triggers : 1,156));
        ok=z.chunks && z.vis && z.floors && z.triggers &&
           readAt(z.file,h.visOff,z.vis,h.vis*2) &&
           readAt(z.file,h.floorOff,z.floors,h.floors*40) && readAt(z.file,h.triggerOff,z.triggers,h.triggers*156);
    }
    if (ok && stride==sizeof(Chunk)) ok=readAt(z.file,h.chunkOff,z.chunks,h.chunks*stride); // v2+: one read
    else for (uint32_t i=0;ok && i<h.chunks;++i) {
        ok=readAt(z.file,h.chunkOff+i*stride,&z.chunks[i],stride);
    }
    for (uint32_t i=0;ok && i<h.chunks;++i) {
        const Chunk& c=z.chunks[i];
        ok=c.zone>0 && c.zone<=65535 && c.firstVis<=h.vis && c.numVis<=h.vis-c.firstVis &&
           c.firstFloor<=h.floors && c.numFloors<=h.floors-c.firstFloor && bounds(c.min,c.max) && memchr(c.name,0,12);
        if (!ok) break;
        bool self=false;
        for (uint32_t j=0;j<c.numVis;++j) {
            const uint16_t id=z.vis[c.firstVis+j];
            if (id>=h.chunks || (j && id<=z.vis[c.firstVis+j-1])) { ok=false; break; }
            if (id==i) self=true;
        }
        ok=ok && self;
        for (uint32_t j=0;ok && j<c.numFloors;++j) {
            const Floor& f=z.floors[c.firstFloor+j];
            ok=f.cell==i && f.zone==c.zone;
        }
        if (c.size) ok=ok && section(c.offset,c.size,1,h.size) && validModel(z.file,c,h.poolOff,h.poolSize);
        else ok=ok && !(c.offset || c.geom || c.tex || c.draws || c.tris);
        if (h.version>=2) {
            const Chunk low=modelInfo(c,1);
            if (low.size) ok=ok && section(low.offset,low.size,1,h.size) && validModel(z.file,low,h.poolOff,h.poolSize);
            else ok=ok && !(low.offset || low.geom || low.tex || low.draws || low.tris);
        }
    }
    // Sections must be disjoint, including blobs (prevents aliasing metadata).
    uint32_t starts[kMaxChunks*2+6], ends[kMaxChunks*2+6];
    int ranges=0;
    auto addRange=[&](uint32_t off,uint32_t size) {
        if (size) { starts[ranges]=off; ends[ranges++]=off+size; }
    };
    if (ok) {
        addRange(h.chunkOff,h.chunks*stride); addRange(h.visOff,h.vis*2);
        addRange(h.floorOff,h.floors*40); addRange(h.triggerOff,h.triggers*156);
        addRange(h.collisionOff,h.collisionSize); addRange(h.poolOff,h.poolSize);
        for (uint32_t i=0;i<h.chunks;++i) {
            addRange(z.chunks[i].offset,z.chunks[i].size);
            if (h.version>=2) addRange(z.chunks[i].lowOffset,z.chunks[i].lowSize);
        }
        for (int i=0;i<ranges;++i) for (int j=i+1;j<ranges;++j)
            if (starts[i]<ends[j] && starts[j]<ends[i]) ok=false;
    }
    for (uint32_t i=0;ok && i<h.floors;++i) {
        const Floor& f=z.floors[i];
        ok=f.cell<h.chunks && f.zone==z.chunks[f.cell].zone;
        for (float p:f.p) ok=ok && std::isfinite(p);
    }
    for (uint32_t i=0;ok && i<h.triggers;++i) {
        const Trigger& t=z.triggers[i];
        ok=bounds(t.min,t.max) && t.kind<=2 && memchr(t.name,0,64) && memchr(t.owner,0,64);
    }
    for (uint32_t i=0;ok && i<h.chunks;++i) {
        uint64_t geom=0,tex=0,draws=0,tris=0;
        const Chunk& c=z.chunks[i];
        for (uint32_t j=0;j<c.numVis;++j) {
            const Chunk v=modelInfo(z.chunks[z.vis[c.firstVis+j]],h.version>=2 ? 1 : 0);
            geom+=v.geom; tex+=v.tex; draws+=v.draws; tris+=v.tris;
        }
        ok=geom<=kGeometryBudget && tex<=kTextureBudget && draws<=150 && (h.version==1 || tris<=kTriangleBudget);
    }
    if (ok) {
        ok=collision_.loadFrom(z.file,h.collisionOff,h.collisionSize,"tz collision");
    }
#ifdef __3DS__
    if (ok) {
        z.models=new(std::nothrow) render::Model[h.chunks];
        z.parkedModels=new(std::nothrow) render::Model[h.chunks];
        ok=z.models && z.parkedModels;
    }
#endif
    tt::loadprof::addMeta(static_cast<uint32_t>(tt::loadprof::nowUs() - metaT0));
    tt::loadprof::noteZone(path);
    if (!ok) { unload(); return fail("zone: invalid TZ/model/collision, budget, heap or I/O failure"); }
#ifdef __3DS__
    LightLock_Init(&z.lock);
    LightEvent_Init(&z.wake, RESET_ONESHOT);
    z.thread = threadCreate(loaderMain, &z, 32 * 1024, 0x31, 0, false);
    if (!z.thread) { unload(); return fail("zone: cannot start loader thread"); }
#endif
    buildFloorGrid(z);
    bool firstBounds=true;
    for (uint32_t i=0;i<h.chunks;++i) if (z.chunks[i].size) {
        for (int a=0;a<3;++a) {
            z.min[a]=firstBounds ? z.chunks[i].min[a] : std::min(z.min[a],z.chunks[i].min[a]);
            z.max[a]=firstBounds ? z.chunks[i].max[a] : std::max(z.max[a],z.chunks[i].max[a]);
        }
        firstBounds=false;
    }
    return true;
}

int Zone::cellAt(const Vec3& feet,float reach) const {
    if (!impl_ || !std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z) ||
        !std::isfinite(reach) || reach<0) return -1;
    const FloorGrid& g=impl_->grid;
    if (!g.start) return cellAtReference(feet,reach);
    const float fx=(feet.x-g.ox)*g.inv,fy=(feet.y-g.oy)*g.inv;
    if (!(fx>=0 && fy>=0 && fx<static_cast<float>(g.w) && fy<static_cast<float>(g.h))) return -1; // no padded floor box here
    const size_t at=static_cast<size_t>(static_cast<int>(fy))*g.w+static_cast<size_t>(static_cast<int>(fx));
    float best=-INFINITY;
    int result=-1;
    for (uint32_t k=g.start[at];k<g.start[at+1];++k) testFloor(impl_->floors[g.items[k]],feet,reach,&best,&result);
    return result;
}
int Zone::cellAtReference(const Vec3& feet,float reach) const {
    if (!impl_ || !std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z) ||
        !std::isfinite(reach) || reach<0) return -1;
    float best=-INFINITY;
    int result=-1;
    for (uint32_t i=0;i<impl_->h.floors;++i) testFloor(impl_->floors[i],feet,reach,&best,&result);
    return result;
}
uint16_t Zone::zoneAt(const Vec3& p,float reach) const {
    const int cell=cellAt(p,reach);
    return cell<0 ? 0 : static_cast<uint16_t>(impl_->chunks[cell].zone);
}
// Top-screen vertical FOV is 55 degrees (Renderer::kFovYDeg) at 400x240.
constexpr float kHalfFovV = 55.0f * 0.5f * 3.14159265f / 180.0f;
constexpr float kConeMargin = 4.0f * 3.14159265f / 180.0f;
constexpr float kCamMove2 = 64.0f;          // replan after the eye moves 8 units
constexpr float kCamTurnDot = 0.984807753f; // cos(10 deg)
// Old 3DS linear heap is 24 MB. Non-cone chunks are evicted below this free floor.
constexpr uint32_t kLinearHeadroom = 12u * 1024u * 1024u;
#ifdef __3DS__
// Main-thread GPU upload per update. One slice stays well under a 33 ms frame.
constexpr uint32_t kUploadBudgetUs = 10000;
#endif

void cameraBasis(const Vec3& eye, const Vec3& target, Vec3& fwd, Vec3& right, Vec3& up) {
    fwd = Vec3{target.x - eye.x, target.y - eye.y, target.z - eye.z};
    const float len = sqrtf(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (len < 1e-4f) fwd = Vec3{1, 0, 0};
    else { fwd.x /= len; fwd.y /= len; fwd.z /= len; }
    // right = normalize(cross(worldUp, forward)), worldUp = +Z.
    right = Vec3{-fwd.y, fwd.x, 0};
    const float rl = sqrtf(right.x * right.x + right.y * right.y);
    if (rl < 1e-4f) right = Vec3{1, 0, 0};
    else { right.x /= rl; right.y /= rl; }
    up = Vec3{fwd.y * right.z - fwd.z * right.y,
              fwd.z * right.x - fwd.x * right.z,
              fwd.x * right.y - fwd.y * right.x};
}

// True when the chunk's bounding sphere can meet the view cone. The cone is a few
// degrees wider than the frustum, so a chunk this accepts is the one draw() may submit.
bool inView(const Chunk& c, const Vec3& eye, const Vec3& fwd, const Vec3& right, const Vec3& up, float drawDist) {
    if (distance2(eye, c) > drawDist * drawDist) return false;
    const float cx = (c.min[0] + c.max[0]) * 0.5f;
    const float cy = (c.min[1] + c.max[1]) * 0.5f;
    const float cz = (c.min[2] + c.max[2]) * 0.5f;
    const float dx = c.max[0] - c.min[0], dy = c.max[1] - c.min[1], dz = c.max[2] - c.min[2];
    const float rad = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
    const float vx = cx - eye.x, vy = cy - eye.y, vz = cz - eye.z;
    const float dist = sqrtf(vx * vx + vy * vy + vz * vz);
    if (dist <= rad + 0.5f) return true;
    const float along = vx * fwd.x + vy * fwd.y + vz * fwd.z;
    if (along < -rad) return false;
    const float halfH = atanf(tanf(kHalfFovV) * (400.0f / 240.0f));
    const float extra = asinf(rad / dist > 1.0f ? 1.0f : rad / dist);
    const float denom = along > 1e-3f ? along : 1e-3f;
    const float angH = atan2f(fabsf(vx * right.x + vy * right.y + vz * right.z), denom);
    const float angV = atan2f(fabsf(vx * up.x + vy * up.y + vz * up.z), denom);
    return angH <= halfH + extra + kConeMargin && angV <= kHalfFovV + extra + kConeMargin;
}

// geom/tex are everything resident. tris/draws are what draw() will submit: the cone
// when a camera is set, otherwise every desired resident chunk. In-cone chunks are
// all counted. The 130-draw cap is applied when the plan is built, not by skipping
// a chunk that is already in the cone.
void recountZone(Zone::Impl& z) {
    z.geom = z.tex = 0;
    for (uint32_t i = 0; i < z.h.chunks; ++i) {
        if (z.resident[i]) {
            const Chunk d = modelInfo(z.chunks[i], z.lod[i]);
            z.geom += d.geom;
            z.tex += d.tex;
        }
        if (z.parked[i] && !(z.resident[i] && z.lod[i] == 0)) {
            z.geom += z.chunks[i].geom;
            z.tex += z.chunks[i].tex;
        }
    }
    z.tris = z.draws = 0;
    if (!z.haveCamera) {
        for (uint32_t i = 0; i < z.h.chunks; ++i) if (z.resident[i] && z.desired[i]) {
            const Chunk d = modelInfo(z.chunks[i], z.lod[i]);
            z.tris += d.tris;
            z.draws += d.draws;
        }
        return;
    }
    Vec3 fwd{}, right{}, up{};
    cameraBasis(z.camEye, z.camTarget, fwd, right, up);
    const float dist = tt::render::activeQuality().drawDistance;
    uint32_t usedDraws = 0, usedTris = 0;
    bool seen[Zone::kMaxChunks] = {};
    auto consider = [&](uint32_t i) {
        if (i >= z.h.chunks || seen[i]) return;
        seen[i] = true;
        if (!z.resident[i] || z.parked[i]) return;
        const Chunk mesh = modelInfo(z.chunks[i], z.lod[i]);
        if (!mesh.size || distance2(z.camEye, z.chunks[i]) > dist * dist) return;
        const bool viewed = static_cast<int>(i) == z.cell ||
                            inView(z.chunks[i], z.camEye, fwd, right, up, dist);
        if (!viewed) return;
        usedDraws += mesh.draws;
        usedTris += mesh.tris;
    };
    for (uint32_t j = 0; j < z.numOrder; ++j) consider(z.order[j]);
    for (uint32_t i = 0; i < z.h.chunks; ++i) consider(i);
    z.draws = usedDraws;
    z.tris = usedTris;
}

bool Zone::update(const Vec3& feet,CellChanged callback,void* user,float reach) {
    if (!impl_) return fail("zone: update before load");
    Impl& z=*impl_;
    int next=cellAt(feet,reach);
    if (next<0 && z.cell<0) {
        // Nothing streamed yet and no tagged floor underfoot (e.g. arriving in a tunnel
        // mouth): use the cell whose bounds contain the toon, else the nearest one.
        float bestD=INFINITY;
        for (uint32_t i=0;i<z.h.chunks;++i) {
            const Chunk& c=z.chunks[i];
            if (!c.numVis) continue;
            const float dx=std::max({c.min[0]-feet.x,0.0f,feet.x-c.max[0]});
            const float dy=std::max({c.min[1]-feet.y,0.0f,feet.y-c.max[1]});
            const float d=dx*dx+dy*dy;
            if (d<bestD) { bestD=d; next=static_cast<int>(i); }
        }
    }
    bool replan=next>=0 && (next!=z.cell || !z.planned || z.extraDirty);
    const bool extrasChanged=replan && z.extraDirty;
    if (next>=0 && !replan && z.h.version>=2) {
        const float dx=feet.x-z.lastPlan.x,dy=feet.y-z.lastPlan.y,dz=feet.z-z.lastPlan.z;
        replan=dx*dx+dy*dy+dz*dz>=16.0f; // avoid churn for tiny movements
    }
    Vec3 fwd{}, right{}, up{};
    if (z.haveCamera) cameraBasis(z.camEye, z.camTarget, fwd, right, up);
    if (next>=0 && !replan && z.haveCamera && !z.plannedCam) replan=true;
    if (next>=0 && !replan && z.haveCamera && z.plannedCam) {
        const float dx=z.camEye.x-z.planEye.x, dy=z.camEye.y-z.planEye.y, dz=z.camEye.z-z.planEye.z;
        const float facing=fwd.x*z.planFwd.x+fwd.y*z.planFwd.y+fwd.z*z.planFwd.z;
        replan=dx*dx+dy*dy+dz*dz>=kCamMove2 || facing<kCamTurnDot;
    }
    if (replan) {
        const Chunk& c=z.chunks[next];
        memset(z.desired,0,sizeof(z.desired));
        memset(z.choice,0,sizeof(z.choice));
        memset(z.failed,0,sizeof(z.failed));
        uint64_t geom=0,tex=0;
        uint32_t count=0;
        const tt::render::Quality& quality = tt::render::activeQuality();
        const float draw2 = quality.drawDistance * quality.drawDistance;
        auto want=[&](uint16_t id) {
            if (z.desired[id]) return;
            // The cell underfoot always stays. Chunks past the profile draw distance stay
            // out of linear memory; fog has already hidden them. New 3DS asks for more.
            if (id != static_cast<uint16_t>(next) && distance2(feet, z.chunks[id]) > draw2) return;
            z.desired[id]=true;
            z.choice[id]=z.h.version>=2 ? 1 : 0;
            z.order[count++]=id;
            const Chunk d=modelInfo(z.chunks[id],z.choice[id]);
            geom+=d.geom; tex+=d.tex;
        };
        for (uint32_t j=0;j<c.numVis;++j) want(z.vis[c.firstVis+j]);
        for (uint32_t i=0;i<z.h.chunks;++i) if (z.extra[i]) want(static_cast<uint16_t>(i));
        z.extraDirty=false;
        z.numOrder=count;
        // No camera: nearest the feet, and the draw cap covers the whole cell (host tests).
        // With a camera: the cell underfoot, then in view, then closer, then more central.
        // The draw and triangle caps count only chunks this frame will submit.
        // Memory caps still cover the cell.
        const bool viewCap = z.haveCamera && z.h.version>=2;
        uint8_t seenView[kMaxChunks] = {};
        float rankDot[kMaxChunks] = {};
        float rankDist[kMaxChunks] = {};
        if (viewCap) {
            for (uint32_t j=0;j<count;++j) {
                const uint16_t id=z.order[j];
                const Chunk& chunk=z.chunks[id];
                const float cx=(chunk.min[0]+chunk.max[0])*0.5f;
                const float cy=(chunk.min[1]+chunk.max[1])*0.5f;
                const float cz=(chunk.min[2]+chunk.max[2])*0.5f;
                const float vx=cx-z.camEye.x, vy=cy-z.camEye.y, vz=cz-z.camEye.z;
                const float dist=sqrtf(vx*vx+vy*vy+vz*vz)+1e-3f;
                rankDot[id]=(vx*fwd.x+vy*fwd.y+vz*fwd.z)/dist;
                rankDist[id]=distance2(z.camEye, chunk);
                seenView[id]= id==static_cast<uint16_t>(next) || inView(chunk, z.camEye, fwd, right, up, quality.drawDistance);
            }
        }
        const float must2 = quality.lodMust * quality.lodMust;
        for (uint32_t j=1;j<count;++j) {
            const uint16_t id=z.order[j];
            uint32_t at=j;
            const uint16_t cellId = static_cast<uint16_t>(next);
            auto before=[&](uint16_t a, uint16_t b) {
                // The cell underfoot is drawn first, so a one-frame cap never drops the ground.
                if (a==cellId || b==cellId) return a==cellId && b!=cellId;
                if (!viewCap) return distance2(feet, z.chunks[a]) < distance2(feet, z.chunks[b]);
                if (seenView[a]!=seenView[b]) return seenView[a]>seenView[b];
                // lodMust (near the toon) before anything farther, then closer to the eye.
                const bool am = distance2(feet, z.chunks[a]) <= must2;
                const bool bm = distance2(feet, z.chunks[b]) <= must2;
                if (am!=bm) return am;
                if (rankDist[a]!=rankDist[b]) return rankDist[a]<rankDist[b];
                if (rankDot[a]!=rankDot[b]) return rankDot[a]>rankDot[b];
                return a<b;
            };
            while (at>0 && before(id, z.order[at-1])) {
                z.order[at]=z.order[at-1]; --at;
            }
            z.order[at]=id;
        }
        memset(z.inCone, 0, sizeof(z.inCone));
        for (uint32_t j=0;j<count;++j) {
            const uint16_t id=z.order[j];
            z.inCone[id]= !viewCap || seenView[id];
        }
        if (z.h.version>=2) {
            // Nearer and in view first. Choice starts at the far mesh. A chunk inside
            // the cone is never left without that far mesh: draw() does not skip it.
            // Upgrades to the near mesh stop at 130 draws and 15k triangles, counting
            // what this walk has already kept. A resident near mesh inside lodKeep
            // stays (Toon Hall) even if that runs past the cap. Past lodKeep it stays
            // only while it still fits; otherwise it is demoted and the far mesh
            // replaces it. Outside the cone a resident near mesh stays in memory and
            // is not submitted, and it is not replaced by the far blob.
            const uint64_t geomCap = z.retainGeom < kGeometryBudget ? z.retainGeom : kGeometryBudget;
            const uint64_t texCap = z.retainTex < kTextureBudget ? z.retainTex : kTextureBudget;
            uint64_t viewDraws=0, viewTris=0;
            for (uint32_t j=0;j<count;++j) {
                const uint16_t id=z.order[j];
                const Chunk& high=z.chunks[id];
                const bool inThis=!viewCap || seenView[id];
                const bool nearResident=(z.resident[id] && z.lod[id]==0) || z.parked[id];
                if (!inThis) {
                    if (nearResident) z.choice[id]=0;
                    continue;
                }
                const float keep=nearResident ? quality.lodKeep : quality.lodNear;
                const bool within=distance2(feet, high)<=keep*keep;
                const Chunk low=modelInfo(high, 1);
                const uint64_t g=geom-low.geom+high.geom, t=tex-low.tex+high.tex;
                const uint64_t tr=viewTris+high.tris, dr=viewDraws+high.draws;
                const bool fits=g<=geomCap && t<=texCap && tr<=kTriangleBudget && dr<=kDrawBudget;
                const bool keepNear=nearResident && within;
                if (keepNear || (fits && (within || nearResident))) {
                    z.choice[id]=0;
                    geom=g; tex=t;
                    viewTris=tr; viewDraws=dr;
                } else {
                    viewTris+=low.tris; viewDraws+=low.draws;
                }
            }
        }
        z.lastPlan=feet;
        z.plannedCam=z.haveCamera;
        if (z.haveCamera) { z.planEye=z.camEye; z.planFwd=fwd; }
        z.planned=true;
        error_[0]=0;
        const uint16_t previous=currentZone();
        z.cell=next;
        if (callback && (previous!=currentZone() || extrasChanged)) {
            uint16_t zones[kMaxChunks+1];
            const int count=visibleZones(zones,kMaxChunks+1);
            callback(user,currentZone(),zones,count);
        }
    }
    if (!z.planned) return true;
    if (z.retryCountdown>0 && --z.retryCountdown==0) memset(z.failed,0,sizeof(z.failed));
    ++z.tick;
    for (uint32_t j=0;j<z.numOrder;++j) z.seenAt[z.order[j]]=z.tick;
    for (uint32_t j=0;j<z.numWarm;++j) if (z.warm[z.warmOrder[j]]) z.seenAt[z.warmOrder[j]]=z.tick;
    bool synced=false;
    auto sync=[&]() {
#ifdef __3DS__
        if (!synced) ::tt::gpu::frameSync(); // the last frame may still read what we free
#endif
        synced=true;
    };
    auto recompute=[&]() { recountZone(z); };
    z.inflightGeom=0;
    z.inflightTex=0;
#ifdef __3DS__
    // One slice of GPU upload. The loader thread already read the file in one span.
    {
        uint32_t budget=kUploadBudgetUs;
        for (auto& j:z.jobs) {
            if (j.state!=Impl::Uploading || !budget) continue;
            const uint64_t t0=svcGetSystemTick();
            const auto result=j.staging.pump(j.model, budget);
            const uint32_t used=static_cast<uint32_t>((svcGetSystemTick()-t0)/(SYSCLOCK_ARM11/1000000));
            budget=used>=budget ? 0 : budget-used;
            if (result==render::ModelStaging::Result::Complete) j.state=Impl::Done;
            else if (result==render::ModelStaging::Result::Error) j.state=Impl::Failed;
        }
    }
    for (auto& j:z.jobs) {
        LightLock_Lock(&z.lock);
        const Impl::JobState st=j.state;
        LightLock_Unlock(&z.lock);
        if (st==Impl::Done || st==Impl::Failed) {
            const uint16_t id=j.chunk;
            // A warm-ring chunk is not in the cone, so `desired` is false. Keeping
            // the upload is what lets warming() finish. An in-cone demotion is
            // desired at lod 1 and replaces the near mesh; a far upload over a
            // near mesh we are still keeping is thrown away.
            const bool plan=z.desired[id] && z.choice[id]==j.lod;
            const bool warmKeep=!z.desired[id] && z.warm[id] && z.warmLod[id]==j.lod;
            const bool take=st==Impl::Done && (plan || warmKeep);
            const bool nearHeld=(z.resident[id] && z.lod[id]==0) || z.parked[id];
            if (take) {
                sync();
                if (z.parked[id] && j.lod==0) { z.parkedModels[id].unload(); z.parked[id]=false; }
                z.models[id].swap(j.model);
                z.resident[id]=true; z.lod[id]=j.lod; z.keepHigh[id]=false;
                if (j.lod==0) z.parked[id]=false;
            } else if (st==Impl::Done && j.lod==1 && nearHeld) {
                z.keepHigh[id]=false; // the near mesh stays; this far upload is not the plan
            } else if (st==Impl::Failed && (plan || warmKeep)) {
                z.failed[id]=true; z.keepHigh[id]=false;
                z.retryCountdown=30;
                fail("zone: chunk load failed; retried on the next plan");
            } else z.keepHigh[id]=false;
            j.model.unload();
            j.staging.reset();
            LightLock_Lock(&z.lock);
            j.state=Impl::Free;
            LightLock_Unlock(&z.lock);
        } else if (st!=Impl::Free) {
            const Chunk d=modelInfo(z.chunks[j.chunk],j.lod);
            z.inflightGeom+=d.geom; z.inflightTex+=d.tex;
        }
    }
#else
    for (auto& j:z.hostJobs) if (j.used) {
        const Chunk d=modelInfo(z.chunks[j.chunk],j.lod);
        z.inflightGeom+=d.geom; z.inflightTex+=d.tex;
    }
#endif
    auto restoreHigh=[&](uint32_t i) {
        if (!z.parked[i]) return;
        sync();
#ifdef __3DS__
        z.models[i].swap(z.parkedModels[i]);
        z.parkedModels[i].unload();
#endif
        z.parked[i]=false; z.resident[i]=true; z.lod[i]=0; z.keepHigh[i]=false;
    };
    for (uint32_t j=0;j<z.numOrder;++j) if (z.choice[z.order[j]]==0) restoreHigh(z.order[j]);
    for (uint32_t j=0;j<z.numWarm;++j)
        if (z.warm[z.warmOrder[j]] && z.warmLod[z.warmOrder[j]]==0) restoreHigh(z.warmOrder[j]);
    recompute();
    // Drop the oldest hidden chunk, then a parked high mesh, until `add` fits.
    // `protect` is never evicted. The displayed mesh of a desired chunk is never evicted.
    auto makeRoom=[&](uint64_t addG,uint64_t addT,uint64_t dropG,uint64_t dropT,int protect,bool forDesired) {
        auto over=[&]() {
            uint64_t g=z.geom+z.inflightGeom+addG, t=z.tex+z.inflightTex+addT;
            if (g>=dropG) g-=dropG; else g=0;
            if (t>=dropT) t-=dropT; else t=0;
            return g>z.retainGeom || t>z.retainTex;
        };
        while (over()) {
            int best=-1, rank=99;
            uint32_t seen=~uint32_t{0};
            for (uint32_t i=0;i<z.h.chunks;++i) {
                if (static_cast<int>(i)==protect) continue;
                const bool warmHold=z.preload && z.warm[i];
                int r=-1;
                if (!z.desired[i] && !warmHold && (z.resident[i] || z.parked[i])) r=0;
                else if (z.h.version>=2 && z.desired[i] && !z.inCone[i] && !warmHold &&
                         (z.resident[i] || z.parked[i])) r=1;
                else if (!z.desired[i] && !warmHold && z.parked[i]) r=2;
                // forDesired used to evict warm holds here, which dropped a chunk the
                // warm loop then queued again. Rank 0 already covers a hidden chunk
                // once preload ends and warmHold is false.
                else if (forDesired && !z.desired[i] && !warmHold && (z.resident[i] || z.parked[i])) r=3;
                else if (forDesired && z.desired[i] && !z.inCone[i] && !warmHold && z.parked[i]) r=4;
                if (r<0) continue;
                if (r<rank || (r==rank && z.seenAt[i]<seen)) { best=static_cast<int>(i); rank=r; seen=z.seenAt[i]; }
            }
            if (best<0) return false;
            const uint32_t i=static_cast<uint32_t>(best);
            sync();
            const bool dropAll=rank==0 || rank==1 || rank==3;
#ifdef __3DS__
            if (dropAll) {
                z.models[i].unload(); z.parkedModels[i].unload();
                z.resident[i]=false; z.parked[i]=false; z.keepHigh[i]=false;
            } else {
                z.parkedModels[i].unload(); z.parked[i]=false;
            }
#else
            if (dropAll) { z.resident[i]=false; z.parked[i]=false; z.keepHigh[i]=false; }
            else z.parked[i]=false;
#endif
            if (rank==1) z.choice[i]=1; // the near mesh no longer fits; the far mesh may load
            recompute();
        }
        return true;
    };
    // 1 = resident or queued, 0 = loader busy, -1 = the retain budget refused it.
    auto queueLoad=[&](uint16_t i,uint8_t lod,bool detail) -> int {
        if (z.failed[i]) return -1;
        if (z.resident[i] && z.lod[i]==lod && !z.keepHigh[i]) return 1;
        // A resident near mesh stays, except an in-cone demotion (choice is the
        // far mesh). Culled chunks are not demoted, so they never swap in the far blob.
        const bool demote=lod==1 && z.desired[i] && z.inCone[i] && z.choice[i]==1;
        if (((z.resident[i] && z.lod[i]==0) || z.parked[i]) && !demote) return 1;
        const Chunk d=modelInfo(z.chunks[i],lod);
        if (!d.size) {
            sync();
#ifdef __3DS__
            if (!z.keepHigh[i]) z.models[i].unload();
#endif
            z.resident[i]=true; z.lod[i]=lod;
            recompute();
            return 1;
        }
        const bool keep=z.keepHigh[i] && z.resident[i];
        const Chunk cur=z.resident[i] ? modelInfo(z.chunks[i],z.lod[i]) : Chunk{};
        const uint64_t dropG=z.resident[i] && !keep ? cur.geom : 0;
        const uint64_t dropT=z.resident[i] && !keep ? cur.tex : 0;
        // The cone always loads, even when hidden chunks already fill the retain
        // cap. Warm and other non-cone reads only fill what is left, and on device
        // they also stop when linear free would fall under 12 MB.
        const bool essential=z.desired[i] && z.inCone[i];
        const bool fit=makeRoom(d.geom,d.tex,dropG,dropT,static_cast<int>(i),true);
        if (!essential && !fit) return -1;
#ifdef __3DS__
        if (!essential) {
            const u32 free=linearSpaceFree();
            const uint64_t need=uint64_t{kLinearHeadroom}+z.inflightGeom+z.inflightTex+d.geom+d.tex;
            if (free<need) return -1;
        }
        bool queued=false;
        Impl::Job* slot=nullptr;
        for (auto& job:z.jobs) {
            if (job.state!=Impl::Free && job.chunk==i && job.lod==lod) queued=true;
            if (job.state==Impl::Free && !slot) slot=&job;
        }
        if (queued) return 1;
        if (!slot) return 0;
        if (detail && lod==0 && !z.keepHigh[i]) ++z.detailReloads;
        ++z.loadsQueued;
        z.inflightGeom+=d.geom; z.inflightTex+=d.tex;
        LightLock_Lock(&z.lock);
        slot->chunk=i; slot->lod=lod; slot->state=Impl::Queued;
        LightLock_Unlock(&z.lock);
        LightEvent_Signal(&z.wake);
#else
        bool queued=false;
        Impl::HostJob* slot=nullptr;
        for (auto& job:z.hostJobs) {
            if (job.used && job.chunk==i && job.lod==lod) queued=true;
            if (!job.used && !slot) slot=&job;
        }
        if (queued) return 1;
        if (!slot) return 0;
        if (detail && lod==0 && !z.keepHigh[i]) ++z.detailReloads;
        ++z.loadsQueued;
        z.inflightGeom+=d.geom; z.inflightTex+=d.tex;
        slot->used=true; slot->chunk=i; slot->lod=lod;
        (void)keep;
#endif
        return 1;
    };
    recompute();
    makeRoom(0,0,0,0,-1,false); // hidden and culled near meshes leave before new reads
    for (uint32_t j=0;j<z.numOrder;++j) {
        const uint16_t i=z.order[j];
        if (z.failed[i] || (z.resident[i] && z.lod[i]==z.choice[i])) continue;
        const int q=queueLoad(i,z.choice[i],true);
        if (q<0) { fail("zone: linear budget full; waiting for evictions"); break; }
        if (q==0) break;
    }
    for (uint32_t j=0;j<z.numWarm && z.preload;++j) {
        const uint16_t i=z.warmOrder[j];
        if (!z.warm[i] || z.failed[i]) continue;
        if (z.resident[i] && (z.lod[i]==z.warmLod[i] || (z.warmLod[i]==1 && z.lod[i]==0))) continue;
        const int q=queueLoad(i,z.warmLod[i],true);
        if (q<0) z.warm[i]=false;
        if (q==0) break;
    }
    makeRoom(0,0,0,0,-1,true); // LRU only once the retain budget is actually full
    recompute();
    if (z.preload) {
        bool pending=false;
        for (uint32_t j=0;j<z.numWarm;++j) {
            const uint16_t i=z.warmOrder[j];
            if (!z.warm[i] || z.failed[i]) continue;
            const bool have=z.resident[i] && (z.lod[i]==z.warmLod[i] || (z.warmLod[i]==1 && z.lod[i]==0));
            if (!have && !(z.parked[i] && z.warmLod[i]==0)) pending=true;
        }
        if (!pending) z.preload=false;
    }
#ifdef __3DS__
    // Hidden and warm chunks leave until the Old 3DS linear heap has 12 MB free.
    // In-cone chunks stay: that is the scenery the frame has to draw. Clearing
    // warm[] stops the ring from queuing the same chunk again next frame.
    auto evictOne=[&](bool dropWarm) {
        int best=-1;
        uint32_t seen=~uint32_t{0};
        for (uint32_t i=0;i<z.h.chunks;++i) {
            if (z.desired[i] && z.inCone[i]) continue;
            if (!dropWarm && z.preload && z.warm[i]) continue;
            if (!(z.resident[i] || z.parked[i])) continue;
            bool busy=false;
            for (auto& j:z.jobs) if (j.state!=Impl::Free && j.chunk==i) busy=true;
            if (busy) continue;
            if (z.seenAt[i]<seen) { best=static_cast<int>(i); seen=z.seenAt[i]; }
        }
        if (best<0) return false;
        const uint32_t i=static_cast<uint32_t>(best);
        sync();
        z.models[i].unload(); z.parkedModels[i].unload();
        z.resident[i]=false; z.parked[i]=false; z.keepHigh[i]=false;
        z.warm[i]=false;
        recompute();
        return true;
    };
    while (linearSpaceFree()<kLinearHeadroom) {
        if (evictOne(false)) continue;
        if (evictOne(true)) continue;
        break;
    }
#endif
    return true;
}
bool Zone::loading() const {
    if (!impl_ || !impl_->planned) return false;
    for (uint32_t j=0;j<impl_->numOrder;++j) {
        const uint16_t i=impl_->order[j];
        if (!impl_->failed[i] && !(impl_->resident[i] && impl_->lod[i]==impl_->choice[i])) return true;
    }
    return false;
}
void Zone::setCamera(const Vec3& eye, const Vec3& target) {
    if (!impl_) return;
    if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z) ||
        !std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)) return;
    impl_->camEye=eye;
    impl_->camTarget=target;
    impl_->haveCamera=true;
}
void Zone::finishLoading(const Vec3& feet,CellChanged callback,void* user) {
    for (int guard=0; guard<3000 && impl_; ++guard) {
        update(feet,callback,user);
#ifndef __3DS__
        completeLoads(); // the device loader finishes inside update(); host tests publish here
#endif
        if (!loading()) return;
#ifdef __3DS__
        svcSleepThread(2 * 1000000LL); // let the loader thread run
#endif
    }
}
void Zone::setRetainBudget(uint32_t geomBytes,uint32_t texBytes) {
    if (!impl_ || !geomBytes || !texBytes) return;
    impl_->retainGeom=geomBytes;
    impl_->retainTex=texBytes;
}
void Zone::warmAround(const Vec3& feet,float radius) {
    if (!impl_ || !impl_->planned || !(radius>0)) return;
    Impl& z=*impl_;
    z.numWarm=0;
    memset(z.warm,0,sizeof(z.warm));
    z.preload=true;
    const float r2=radius*radius;
    const tt::render::Quality& quality=tt::render::activeQuality();
    uint16_t ids[kMaxChunks];
    uint32_t n=0;
    for (uint32_t i=0;i<z.h.chunks;++i) {
        if (z.desired[i]) continue;
        if (distance2(feet,z.chunks[i])>r2) continue;
        ids[n++]=static_cast<uint16_t>(i);
    }
    for (uint32_t j=1;j<n;++j) {
        const uint16_t id=ids[j];
        const float dist=distance2(feet,z.chunks[id]);
        uint32_t at=j;
        while (at>0 && distance2(feet,z.chunks[ids[at-1]])>dist) { ids[at]=ids[at-1]; --at; }
        ids[at]=id;
    }
    uint64_t g=z.geom+z.inflightGeom, t=z.tex+z.inflightTex;
    for (uint32_t j=0;j<n;++j) {
        const uint16_t id=ids[j];
        uint8_t lod=0;
        if (z.h.version>=2 && distance2(feet,z.chunks[id])>quality.lodNear*quality.lodNear) lod=1;
        const Chunk d=modelInfo(z.chunks[id],lod);
        if (g+d.geom>z.retainGeom || t+d.tex>z.retainTex) continue;
        z.warm[id]=true;
        z.warmLod[id]=lod;
        z.warmOrder[z.numWarm++]=id;
        g+=d.geom; t+=d.tex;
    }
}
bool Zone::warming() const {
    if (!impl_ || !impl_->preload) return false;
    for (uint32_t j=0;j<impl_->numWarm;++j) {
        const uint16_t i=impl_->warmOrder[j];
        if (!impl_->warm[i] || impl_->failed[i]) continue;
        const bool have=impl_->resident[i] && (impl_->lod[i]==impl_->warmLod[i] ||
                         (impl_->warmLod[i]==1 && impl_->lod[i]==0));
        if (!have && !(impl_->parked[i] && impl_->warmLod[i]==0)) return true;
    }
    return false;
}
bool Zone::highParked(int index) const {
    return impl_ && index>=0 && index<numChunks() && impl_->parked[index];
}
uint32_t Zone::loadsQueued() const { return impl_ ? impl_->loadsQueued : 0; }
uint32_t Zone::detailReloads() const { return impl_ ? impl_->detailReloads : 0; }
int Zone::completeLoads() {
#ifdef __3DS__
    return 0;
#else
    if (!impl_) return 0;
    Impl& z=*impl_;
    int n=0;
    for (auto& j:z.hostJobs) {
        if (!j.used) continue;
        const uint16_t id=j.chunk;
        const bool plan=z.desired[id] && z.choice[id]==j.lod;
        const bool warmKeep=!z.desired[id] && z.warm[id] && z.warmLod[id]==j.lod;
        const bool nearHeld=(z.resident[id] && z.lod[id]==0) || z.parked[id];
        if (plan || warmKeep) {
            z.resident[id]=true; z.lod[id]=j.lod; z.keepHigh[id]=false;
            if (j.lod==0) z.parked[id]=false;
        } else if (j.lod==1 && nearHeld) {
            z.keepHigh[id]=false;
        } else z.keepHigh[id]=false;
        j.used=false;
        ++n;
    }
    if (n) recountZone(z);
    return n;
#endif
}
void Zone::draw(render::Renderer& renderer,const Vec3& eye,float distance,const float* placement) {
    if (!impl_ || distance<=0) return;
#ifdef __3DS__
    C3D_Mtx identity;
    Mtx_Identity(&identity);
    if (placement) {
        for (int r=0;r<3;++r) {
            identity.r[r].x=placement[r]; identity.r[r].y=placement[3+r];
            identity.r[r].z=placement[6+r]; identity.r[r].w=placement[9+r];
        }
    }
#else
    (void)placement;
#endif
    Impl& z=*impl_;
    Vec3 fwd{}, right{}, up{};
    const bool cap=z.haveCamera;
    if (cap) cameraBasis(eye, z.camTarget, fwd, right, up);
    bool seen[kMaxChunks]={};
    auto submit=[&](uint32_t i) {
        if (i>=z.h.chunks || seen[i]) return;
        seen[i]=true;
        if (!z.resident[i] || z.parked[i]) return;
        const Chunk mesh=modelInfo(z.chunks[i], z.lod[i]);
        if (!mesh.size || distance2(eye, z.chunks[i])>distance*distance) return;
        // Retained chunks stay in memory. Only the cone is submitted. Every in-cone
        // chunk is drawn, at least at the far mesh the plan left it on. The 130-draw
        // cap limits near upgrades; it does not drop a farther chunk.
        if (cap) {
            const bool viewed=static_cast<int>(i)==z.cell || inView(z.chunks[i], eye, fwd, right, up, distance);
            if (!viewed) return;
        }
#ifdef __3DS__
        renderer.drawModel(z.models[i], identity);
#else
        (void)renderer;
        (void)mesh;
#endif
    };
    for (uint32_t j=0;j<z.numOrder;++j) submit(z.order[j]);
    for (uint32_t i=0;i<z.h.chunks;++i) submit(i);
}
int Zone::visibleZones(uint16_t* out,int capacity) const {
    if (!impl_ || impl_->cell<0) return 0;
    uint16_t ids[kMaxChunks+1];
    int count=1;
    ids[0]=branchZone();
    const Chunk& c=impl_->chunks[impl_->cell];
    auto add=[&](uint32_t chunk) {
        const uint16_t id=static_cast<uint16_t>(impl_->chunks[chunk].zone);
        bool found=false;
        for (int j=0;j<count;++j) if (ids[j]==id) found=true;
        if (!found && count<kMaxChunks+1) ids[count++]=id;
    };
    for (uint32_t i=0;i<c.numVis;++i) add(impl_->vis[c.firstVis+i]);
    for (uint32_t i=0;i<impl_->h.chunks;++i) if (impl_->extra[i]) add(i);
    if (out && capacity>0) std::copy(ids,ids+std::min(count,capacity),out);
    return count;
}
void Zone::setExtraVisible(const uint16_t* zones, int count) {
    if (!impl_) return;
    bool next[kMaxChunks]={};
    for (int k=0;k<count;++k)
        for (uint32_t i=0;i<impl_->h.chunks;++i)
            if (impl_->chunks[i].zone==zones[k]) next[i]=true;
    if (memcmp(next,impl_->extra,sizeof(next))!=0) {
        memcpy(impl_->extra,next,sizeof(next));
        impl_->extraDirty=true;
    }
}
uint16_t Zone::currentZone() const { return impl_ && impl_->cell>=0 ? static_cast<uint16_t>(impl_->chunks[impl_->cell].zone) : 0; }
uint16_t Zone::branchZone() const { return impl_ ? static_cast<uint16_t>(impl_->h.branch) : 0; }
int Zone::currentCell() const { return impl_ ? impl_->cell : -1; }
const float* Zone::boundsMin() const { static const float empty[3]={}; return impl_ ? impl_->min : empty; }
const float* Zone::boundsMax() const { static const float empty[3]={}; return impl_ ? impl_->max : empty; }
int Zone::numChunks() const { return impl_ ? static_cast<int>(impl_->h.chunks) : 0; }
int Zone::numVisible() const {
    if (!impl_ || impl_->cell<0) return 0;
    return static_cast<int>(impl_->chunks[impl_->cell].numVis);
}
uint16_t Zone::version() const { return impl_ ? impl_->h.version : 0; }
uint32_t Zone::triangles() const { return impl_ ? impl_->tris : 0; }
uint32_t Zone::drawCalls() const { return impl_ ? impl_->draws : 0; }
int Zone::chunkLod(int i) const { return impl_ && i>=0 && i<numChunks() && impl_->resident[i] ? impl_->lod[i] : -1; }
uint32_t Zone::geometryBytes() const { return impl_ ? impl_->geom : 0; }
uint32_t Zone::textureBytes() const { return impl_ ? impl_->tex : 0; }
int Zone::numTriggers() const { return impl_ ? static_cast<int>(impl_->h.triggers) : 0; }
const Zone::Trigger* Zone::trigger(int i) const { return i>=0 && i<numTriggers() ? &impl_->triggers[i] : nullptr; }
int Zone::triggersAt(const Vec3& p,float radius,int* out,int capacity,bool tunnelsOnly) const {
    if (!std::isfinite(radius) || radius<0 || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return 0;
    int count=0;
    const float v[3]={p.x,p.y,p.z};
    for (int i=0;i<numTriggers();++i) {
        const Trigger& t=impl_->triggers[i];
        if (tunnelsOnly && t.kind!=2) continue;
        float d2=0;
        for (int a=0;a<3;++a) {
            const float d=std::max(std::max(t.min[a]-v[a],v[a]-t.max[a]),0.0f);
            d2+=d*d;
        }
        if (d2<=radius*radius) {
            if (out && count<capacity) out[count]=i;
            ++count;
        }
    }
    return count;
}
} // namespace tt::world
