#include "render/renderer.h"

#include <cmath>

#include "core/log.h"
#include "morph_shbin.h"
#include "world_shbin.h"

namespace tt::render {

namespace {

constexpr u32 kClearSky = 0x6AB0D9FF; // RGBA8; Toontown-ish sky blue
constexpr u32 kVertexStride = 16;
constexpr u32 kMorphStride = 8; // tta frame vertex: s16 x, y, z, pad
// Depth bias per "draw in front" layer (eyes over face, pupils over eyes).
// Positive = nearer, because citro3d's perspective + GPU_GREATER is reversed-Z.
constexpr float kLayerDepthBias = 0.00005f;
// Leave headroom in the command buffer: citro3d panics on overflow.
constexpr float kCmdBufLimit = 0.9f;

constexpr u32 kDisplayTransferFlags =
    GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
    GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

C3D_FVec normalizePlane(C3D_FVec p) {
    const float len = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
    return FVec4_New(p.x / len, p.y / len, p.z / len, p.w / len);
}

} // namespace

bool Renderer::init() {
    topLeft_ = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    topRight_ = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!topLeft_ || !topRight_) {
        TT_LOG("renderer: cannot create render targets");
        return false;
    }
    C3D_RenderTargetSetOutput(topLeft_, GFX_TOP, GFX_LEFT, kDisplayTransferFlags);
    C3D_RenderTargetSetOutput(topRight_, GFX_TOP, GFX_RIGHT, kDisplayTransferFlags);

    if (!loadProgram(world_shbin, world_shbin_size, &programs_[kModeStatic])) return false;
    if (!loadProgram(morph_shbin, morph_shbin_size, &programs_[kModeMorph])) return false;
    return true;
}

bool Renderer::loadProgram(const u8* shbin, u32 size, Program* out) {
    out->dvlb = DVLB_ParseFile(reinterpret_cast<u32*>(const_cast<u8*>(shbin)), size);
    if (!out->dvlb) return false;
    shaderProgramInit(&out->program);
    shaderProgramSetVsh(&out->program, &out->dvlb->DVLE[0]);
    out->uProjection = shaderInstanceGetUniformLocation(out->program.vertexShader, "projection");
    out->uModelView = shaderInstanceGetUniformLocation(out->program.vertexShader, "modelView");
    out->uTint = shaderInstanceGetUniformLocation(out->program.vertexShader, "tint");
    out->uBlend = shaderInstanceGetUniformLocation(out->program.vertexShader, "blend");
    return true;
}

void Renderer::shutdown() {
    for (Program& p : programs_) {
        if (!p.dvlb) continue;
        shaderProgramFree(&p.program);
        DVLB_Free(p.dvlb);
        p.dvlb = nullptr;
    }
    if (topLeft_) C3D_RenderTargetDelete(topLeft_);
    if (topRight_) C3D_RenderTargetDelete(topRight_);
    topLeft_ = topRight_ = nullptr;
}

void Renderer::bindMode(Mode mode) {
    if (mode == mode_) return;
    mode_ = mode;
    batchState_.tintValid = false;
    batchState_.geometryValid = false;
    const Program& p = programs_[mode];
    C3D_BindProgram(const_cast<shaderProgram_s*>(&p.program));
    C3D_AttrInfo* attr = C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr, 0, GPU_SHORT, 4);         // position (w = pad, overridden in shader)
    AttrInfo_AddLoader(attr, 1, GPU_SHORT, 2);         // uv
    AttrInfo_AddLoader(attr, 2, GPU_UNSIGNED_BYTE, 4); // rgba
    if (mode == kModeMorph) {
        AttrInfo_AddLoader(attr, 3, GPU_SHORT, 4); // frame A position
        AttrInfo_AddLoader(attr, 4, GPU_SHORT, 4); // frame B position
    }
    ++stats_.uniformUploads;
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, p.uProjection, &projection_);
}

