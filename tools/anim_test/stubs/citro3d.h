#pragma once
#include <3ds.h>
struct C3D_FVec { float x,y,z,w; };
struct C3D_Mtx { C3D_FVec r[4]; };
inline C3D_FVec FVec4_New(float x,float y,float z,float w) { return {x,y,z,w}; }
struct C3D_Tex { void* data=nullptr;u32 size=0,maxLevel=0; };
inline u32 C3D_TexCalcTotalSize(u32 size,u32) { return size; }
void C3D_TexDelete(C3D_Tex*);
void C3D_FrameSync();
