#include "world/zone.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#ifdef __3DS__
#include "render/renderer.h"
#endif

namespace tt::world {
namespace {
struct Header {
    char magic[4];
    uint16_t version, flags;
    uint32_t branch, chunks, vis, floors, triggers;
    uint32_t chunkOff, visOff, floorOff, triggerOff, collisionOff, collisionSize, size, reserved[2];
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
// Validate every field trusted by Model::load before permitting GPU allocation.
bool validModel(FILE* f, const Chunk& c) {
    struct ModelHeader {
        char magic[4]; uint16_t version, flags, textures, batches;
        uint32_t vertices, indices; float scale[3], offset[3];
        uint32_t texOff, batchOff, vertexOff, indexOff, reserved;
    } h;
    static_assert(sizeof(h) == 64, "TTM header");
    if (c.size < 64 || !readAt(f, c.offset, &h, sizeof(h)) || memcmp(h.magic,"TTM1",4) ||
        h.version != 1 || h.flags || h.batches != c.draws || h.indices % 3 || h.indices / 3 != c.tris ||
        static_cast<uint64_t>(h.vertices)*16 + static_cast<uint64_t>(h.indices)*2 != c.geom) return false;
    for (int i=0;i<3;++i)
        if (!std::isfinite(h.scale[i]) || h.scale[i]<=0 || !std::isfinite(h.offset[i])) return false;
    if (!span(h.texOff,h.textures,48,c.size) || !span(h.batchOff,h.batches,24,c.size) ||
        !span(h.vertexOff,h.vertices,16,c.size) || !span(h.indexOff,h.indices,2,c.size)) return false;
    struct Tex { uint16_t w, h; uint8_t fmt, mips, ws, wt; uint32_t off, size; char name[32]; };
    uint64_t total=0;
    for (uint32_t i=0;i<h.textures;++i) {
        Tex t;
        if (!readAt(f,c.offset+h.texOff+i*48,&t,sizeof(t)) || t.w<8 || t.h<8 || t.w>256 || t.h>256 ||
            (t.w & (t.w-1)) || (t.h & (t.h-1)) || !t.mips || t.mips>6 ||
            (t.ws!=0 && t.ws!=2) || (t.wt!=0 && t.wt!=2) || !span(t.off,t.size,1,c.size)) return false;
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
    for (uint32_t i=0;i<h.batches;++i) {
        Batch b;
        if (!readAt(f,c.offset+h.batchOff+i*24,&b,sizeof(b)) ||
            (b.texture!=65535 && b.texture>=h.textures) || b.blend>2 || b.flags>1 || b.tint ||
            b.layer>4 || b.slot || b.pad || b.fv>h.vertices || b.nv>h.vertices-b.fv ||
            b.fi>h.indices || b.ni>h.indices-b.fi || b.ni%3) return false;
        // Model::loadFrom checks indices in memory; only scan here when it cannot.
        for (uint32_t j=0;h.vertices>0x10000 && j<b.ni;) {
            const uint32_t n=std::min(b.ni-j,static_cast<uint32_t>(512));
            if (!readAt(f,c.offset+h.indexOff+(b.fi+j)*2,ids,n*2)) return false;
            for (uint32_t k=0;k<n;++k) if (ids[k]>=b.nv) return false;
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
    uint8_t choice[kMaxChunks] = {};
    bool failed[kMaxChunks] = {}; // load failed for this plan; retried on the next one
    int retryCountdown = 0;       // updates until failed loads are retried anyway
    uint16_t order[kMaxChunks] = {};
    uint32_t numOrder = 0;
    bool planned = false;
    bool extra[kMaxChunks] = {}; // setExtraVisible: chunks shown beyond the cell's PVS
    bool extraDirty = false;
    Vec3 lastPlan{};
    int cell = -1;
    uint32_t geom = 0, tex = 0, tris = 0, draws = 0;
    float min[3] = {}, max[3] = {};
#ifdef __3DS__
    enum JobState : uint8_t { Free, Queued, Loading, Done, Failed };
    struct Job {
        volatile JobState state = Free;
        uint16_t chunk = 0;
        uint8_t lod = 0;
        render::Model model;
    };
    static constexpr int kJobs = 3;
    render::Model* models = nullptr;
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
    Zone::Impl& z = *static_cast<Zone::Impl*>(arg);
    while (!z.quit) {
        Zone::Impl::Job* job = nullptr;
        LightLock_Lock(&z.lock);
        for (auto& j : z.jobs)
            if (j.state == Zone::Impl::Queued) { job = &j; j.state = Zone::Impl::Loading; break; }
        LightLock_Unlock(&z.lock);
        if (!job) { LightEvent_Wait(&z.wake); continue; }
        const Chunk d = modelInfo(z.chunks[job->chunk], job->lod);
        // Model owns the shared LinearGuard; never lock around this call.
        const bool ok = job->model.loadFrom(z.file, d.offset, "tz chunk");
        LightLock_Lock(&z.lock);
        job->state = ok ? Zone::Impl::Done : Zone::Impl::Failed;
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
        threadJoin(impl_->thread, U64_MAX);
        threadFree(impl_->thread);
    }
    C3D_FrameSync();
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
    unload();
    error_[0]=0;
    if (!path || !*path) return fail("zone: invalid path");
    impl_ = new(std::nothrow) Impl;
    if (!impl_) return fail("zone: out of heap");
    Impl& z=*impl_;
    z.file=fopen(path,"rb");
    bool ok=z.file && readAt(z.file,0,&z.h,sizeof(z.h));
    Header& h=z.h;
    ok=ok && !memcmp(h.magic,"TZ01",4) && (h.version==1 || h.version==2) && !h.flags && !h.reserved[0] && !h.reserved[1] &&
       h.branch>0 && h.branch<=65535 && h.chunks>0 && h.chunks<=kMaxChunks &&
       h.vis<=kMaxChunks*kMaxChunks && h.floors<=100000 && h.triggers<=4096 && h.size<=128u*1024*1024;
    const uint32_t stride=h.version==2 ? 104 : 80;
    if (ok) {
        ok=fseek(z.file,0,SEEK_END)==0 && ftell(z.file)==static_cast<long>(h.size) &&
           section(h.chunkOff,h.chunks,stride,h.size) && section(h.visOff,h.vis,2,h.size) &&
           section(h.floorOff,h.floors,40,h.size) && section(h.triggerOff,h.triggers,156,h.size) &&
           h.collisionSize>=96 && h.collisionSize<=8u*1024*1024 && section(h.collisionOff,h.collisionSize,1,h.size);
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
    for (uint32_t i=0;ok && i<h.chunks;++i) {
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
        if (c.size) ok=ok && section(c.offset,c.size,1,h.size) && validModel(z.file,c);
        else ok=ok && !(c.offset || c.geom || c.tex || c.draws || c.tris);
        if (h.version==2) {
            const Chunk low=modelInfo(c,1);
            if (low.size) ok=ok && section(low.offset,low.size,1,h.size) && validModel(z.file,low);
            else ok=ok && !(low.offset || low.geom || low.tex || low.draws || low.tris);
        }
    }
    // Sections must be disjoint, including blobs (prevents aliasing metadata).
    uint32_t starts[kMaxChunks*2+5], ends[kMaxChunks*2+5];
    int ranges=0;
    auto addRange=[&](uint32_t off,uint32_t size) {
        if (size) { starts[ranges]=off; ends[ranges++]=off+size; }
    };
    if (ok) {
        addRange(h.chunkOff,h.chunks*stride); addRange(h.visOff,h.vis*2);
        addRange(h.floorOff,h.floors*40); addRange(h.triggerOff,h.triggers*156);
        addRange(h.collisionOff,h.collisionSize);
        for (uint32_t i=0;i<h.chunks;++i) {
            addRange(z.chunks[i].offset,z.chunks[i].size);
            if (h.version==2) addRange(z.chunks[i].lowOffset,z.chunks[i].lowSize);
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
            const Chunk v=modelInfo(z.chunks[z.vis[c.firstVis+j]],h.version==2 ? 1 : 0);
            geom+=v.geom; tex+=v.tex; draws+=v.draws; tris+=v.tris;
        }
        ok=geom<=kGeometryBudget && tex<=kTextureBudget && draws<=150 && (h.version==1 || tris<=kTriangleBudget);
    }
    if (ok) {
        ok=collision_.loadFrom(z.file,h.collisionOff,h.collisionSize,"tz collision");
    }
#ifdef __3DS__
    if (ok) { z.models=new(std::nothrow) render::Model[h.chunks]; ok=z.models!=nullptr; }
#endif
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
    if (next>=0 && !replan && z.h.version==2) {
        const float dx=feet.x-z.lastPlan.x,dy=feet.y-z.lastPlan.y,dz=feet.z-z.lastPlan.z;
        replan=dx*dx+dy*dy+dz*dz>=16.0f; // avoid churn for tiny movements
    }
    if (replan) {
        const Chunk& c=z.chunks[next];
        memset(z.desired,0,sizeof(z.desired));
        memset(z.choice,0,sizeof(z.choice));
        memset(z.failed,0,sizeof(z.failed));
        uint64_t geom=0,tex=0,tris=0,draws=0;
        uint32_t count=0;
        auto want=[&](uint16_t id) {
            if (z.desired[id]) return;
            z.desired[id]=true;
            z.choice[id]=z.h.version==2 ? 1 : 0;
            z.order[count++]=id;
            const Chunk d=modelInfo(z.chunks[id],z.choice[id]);
            geom+=d.geom; tex+=d.tex; tris+=d.tris; draws+=d.draws;
        };
        for (uint32_t j=0;j<c.numVis;++j) want(z.vis[c.firstVis+j]);
        for (uint32_t i=0;i<z.h.chunks;++i) if (z.extra[i]) want(static_cast<uint16_t>(i));
        z.extraDirty=false;
        z.numOrder=count;
        // Stable nearest-first insertion sort; fixed storage, no per-frame allocation.
        for (uint32_t j=1;j<count;++j) {
            const uint16_t id=z.order[j];
            const float dist=distance2(feet,z.chunks[id]);
            uint32_t at=j;
            while (at>0 && distance2(feet,z.chunks[z.order[at-1]])>dist) {
                z.order[at]=z.order[at-1]; --at;
            }
            z.order[at]=id;
        }
        if (z.h.version==2) {
            for (uint32_t j=0;j<count;++j) {
                const uint16_t id=z.order[j];
                const Chunk& high=z.chunks[id];
                // Hysteresis: a chunk already at full detail keeps it out to 75 units, so a
                // toon standing near the 65-unit boundary doesn't reload it from SD repeatedly.
                const float reach=z.resident[id] && z.lod[id]==0 ? 75.0f : 65.0f;
                if (distance2(feet,high)>reach*reach) continue;
                const Chunk low=modelInfo(high,1);
                const uint64_t g=geom-low.geom+high.geom,t=tex-low.tex+high.tex;
                const uint64_t tr=tris-low.tris+high.tris,dr=draws-low.draws+high.draws;
                if (g<=kGeometryBudget && t<=kTextureBudget && tr<=kTriangleBudget && dr<=150) {
                    z.choice[id]=0; geom=g; tex=t; tris=tr; draws=dr;
                }
            }
        }
        z.lastPlan=feet;
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
    auto account=[&](uint32_t i,int sign) {
        const Chunk d=modelInfo(z.chunks[i],z.lod[i]);
        z.geom+=sign*d.geom; z.tex+=sign*d.tex; z.tris+=sign*d.tris; z.draws+=sign*d.draws;
    };
    bool synced=false;
    auto sync=[&]() {
#ifdef __3DS__
        if (!synced) C3D_FrameSync(); // the last frame may still read what we free
#endif
        synced=true;
    };
    (void)sync;
#ifdef __3DS__
    // 1. Swap in finished loads (still wanted) and drop the rest.
    uint64_t inflightGeom=0,inflightTex=0;
    for (auto& j:z.jobs) {
        LightLock_Lock(&z.lock);
        const Impl::JobState st=j.state;
        LightLock_Unlock(&z.lock);
        if (st==Impl::Done || st==Impl::Failed) {
            const bool wanted=z.desired[j.chunk] && z.choice[j.chunk]==j.lod;
            if (st==Impl::Done && wanted) {
                if (z.resident[j.chunk]) { sync(); account(j.chunk,-1); } // only an old model needs the GPU idle
                z.models[j.chunk].swap(j.model);
                z.resident[j.chunk]=true; z.lod[j.chunk]=j.lod;
                account(j.chunk,+1);
            } else if (st==Impl::Failed && wanted) {
                z.failed[j.chunk]=true;
                z.retryCountdown=30; // about a second: transient SD/linear failures heal
                fail("zone: chunk load failed; retried on the next plan");
            }
            j.model.unload(); // the old LOD, or an unwanted load: never drawn by the GPU
            LightLock_Lock(&z.lock);
            j.state=Impl::Free;
            LightLock_Unlock(&z.lock);
        } else if (st!=Impl::Free) {
            const Chunk d=modelInfo(z.chunks[j.chunk],j.lod);
            inflightGeom+=d.geom; inflightTex+=d.tex;
        }
    }
#endif
    // 2. Evict chunks the plan no longer wants. A chunk changing LOD stays drawn until
    // its replacement arrives.
    for (uint32_t i=0;i<z.h.chunks;++i) if (z.resident[i] && !z.desired[i]) {
        sync();
#ifdef __3DS__
        z.models[i].unload();
#endif
        account(i,-1);
        z.resident[i]=false;
    }
    // 3. Queue missing chunks, nearest first.
    for (uint32_t j=0;j<z.numOrder;++j) {
        const uint16_t i=z.order[j];
        if (z.failed[i] || (z.resident[i] && z.lod[i]==z.choice[i])) continue;
        const Chunk d=modelInfo(z.chunks[i],z.choice[i]);
#ifdef __3DS__
        bool queued=false;
        Impl::Job* slot=nullptr;
        for (auto& job:z.jobs) {
            if (job.state!=Impl::Free && job.chunk==i && job.lod==z.choice[i]) queued=true;
            if (job.state==Impl::Free && !slot) slot=&job;
        }
        if (queued) continue;
        if (!slot) break;
        if (!d.size) { // empty chunk: nothing to read
            if (z.resident[i]) { sync(); z.models[i].unload(); account(i,-1); }
            z.resident[i]=true; z.lod[i]=z.choice[i];
            continue;
        }
        if (z.geom+inflightGeom+d.geom>kGeometryBudget || z.tex+inflightTex+d.tex>kTextureBudget) {
            fail("zone: linear budget full; waiting for evictions");
            break;
        }
        inflightGeom+=d.geom; inflightTex+=d.tex;
        LightLock_Lock(&z.lock);
        slot->chunk=i; slot->lod=z.choice[i]; slot->state=Impl::Queued;
        LightLock_Unlock(&z.lock);
        LightEvent_Signal(&z.wake);
#else
        if (z.resident[i]) account(i,-1);
        z.resident[i]=true; z.lod[i]=z.choice[i];
        account(i,+1);
        (void)d;
#endif
    }
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
void Zone::finishLoading(const Vec3& feet,CellChanged callback,void* user) {
    for (int guard=0; guard<3000 && impl_; ++guard) {
        update(feet,callback,user);
        if (!loading()) return;
#ifdef __3DS__
        svcSleepThread(2 * 1000000LL); // let the loader thread run
#endif
    }
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
    for (uint32_t i=0;i<impl_->h.chunks;++i) {
        if (!impl_->resident[i] || !modelInfo(impl_->chunks[i],impl_->lod[i]).size) continue;
        const Chunk& c=impl_->chunks[i];
        const float p[3]={eye.x,eye.y,eye.z};
        float d2=0;
        for (int a=0;a<3;++a) {
            const float d=std::max(std::max(c.min[a]-p[a],p[a]-c.max[a]),0.0f);
            d2+=d*d;
        }
        if (d2>distance*distance) continue;
#ifdef __3DS__
        renderer.drawModel(impl_->models[i],identity);
#else
        (void)renderer;
#endif
    }
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
