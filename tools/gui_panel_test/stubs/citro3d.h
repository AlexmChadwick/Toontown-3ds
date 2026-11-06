// Host stub of <citro3d.h> for the GUI panel tests. Only the API the compiled
// bottom-screen sources use.
#pragma once

#include <cstdint>
#include "3ds.h"

struct C3D_Tex {
    u32 magic = 0x1234; // non-zero so loaded() paths stay taken
};

struct C3D_RenderTarget {
    int unused = 0;
};

struct C3D_Mtx {};

struct C3D_FVec {
    float x = 0, y = 0, z = 0, w = 0;
};

// Shader objects owned by render/renderer.h; inert on the host.
struct DVLB_s {
    int dummy = 0;
};
struct shaderProgram_s {
    int dummy = 0;
};
struct C3D_FogLut {
    float data[128] = {};
};

enum { C3D_FOG_GPU_FVEC = 0 };

enum GPU_TexFilter { GPU_NEAREST = 0, GPU_LINEAR = 1 };
enum GPU_TEXFACE { GPU_TEXFACE_TEX2 = 0 };
enum GPU_WrapMode {
    GPU_CLAMP_TO_EDGE = 0,
    GPU_REPEAT = 1,
    GPU_MIRRORED_REPEAT = 2,
};
enum GPU_TEXCOLORFORMAT {
    GPU_RGBA8 = 0,
    GPU_RGB8 = 1,
    GPU_ETC1 = 3,
    GPU_ETC1A4 = 4,
    GPU_L8 = 5,
    GPU_A8 = 6,
    GPU_LA8 = 7,
    GPU_RGBA5551 = 8,
    GPU_RGBA4 = 9,
    GPU_RGB565 = 10,
};
enum GPUScissorMode {
    GPU_SCISSOR_DISABLE = 0,
    GPU_SCISSOR_NORMAL = 1,
    GPU_SCISSOR_INVERT = 2,
};
enum GPU_TESTFUNC { GPU_ALWAYS = 0 };
enum GPU_BLENDMODE { GPU_BLEND_ADD = 0, GPU_BLEND_PREMULT = 1, GPU_BLEND_NONE = 2 };
enum GPU_BLENDFACTOR { GPU_ONE = 0, GPU_SRC_ALPHA = 1 };
enum GPU_PRIMITIVE { GPU_TRIANGLES = 0 };
enum GPU_FMT_SHADER { GPU_SHORT = 0 };
enum { C3D_DEPTHTYPE = 0 };

inline void C3D_TexDelete(C3D_Tex*) {}
inline void C3D_SetScissor(GPUScissorMode mode, u32 x0, u32 y0, u32 x1, u32 y1);
inline void C3D_DepthTest(bool, GPU_TESTFUNC, u32) {}

// Matrix ops the world headers reference (world/place.h); inert on the host.
inline void Mtx_Identity(C3D_Mtx*) {}
inline void Mtx_Multiply(C3D_Mtx*, const C3D_Mtx*, const C3D_Mtx*) {}
