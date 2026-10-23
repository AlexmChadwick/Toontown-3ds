// Toontown 3DS - Phase 1 client: TTC playground scene, controllable toon, bottom-screen UI.
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <malloc.h>

#include <cmath>
#include <cstdio>

#include "core/autotest.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/screenshot.h"
#include "render/model.h"
#include "render/renderer.h"
#include "render/ui.h"
#include "world/player.h"

namespace {

using tt::render::Model;

struct Prop {
    Model* model;
    float x, y, z, headingDeg;
};

constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kSpawnX = 0.0f, kSpawnY = -20.0f; // TTC ground is z=0 here (plaza is raised)
constexpr int kStressCols = 8;
constexpr float kStressSpacing = 4.0f;

void propMatrix(const Prop& p, C3D_Mtx* out) {
    Mtx_Identity(out);
    Mtx_Translate(out, p.x, p.y, p.z, true);
    Mtx_RotateZ(out, p.headingDeg * kDegToRad, true);
}

void applyToonColors(const tt::ui::State& s, tt::render::Tints& t) {
    using namespace tt::render;
    t.setAll(1.0f, 1.0f, 1.0f);
    auto set = [&](TintSlot slot, int color) {
        for (int i = 0; i < 3; i++) t.rgba[slot][i] = tt::ui::kToonColors[color][i];
    };
    set(kTintHead, s.bodyColor);
    set(kTintArm, s.bodyColor);
    set(kTintLeg, s.bodyColor);
    set(kTintShirt, s.shirtColor);
    set(kTintSleeve, s.shirtColor);
    set(kTintBottom, s.bottomColor);
    // kTintGlove stays white (ToonDNA requires gloveColor 0).
}

} // namespace