void Renderer::beginWorld(C3D_RenderTarget* target, const C3D_FVec& eye, const C3D_FVec& lookAt, float iod,
                          int eyeSign) {
    batchState_ = BatchState{};
    eye_ = eye;
    C3D_RenderTargetClear(target, C3D_CLEAR_ALL, kClearSky, 0);
    C3D_FrameDrawOn(target);

    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
                   GPU_ONE_MINUS_SRC_ALPHA);
    C3D_CullFace(GPU_CULL_BACK_CCW);
    C3D_DepthMap(true, -1.0f, 0.0f);

    const float fovy = C3D_AngleFromDegrees(kFovYDeg);
    if (eyeSign == 0) {
        Mtx_PerspTilt(&projection_, fovy, C3D_AspectRatioTop, kNear, kFar, false);
    } else {
        Mtx_PerspStereoTilt(&projection_, fovy, C3D_AspectRatioTop, kNear, kFar, eyeSign * iod, 2.0f, false);
    }
    // citro2d rebinds its own shader/attributes/texenv every frame, so rebind ours.
    mode_ = kModeNone;
    bindMode(kModeStatic);

    // Panda/Toontown world space: Z up, right-handed.
    Mtx_LookAt(&view_, eye, lookAt, FVec3_New(0.0f, 0.0f, 1.0f), false);

    // Frustum planes from clip = P * V (Gribb/Hartmann). PICA clip z range is [-w, 0].
    C3D_Mtx clip;
    Mtx_Multiply(&clip, &projection_, &view_);
    const C3D_FVec r0 = clip.r[0], r1 = clip.r[1], r2 = clip.r[2], r3 = clip.r[3];
    // Mtx_PerspTilt rotates x/y for the sideways framebuffer; both axes are bounded
    // by |x| <= w and |y| <= w, so the plane set is the same either way.
    frustum_[0] = normalizePlane(FVec4_Add(r3, r0));
    frustum_[1] = normalizePlane(FVec4_Subtract(r3, r0));
    frustum_[2] = normalizePlane(FVec4_Add(r3, r1));
    frustum_[3] = normalizePlane(FVec4_Subtract(r3, r1));
    frustum_[4] = normalizePlane(FVec4_Negate(r2)); // z <= 0
    frustum_[5] = normalizePlane(FVec4_Add(r3, r2)); // z >= -w
}

void Renderer::endWorld() {
    batchState_ = BatchState{};
    mode_ = kModeNone;
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_DepthMap(true, -1.0f, 0.0f);
    C3D_CullFace(GPU_CULL_NONE);
}

bool Renderer::sphereVisible(const C3D_FVec& c, float radius) const {
    for (const C3D_FVec& p : frustum_) {
        if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -radius) return false;
    }
    return true;
}

bool Renderer::beginDraw(const float* lo, const float* hi, const C3D_Mtx& world) {
    // Bounding sphere in world space (bounds are axis-aligned in model space).
    const C3D_FVec localCenter = FVec4_New((lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f, 1.0f);
    const C3D_FVec worldCenter = Mtx_MultiplyFVec4(&world, localCenter);
    const float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    const float radius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz); // conservative for world scales <= 1
    if (!sphereVisible(worldCenter, radius)) {
        stats_.modelsCulled++;
        return false;
    }
    if (C3D_GetCmdBufUsage() > kCmdBufLimit) {
        stats_.modelsSkipped++;
        return false;
    }
    stats_.modelsDrawn++;
    return true;
}

void Renderer::setModelView(const C3D_Mtx& world, const float* posScale, const float* posOffset) {
    // modelView = view * world * dequantize
    C3D_Mtx worldQ, modelView;
    dequantizeWorld(worldQ.m, world.m, posScale, posOffset);
    Mtx_Multiply(&modelView, &view_, &worldQ);
    ++stats_.uniformUploads;
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, programs_[mode_].uModelView, &modelView);
}

void Renderer::drawModel(Model& model, const C3D_Mtx& world, const Tints* tints, const C3D_Tex* const* slotTex) {
    if (!model.loaded()) return;
    bindMode(kModeStatic);
    if (!beginDraw(model.boundsMin(), model.boundsMax(), world)) return;
    setModelView(world, model.posScale(), model.posOffset());

    const u8* vbase = static_cast<const u8*>(model.vertices());
    if (model.absoluteIndices() && batchState_.bindGeometry(model.vertices())) {
        C3D_BufInfo* buf = C3D_GetBufInfo();
        ++stats_.bufferBinds;
        BufInfo_Init(buf);
        BufInfo_Add(buf, vbase, kVertexStride, 3, 0x210);
    }
    if (!model.absoluteIndices()) batchState_.geometryValid=false;
    drawBatches(model, tints, slotTex, !model.absoluteIndices());
}

