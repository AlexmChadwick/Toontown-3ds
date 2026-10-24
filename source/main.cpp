// Toontown 3DS - Phase 1 client: TTC playground scene, controllable toon, bottom-screen UI.
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <malloc.h>

#include <cmath>
#include <cstdio>

#include "audio/audio.h"
#include "core/autotest.h"
#include "core/config.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/screenshot.h"
#include "net/client.h"
#include "net/connection.h"
#include "net/net_platform.h"
#include "render/model.h"
#include "render/renderer.h"
#include "render/ui.h"
#include "ui/balloons.h"
#include "ui/projection.h"
#include "ui/speedchat_data.h"
#include "world/player.h"
#include "world/remote_toons.h"
#include "world/toon_avatar.h"
#include "world/toon_cache.h"
#include "world/toon_dna.h"

namespace {

using tt::render::Model;

struct Prop {
    Model* model;
    float x, y, z, headingDeg;
};

constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kSpawnX = 0.0f, kSpawnY = -20.0f; // TTC ground is z=0 here (plaza is raised)
constexpr int kStressCols = 8;
constexpr int kMaxStressToons = 64;
// DistributedSmoothNode broadcast period: 0.2 s (5 Hz) while moving.
constexpr u32 kPosSendIntervalMs = 200;
constexpr float kHideToonCameraDistance = 2.5f;
constexpr int kMaxMapMarkers = 64;
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

    // Audio: silent (not fatal) if the DSP is unavailable, e.g. no sdmc:/3ds/dspfirm.cdc on hardware.
    tt::audio::init();
    tt::audio::playMusic("romfs:/baked/audio/ttc_nbrhood.tsnd", true);
    const tt::audio::SfxId sfxWalk = tt::audio::loadSfx("romfs:/baked/audio/footstep_walk.tsnd");
    const tt::audio::SfxId sfxRun = tt::audio::loadSfx("romfs:/baked/audio/footstep_run.tsnd");
    const tt::audio::SfxId sfxClick = tt::audio::loadSfx("romfs:/baked/audio/gui_click.tsnd");
    tt::audio::VoiceHandle footsteps = tt::audio::kNoVoice;
    tt::world::ToonAnim footstepAnim = tt::world::kAnimNeutral;

    C3D_Init(2 * C3D_DEFAULT_CMDBUF_SIZE); // 512 KB; stress tests overflowed 256 KB at ~700 draws
    C3D_FrameRate(30.0f); // AGENTS.md: locked 30 fps on Old 3DS
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    tt::render::Renderer renderer;
    tt::ui::Ui ui;
    if (!renderer.init() || !ui.init()) TT_LOG("boot: renderer/ui init failed");

    // Toontown Central playground assembled from DNA (tools/pipeline/bake_zone.py).
    Model terrain;
    terrain.load("romfs:/baked/ttc_sz.ttm");
    tt::world::CollisionWorld collision;
    collision.load("romfs:/baked/ttc_sz.ttc");
    // Toon parts/clothing shared by every toon (SD set, RomFS boot set fallback).
    static tt::world::ToonCache toonCache;
    toonCache.init();
    if (!toonCache.initFallback()) TT_LOG("boot: default toon failed to load");
    // Local toon: default dog until the server tells us our real DNA.
    static tt::world::ToonLook localLook;
    toonCache.acquire(tt::world::defaultToonDna(), tt::world::ToonLod::High, &localLook);
    bool localLookFromServer = false;
    tt::world::ToonAvatar localToon;
    localToon.setLook(&localLook, &toonCache.fallback());
    static tt::world::RemoteToons remoteToons;
    remoteToons.init(&toonCache);

    // Multiplayer: Open Toontown server from sdmc:/3ds/toontown3ds/server.txt.
    const tt::config::Settings settings = tt::config::load();
    static tt::net::Connection netConn;
    static tt::net::Client net;
    bool netStarted = false;
    if (settings.online && netConn.init()) {
        tt::net::ClientConfig cfg;
        cfg.host = settings.host;
        cfg.port = static_cast<uint16_t>(settings.port);
        cfg.playToken = settings.token;
        netStarted = net.start(netConn, cfg);
    }
    u32 lastPosSendMs = 0;
    bool wasMovingNet = false;
    const char* sentState = nullptr;
    static tt::world::ToonAvatar stressToons[kMaxStressToons];
    for (int i = 0; i < kMaxStressToons; i++) {
        stressToons[i].setLook(&localLook, &toonCache.fallback());
        stressToons[i].play(static_cast<tt::world::ToonAnim>(i % tt::world::kAnimCount), 1.0f);
        stressToons[i].update(i * 0.37f); // desynchronize
    }