int main() {
    gfxInitDefault();
    gfxSet3D(false); // stereo is opt-in only
    romfsInit();
    tt::log::init();

    bool isNew3ds = false;
    APT_CheckNew3DS(&isNew3ds);
    if (isNew3ds) osSetSpeedupEnable(true); // optional extra, never required
    TT_LOG("boot: %s", isNew3ds ? "New 3DS" : "Old 3DS");

    C3D_Init(2 * C3D_DEFAULT_CMDBUF_SIZE); // 512 KB; stress tests overflowed 256 KB at ~700 draws
    C3D_FrameRate(30.0f); // AGENTS.md: locked 30 fps on Old 3DS
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    tt::render::Renderer renderer;
    tt::ui::Ui ui;
    if (!renderer.init() || !ui.init()) TT_LOG("boot: renderer/ui init failed");

    Model terrain, gazebo, mailbox, toon;
    terrain.load("romfs:/baked/toontown_central.ttm");
    gazebo.load("romfs:/baked/gazebo.ttm");
    mailbox.load("romfs:/baked/mailbox_TT.ttm");
    toon.load("romfs:/baked/toon_dog_m.ttm");

    // Positions from phase_4/dna/toontown_central_sz.dna.
    const Prop props[] = {
        {&gazebo, -60.94f, -8.8f, -2.0f, -178.0f},
        {&mailbox, -95.0f, 87.0f, 0.5f, -75.0f},
        {&mailbox, -60.8037f, -84.8026f, 0.5f, 105.0f},
        {&mailbox, 57.0769f, -159.55f, 3.0f, 90.0f},
    };
    const tt::ui::MapMarker markers[] = {
        {-60.94f, -8.8f, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF)},
        {-95.0f, 87.0f, C2D_Color32(0xE0, 0x60, 0x20, 0xFF)},
        {-60.8f, -84.8f, C2D_Color32(0xE0, 0x60, 0x20, 0xFF)},
        {57.1f, -159.6f, C2D_Color32(0xE0, 0x60, 0x20, 0xFF)},
    };

    tt::world::Player player;
    player.reset(kSpawnX, kSpawnY, 0.0f, 0.0f);
    tt::ui::State uiState;
    tt::render::Tints tints;
    bool stereoActive = false;

    tt::autotest::Runner autotest;
    autotest.begin();
    const char* shotName = nullptr;

    u64 lastTick = svcGetSystemTick();
    u64 fpsTick = lastTick;
    int fpsFrames = 0;
    float fps = 0.0f;
    tt::render::FrameStats lastStats;
    float lastCmdBufUsage = 0.0f;

    TT_LOG("boot: heap %u KB used, linear %lu KB free", static_cast<unsigned>(mallinfo().uordblks / 1024),
           static_cast<unsigned long>(linearSpaceFree() / 1024));

    while (aptMainLoop()) {
        const u64 now = svcGetSystemTick();
        float dt = static_cast<float>(now - lastTick) / SYSCLOCK_ARM11;
        lastTick = now;
        if (dt > 0.1f) dt = 0.1f; // after HOME menu / sleep
        fpsFrames++;
        bool secondElapsed = false;
        if (now - fpsTick >= SYSCLOCK_ARM11) {
            fps = fpsFrames * static_cast<float>(SYSCLOCK_ARM11) / (now - fpsTick);
            fpsFrames = 0;
            fpsTick = now;
            secondElapsed = true;
        }

        hidScanInput();
        tt::world::Input in;
        in.held = hidKeysHeld();
        in.down = hidKeysDown();
        hidCircleRead(&in.pad);
        if (isNew3ds) hidCstickRead(&in.cstick);
        touchPosition touch;
        hidTouchRead(&touch);
        if (in.down & KEY_START) break;
        if (autotest.active() && secondElapsed) {
            TT_LOG("stats: [%s] fps %.1f cpu %.2f gpu %.2f cmdbuf %.0f%% skipped %lu draws %lu tris %lu toons %d pos %.1f,%.1f,%.1f h %.0f heap %uK "
                   "linfree %luK",
                   autotest.label(), fps, C3D_GetProcessingTime(), C3D_GetDrawingTime(), lastCmdBufUsage * 100.0f,
                   static_cast<unsigned long>(lastStats.modelsSkipped),
                   static_cast<unsigned long>(lastStats.drawCalls), static_cast<unsigned long>(lastStats.triangles),
                   uiState.stressToons, player.x(), player.y(), player.z(), player.headingDeg(),
                   static_cast<unsigned>(mallinfo().uordblks / 1024),
                   static_cast<unsigned long>(linearSpaceFree() / 1024));
        }
        autotest.apply(dt, in.pad, in.held, in.down, touch, uiState.stressToons);
        if (autotest.pendingShot()) shotName = autotest.pendingShot();

        ui.handleInput(in.down, touch, uiState);
        player.update(in, dt);
        applyToonColors(uiState, tints);

        const float slider = osGet3DSliderState();
        const bool wantStereo = uiState.stereo && slider > 0.0f;
        if (wantStereo != stereoActive) {
            gfxSet3D(wantStereo);
            stereoActive = wantStereo;
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        if (shotName) {
            // Previous frame is complete after FrameBegin; dump it before clearing.
            char path[96];
            snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_top.bin", shotName);
            tt::shot::dumpTarget(renderer.topLeft(), path);
            snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_bottom.bin", shotName);
            tt::shot::dumpTarget(bottom, path);
            shotName = nullptr;
        }
        renderer.resetStats();
        C3D_Mtx m;
        const int eyes = stereoActive ? 2 : 1;
        for (int e = 0; e < eyes; e++) {
            C3D_RenderTarget* target = e == 0 ? renderer.topLeft() : renderer.topRight();
            const int eyeSign = stereoActive ? (e == 0 ? -1 : 1) : 0;
            renderer.beginWorld(target, player.cameraEye(), player.cameraTarget(), slider / 3.0f, eyeSign);

            Mtx_Identity(&m);
            renderer.drawModel(terrain, m);
            for (const Prop& p : props) {
                propMatrix(p, &m);
                renderer.drawModel(*p.model, m);
            }
            player.worldMatrix(&m);
            renderer.drawModel(toon, m, &tints);
            // Stress toons: a grid in front of the player, facing it, so they are on screen.
            const float h = player.headingDeg() * kDegToRad;
            const float fx = -sinf(h), fy = cosf(h); // forward
            for (int i = 0; i < uiState.stressToons; i++) {
                const float side = (i % kStressCols - (kStressCols - 1) * 0.5f) * kStressSpacing;
                const float ahead = 10.0f + (i / kStressCols) * kStressSpacing;
                const Prop p{&toon, player.x() + fx * ahead + fy * side, player.y() + fy * ahead - fx * side,
                             player.z(), player.headingDeg() + 180.0f};
                propMatrix(p, &m);
                renderer.drawModel(toon, m, &tints);
            }
        }
        renderer.endWorld();
        lastStats = renderer.stats();
        lastCmdBufUsage = C3D_GetCmdBufUsage();

        // citro2d state must be re-bound after our custom shader.
        C2D_Prepare();
        const struct mallinfo mi = mallinfo();
        tt::ui::Context ctx{};
        ctx.playerX = player.x();
        ctx.playerY = player.y();
        ctx.playerHeadingDeg = player.headingDeg();
        ctx.zoneMinX = terrain.loaded() ? terrain.boundsMin()[0] : -300;
        ctx.zoneMinY = terrain.loaded() ? terrain.boundsMin()[1] : -300;
        ctx.zoneMaxX = terrain.loaded() ? terrain.boundsMax()[0] : 300;
        ctx.zoneMaxY = terrain.loaded() ? terrain.boundsMax()[1] : 300;
        ctx.markers = markers;
        ctx.numMarkers = sizeof(markers) / sizeof(markers[0]);
        ctx.fps = fps;
        ctx.cpuMs = C3D_GetProcessingTime();
        ctx.gpuMs = C3D_GetDrawingTime();
        ctx.drawCalls = lastStats.drawCalls;
        ctx.triangles = lastStats.triangles;
        ctx.modelsDrawn = lastStats.modelsDrawn;
        ctx.modelsCulled = lastStats.modelsCulled;
        ctx.heapUsedKB = mi.uordblks / 1024;
        ctx.heapBudgetKB = tt::mem::kHeapBudget / 1024;
        ctx.linearFreeKB = linearSpaceFree() / 1024;
        ctx.linearBudgetKB = tt::mem::kLinearBudget / 1024;
        ctx.vramFreeKB = vramSpaceFree() / 1024;
        ctx.isNew3ds = isNew3ds;
        ui.draw(bottom, uiState, ctx);

        C3D_FrameEnd(0);
    }

    toon.unload();
    mailbox.unload();
    gazebo.unload();
    terrain.unload();
    ui.shutdown();
    renderer.shutdown();
    C2D_Fini();
    C3D_Fini();
    tt::log::shutdown();
    romfsExit();
    gfxExit();
    return 0;
}