void Renderer::drawMorph(Model& model, const Anim& anim, u32 frameA, u32 frameB, float blend, const C3D_Mtx& world,
                         const Tints* tints, const C3D_Tex* const* slotTex) {
    // Morph draws bind one vertex buffer per model, so they need absolute indices.
    if (!model.loaded() || !anim.loaded() || !model.absoluteIndices() || anim.numVertices() != model.numVertices()) {
        return;
    }
    bindMode(kModeMorph);
    float lo[3], hi[3];
    for (int i = 0; i < 3; i++) {
        lo[i] = anim.posOffset()[i] - 32767.0f * anim.posScale()[i];
        hi[i] = anim.posOffset()[i] + 32767.0f * anim.posScale()[i];
    }
    if (!beginDraw(lo, hi, world)) return;
    setModelView(world, anim.posScale(), anim.posOffset());
    ++stats_.uniformUploads;
    C3D_FVUnifSet(GPU_VERTEX_SHADER, programs_[kModeMorph].uBlend, blend, 0.0f, 0.0f, 0.0f);

    C3D_BufInfo* buf = C3D_GetBufInfo();
    ++stats_.bufferBinds;
    batchState_.geometryValid=false;
    BufInfo_Init(buf);
    BufInfo_Add(buf, model.vertices(), kVertexStride, 3, 0x210);
    BufInfo_Add(buf, anim.frame(frameA), kMorphStride, 1, 0x3);
    BufInfo_Add(buf, anim.frame(frameB), kMorphStride, 1, 0x4);
    drawBatches(model, tints, slotTex, false);
}

void Renderer::drawBatches(Model& model, const Tints* tints, const C3D_Tex* const* slotTex, bool perBatchBuffer) {
    const int uTint = programs_[mode_].uTint;
    const u8* vbase = static_cast<const u8*>(model.vertices());

    for (u32 i = 0; i < model.numBatches(); i++) {
        const Batch& b = model.batch(i);

        const float *color = tints && b.tint ? tints->rgba[b.tint] : baseTint_;
        if (batchState_.uploadTint(color)) {
            ++stats_.uniformUploads;
            C3D_FVUnifSet(GPU_VERTEX_SHADER, uTint, color[0], color[1], color[2], color[3]);
        }

        const C3D_Tex* tex = b.texture == 0xFFFF ? nullptr : model.texture(b.texture);
        if (slotTex && b.texSlot && b.texSlot < kTexSlotCount && slotTex[b.texSlot]) tex = slotTex[b.texSlot];
        if (batchState_.bindTexture(tex)) {
            C3D_TexEnv* env = C3D_GetTexEnv(0);
            C3D_TexEnvInit(env);
            if (tex) {
                ++stats_.textureBinds;
                C3D_TexBind(0, const_cast<C3D_Tex*>(tex));
                C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
            } else {
                C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
            }
        }

        if (b.blend != batchState_.blend) {
            C3D_AlphaTest(b.blend == kBlendAlphaTest, GPU_GREATER, 127);
            C3D_DepthTest(true, GPU_GREATER, b.blend == kBlendAlpha ? GPU_WRITE_COLOR : GPU_WRITE_ALL);
            batchState_.blend = b.blend;
        }
        if (b.layer != batchState_.layer) {
            C3D_DepthMap(true, -1.0f, kLayerDepthBias * b.layer);
            batchState_.layer = b.layer;
        }
        const int cull = (b.flags & 1) ? GPU_CULL_NONE : GPU_CULL_BACK_CCW;
        if (cull != batchState_.cull) {
            C3D_CullFace(static_cast<GPU_CULLMODE>(cull));
            batchState_.cull = cull;
        }

        if (perBatchBuffer) {
            C3D_BufInfo* buf = C3D_GetBufInfo();
            ++stats_.bufferBinds;
            BufInfo_Init(buf);
            BufInfo_Add(buf, vbase + b.firstVertex * kVertexStride, kVertexStride, 3, 0x210);
        }
        C3D_DrawElements(GPU_TRIANGLES, b.numIndices, C3D_UNSIGNED_SHORT, model.indices() + b.firstIndex);

        stats_.drawCalls++;
        stats_.triangles += b.numIndices / 3;
    }

    // Keep cached state across models. endWorld restores the citro2d boundary.
}

} // namespace tt::render