    // Map markers: positions from phase_4/dna/toontown_central_sz.dna.
    const u32 kBuilding = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF), kTunnel = C2D_Color32(0xE0, 0x60, 0x20, 0xFF),
              kLandmark = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), kWater = C2D_Color32(0x40, 0x90, 0xE0, 0xFF);
    const tt::ui::MapMarker markers[] = {
        {24.6f, 24.9f, kBuilding},   // Toon HQ
        {-86.7f, -90.6f, kBuilding}, // Gag shop
        {-124.4f, 74.4f, kBuilding}, // Pet shop
        {106.3f, 160.8f, kBuilding}, // Clothing shop
        {57.2f, 38.7f, kBuilding},   // Bank
        {116.7f, 24.3f, kBuilding},  // Toon Hall
        {91.4f, -44.9f, kBuilding},  // Library
        {129.9f, -138.4f, kBuilding}, // School house
        {-60.9f, -8.8f, kLandmark},  // Gazebo
        {93.2f, -106.5f, kLandmark}, // Fountain
        {-68.0f, -2.0f, kWater},     // Fishing pond
        {-239.7f, 64.1f, kTunnel},   // Tunnels to the streets
        {-68.4f, -202.6f, kTunnel},
        {27.6f, 176.5f, kTunnel},
    };

    tt::world::Player player;
    player.setCollision(&collision);
    player.reset(kSpawnX, kSpawnY, 0.0f, 0.0f);
    tt::ui::State uiState;
    tt::ui::Balloons balloons;
    balloons.init();
    const char* myPhrase = nullptr;
    float myPhraseAge = 99.0f;
    int pendingSpeech = -1;
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
            TT_LOG("stats: [%s] fps %.1f cpu %.2f gpu %.2f cmdbuf %.0f%% skipped %lu draws %lu tris %lu toons %d remote %d pos %.1f,%.1f,%.1f h %.0f heap %uK "
                   "linfree %luK",
                   autotest.label(), fps, C3D_GetProcessingTime(), C3D_GetDrawingTime(), lastCmdBufUsage * 100.0f,
                   static_cast<unsigned long>(lastStats.modelsSkipped),
                   static_cast<unsigned long>(lastStats.drawCalls), static_cast<unsigned long>(lastStats.triangles),
                   uiState.stressToons, remoteToons.count(), player.x(), player.y(), player.z(), player.headingDeg(),
                   static_cast<unsigned>(mallinfo().uordblks / 1024),
                   static_cast<unsigned long>(linearSpaceFree() / 1024));
        }
        autotest.apply(dt, in.pad, in.held, in.down, touch, uiState.stressToons);
        if (autotest.pendingShot()) shotName = autotest.pendingShot();

        const int prevTab = uiState.tab;
        // Buttons the UI used (e.g. A/B in the SpeedChat list) must not also reach the world.
        in.down &= ~ui.handleInput(in.down, in.held, touch, uiState);
        if (uiState.tab != prevTab) tt::audio::playSfx(sfxClick);
        int speechMsg;
        while (ui.popSpeech(speechMsg)) {
            myPhrase = tt::sc::phraseText(speechMsg);
            myPhraseAge = 0.0f;
            tt::audio::playSfx(sfxClick);
            pendingSpeech = speechMsg;
        }
        myPhraseAge += dt;
        player.update(in, dt);
        float animRate = 1.0f;
        const tt::world::ToonAnim anim = player.animState(&animRate);
        localToon.play(anim, animRate);
        if (anim != footstepAnim) {
            // LocalAvatar: looping walk/run footsteps follow the motion state; none while airborne.
            tt::audio::stopVoice(footsteps);
            footsteps = tt::audio::kNoVoice;
            if (anim == tt::world::kAnimWalk) footsteps = tt::audio::playSfx(sfxWalk, 1.0f, 0.0f, true);
            if (anim == tt::world::kAnimRun) footsteps = tt::audio::playSfx(sfxRun, 1.0f, 0.0f, true);
            footstepAnim = anim;
        }
        localToon.update(dt);

        if (netStarted) {
            const u32 nowMs = tt::net::plat::nowMs();
            net.update(nowMs);
            tt::net::Event ev;
            while (net.pollEvent(ev)) remoteToons.handleEvent(ev, net);
            if (net.inZone() && !localLookFromServer) {
                tt::world::ToonDna dna;
                if (tt::world::parseNetString(net.avatarDna(), tt::net::kDnaSize, &dna)) {
                    toonCache.acquire(dna, tt::world::ToonLod::High, &localLook);
                    localLookFromServer = true;
                }
            } else if (!net.inZone()) {
                localLookFromServer = false;
            }
            if (net.inZone()) {
                // Our motion: setSmPosHpr at 5 Hz while moving, one final sample + setSmStop when
                // we stop, setAnimState on clip changes (DistributedSmoothNode / LocalToon).
                const bool movingNow = player.moving() || player.airborne() || anim != tt::world::kAnimNeutral;
                if (movingNow && nowMs - lastPosSendMs >= kPosSendIntervalMs) {
                    net.sendPosHpr(player.x(), player.y(), player.z(), player.headingDeg(), 0.0f, 0.0f);
                    lastPosSendMs = nowMs;
                } else if (!movingNow && wasMovingNet) {
                    net.sendPosHpr(player.x(), player.y(), player.z(), player.headingDeg(), 0.0f, 0.0f);
                    net.sendStop();
                }
                wasMovingNet = movingNow;
                // Anim *states*, as LocalAvatar sends them: "run" covers all ground locomotion
                // (others derive walk/run/neutral from our smoothed speed), "jumpAirborne" in the air.
                const char* netState = player.airborne() ? "jumpAirborne" : "run";
                if (netState != sentState) {
                    net.sendAnimState(netState, 1.0f);
                    sentState = netState;
                }
                if (pendingSpeech >= 0) net.sendSpeedChat(static_cast<uint16_t>(pendingSpeech));
            } else {
                sentState = nullptr; // resend after (re)entering
            }
        }
        pendingSpeech = -1;
        remoteToons.update(dt);
        for (int i = 0; i < uiState.stressToons && i < kMaxStressToons; i++) stressToons[i].update(dt);
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
            player.worldMatrix(&m);
            // Like upstream's camera-too-close fade: don't draw the toon around the camera.
            // The Toon tab colour picker is an offline preview; online the server's DNA wins.
            if (player.cameraDistance() > kHideToonCameraDistance) {
                localToon.draw(renderer, m, localLookFromServer ? nullptr : &tints);
            }
            remoteToons.draw(renderer, player.x(), player.y());
            // Stress toons: a grid in front of the player, facing it, so they are on screen.
            const float h = player.headingDeg() * kDegToRad;
            const float fx = -sinf(h), fy = cosf(h); // forward
            for (int i = 0; i < uiState.stressToons && i < kMaxStressToons; i++) {
                const float side = (i % kStressCols - (kStressCols - 1) * 0.5f) * kStressSpacing;
                const float ahead = 10.0f + (i / kStressCols) * kStressSpacing;
                const Prop p{nullptr, player.x() + fx * ahead + fy * side, player.y() + fy * ahead - fx * side,
                             player.z(), player.headingDeg() + 180.0f};
                propMatrix(p, &m);
                stressToons[i].draw(renderer, m, localLookFromServer ? nullptr : &tints);
            }
        }
        renderer.endWorld();
        lastStats = renderer.stats();
        lastCmdBufUsage = C3D_GetCmdBufUsage();

        // citro2d state must be re-bound after our custom shader.
        C2D_Prepare();
        {
            // Nametag + speech balloon over the head (toon height + 0.5, Avatar.py).
            C3D_Mtx proj, view;
            Mtx_PerspTilt(&proj, C3D_AngleFromDegrees(tt::render::Renderer::kFovYDeg), C3D_AspectRatioTop,
                          tt::render::Renderer::kNear, tt::render::Renderer::kFar, false);
            Mtx_LookAt(&view, player.cameraEye(), player.cameraTarget(), FVec3_New(0.0f, 0.0f, 1.0f), false);
            balloons.begin();
            float sx, sy, depth;
            if (tt::ui::projectToScreen(proj, view, player.x(), player.y(), player.z() + localLook.nametagHeight, &sx, &sy,
                                        &depth)) {
                const char* myName = netStarted && net.avatarName()[0] ? net.avatarName() : "Toon";
                balloons.addSpeaker(sx, sy, myName, myPhrase, myPhraseAge, depth);
            }
            remoteToons.addBalloons(balloons, proj, view);
            balloons.draw(renderer.topLeft());
            if (stereoActive) balloons.draw(renderer.topRight());
        }
        const struct mallinfo mi = mallinfo();
        tt::ui::Context ctx{};
        ctx.playerX = player.x();
        ctx.playerY = player.y();
        ctx.playerHeadingDeg = player.headingDeg();
        ctx.zoneMinX = terrain.loaded() ? terrain.boundsMin()[0] : -300;
        ctx.zoneMinY = terrain.loaded() ? terrain.boundsMin()[1] : -300;
        ctx.zoneMaxX = terrain.loaded() ? terrain.boundsMax()[0] : 300;
        ctx.zoneMaxY = terrain.loaded() ? terrain.boundsMax()[1] : 300;
        // Static landmarks plus other players (blue).
        tt::ui::MapMarker mapMarkers[kMaxMapMarkers];
        int numMapMarkers = 0;
        for (const tt::ui::MapMarker& mk : markers) mapMarkers[numMapMarkers++] = mk;
        float remoteXY[2 * kMaxMapMarkers];
        const int numRemote = remoteToons.mapPositions(remoteXY, kMaxMapMarkers - numMapMarkers);
        for (int i = 0; i < numRemote; i++) {
            mapMarkers[numMapMarkers++] = {remoteXY[2 * i], remoteXY[2 * i + 1], C2D_Color32(0x40, 0xF0, 0xFF, 0xFF)};
        }
        ctx.markers = mapMarkers;
        ctx.numMarkers = numMapMarkers;
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
        tt::audio::update();
    }

    if (netStarted) net.stop();
    netConn.shutdown();
    remoteToons.clear();
    toonCache.release(&localLook);
    toonCache.shutdown();
    terrain.unload();
    balloons.shutdown();
    ui.shutdown();
    renderer.shutdown();
    C2D_Fini();
    C3D_Fini();
    tt::audio::shutdown();
    tt::log::shutdown();
    romfsExit();
    gfxExit();
    return 0;
}
