#pragma once
struct C3D_FVec { float x = 0, y = 0, z = 0, w = 0; };
struct C3D_Mtx {};
inline C3D_FVec FVec3_New(float x, float y, float z) { return {x,y,z,0}; }
inline void Mtx_Identity(C3D_Mtx*) {}
inline void Mtx_Translate(C3D_Mtx*, float, float, float, bool) {}
inline void Mtx_RotateY(C3D_Mtx*, float, bool) {}
