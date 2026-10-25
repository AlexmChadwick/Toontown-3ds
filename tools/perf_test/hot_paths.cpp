#include "render/hot_paths.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
using namespace tt::render;
static volatile float sink=0;
static void multiply(float* out,const float* a,const float* b) {
    for(int r=0;r<4;++r) for(int c=0;c<4;++c) {
        float sum=0;
        for(int k=0;k<4;++k) sum+=a[4*r+3-k]*b[4*k+3-c];
        out[4*r+3-c]=sum;
    }
}
static void reference(float* out,const float* world,const float* scale,const float* offset) {
    float q[16]={};
    for(int i=0;i<4;++i) q[4*i+3-i]=i<3?scale[i]:1;
    for(int i=0;i<3;++i) q[4*i]=offset[i];
    multiply(out,world,q);
}
template<class F> static double bench(F function) {
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<1000000;++i) function(i);
    return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/1000000;
}
int main() {
    float world[16],scale[3]={.02f,.03f,.04f},offset[3]={45,-33,22},a[16],b[16];
    for(int sample=0;sample<10000;++sample) {
        for(int i=0;i<16;++i) world[i]=std::sin(float(sample+i)*.13f)*7;
        reference(a,world,scale,offset);
        dequantizeWorld(b,world,scale,offset);
        for(int i=0;i<16;++i) assert(std::fabs(a[i]-b[i])<=.0001f);
    }
    const double before=bench([&](int i) { world[0]=i*.0001f;reference(a,world,scale,offset);sink=a[i&15]; });
    const double after=bench([&](int i) { world[0]=i*.0001f;dequantizeWorld(a,world,scale,offset);sink=a[i&15]; });
    BatchState state;
    const float white[4]={1,1,1,1},red[4]={1,0,0,1};
    assert(state.uploadTint(white));assert(!state.uploadTint(white));
    assert(state.uploadTint(red));assert(state.uploadTint(white));
    assert(state.bindTexture(nullptr));assert(!state.bindTexture(nullptr));
    assert(state.bindTexture(&state));assert(!state.bindTexture(&state));
    state.tintValid=false;assert(state.uploadTint(white));
    int oldTint=0,newTint=0,oldTexture=0,newTexture=0;
    state=BatchState{};
    for(int model=0;model<128;++model) {
        BatchState old;
        for(int batch=0;batch<3;++batch) {
            oldTint+=old.uploadTint(white);newTint+=state.uploadTint(white);
            oldTexture+=old.bindTexture(nullptr);newTexture+=state.bindTexture(nullptr);
        }
    }
    assert(oldTint==128 && newTint==1 && oldTexture==128 && newTexture==1);
    std::printf("dequantizeWorld ns/draw: before %.2f after %.2f speedup %.2fx; 10000 equivalence cases PASS\n",before,after,before/after);
    std::printf("128 shared-model instances, same ordered batches: tint uploads %d -> %d, texture states %d -> %d PASS\n",oldTint,newTint,oldTexture,newTexture);
}
