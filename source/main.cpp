// Toontown 3DS - Phase 1 client: TTC playground scene, controllable toon, bottom-screen UI.
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <malloc.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "audio/audio.h"
#include "core/autotest.h"
#include "core/config.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/profile.h"
#include "core/screenshot.h"
#include "game/front_end.h"
#include "net/client.h"
#include "net/connection.h"
#include "net/net_platform.h"
#include "render/model.h"
#include "render/renderer.h"
#include "render/ui.h"
#include "ui/balloons.h"
#include "ui/hud.h"
#include "ui/projection.h"
#include "ui/speedchat_data.h"
#include "world/asset_loader.h"
#include "world/boat.h"
#include "world/cchar.h"
#include "world/suits.h"
#include "game/minigame_session.h"
#include "quest/quest_talk.h"
#include "world/battle.h"
#include "world/trolley.h"
#include "world/fishing.h"
#include "world/place.h"
#include "world/player.h"
#include "world/remote_toons.h"
#include "world/toon_avatar.h"
#include "world/toon_cache.h"
#include "world/toon_dna.h"
#include "world/zone_actors.h"

namespace {

using tt::render::Model;

// Street visgroup changes from tt::world::Zone, applied to the network once in zone.
struct StreetCell {
    bool pending = false;
    uint16_t cell = 0;
    uint16_t zones[64] = {};
    int count = 0;
};
StreetCell gStreetCell;
void onStreetCell(void*, uint16_t cell, const uint16_t* zones, int count) {
    gStreetCell.pending = true;
    gStreetCell.cell = cell;
    gStreetCell.count = count < 64 ? count : 64;
    for (int i = 0; i < gStreetCell.count; i++) gStreetCell.zones[i] = zones[i];
}

// APT lifecycle (HOME menu, sleep): keep the log on the SD card and remember that the
// console slept, because sockets do not survive sleep on hardware.
volatile bool gWokeFromSleep = false;
aptHookCookie gAptCookie;
void onApt(APT_HookType hook, void*) {
    if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP || hook == APTHOOK_ONEXIT) tt::log::flush();
    if (hook == APTHOOK_ONWAKEUP) gWokeFromSleep = true;
}

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
constexpr u32 kStartZone = 2000; // ToontownGlobals.ToontownCentral
constexpr float kFadeSec = 0.4f;  // each half of a zone transition
constexpr float kHideToonCameraDistance = 2.5f;
constexpr float kShadowScale = 0.4f; // ShadowCaster.initializeDropShadow
constexpr float kShadowGray = 0.5f;  // globalDropShadowGrayLevel
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
    tt::mem::linearLockInit();
    aptHook(&gAptCookie, onApt, nullptr);

    bool isNew3ds = false;
    APT_CheckNew3DS(&isNew3ds);
    if (isNew3ds) osSetSpeedupEnable(true); // optional extra, never required
    TT_LOG("boot: %s", isNew3ds ? "New 3DS" : "Old 3DS");

    // Audio: silent (not fatal) if the DSP is unavailable, e.g. no sdmc:/3ds/dspfirm.cdc on hardware.
    tt::audio::init();
    // Playground music per hood (toontown/safezone/*SafeZoneLoader.py musicFile): TTC is in
    // RomFS, the others on the SD set (tools/pipeline/bake_hoods.py).
    auto playHoodMusic = [](u32 zone) {
        const u32 hood = tt::world::hoodOf(zone);
        char path[80];
        if (hood == 2000) {
            snprintf(path, sizeof(path), "romfs:/baked/audio/ttc_nbrhood.tsnd");
        } else {
            snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/audio/%lu.tsnd", static_cast<unsigned long>(hood));
        }
        tt::audio::stopMusic(300);
        tt::audio::playMusic(path, true, 300);
    };
    playHoodMusic(2000);
    // Streets play the hood's town music (TownLoader.musicFile), interiors its activity
    // music (activityMusicFile); docs/design/audio.md. Falls back to the playground track.
    auto playPlaceMusic = [&](u32 zone) {
        const tt::world::PlaceKind kind = tt::world::placeKind(zone);
        if (kind == tt::world::PlaceKind::Playground) {
            playHoodMusic(zone);
            return;
        }
        const unsigned long hood = tt::world::hoodOf(zone);
        char path[80];
        snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/audio/%s_%lu.tsnd",
                 kind == tt::world::PlaceKind::Interior ? "in" : "st", hood);
        FILE* f = fopen(path, "rb");
        if (!f && kind == tt::world::PlaceKind::Interior) { // DG's interiors share the street track
            snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/audio/st_%lu.tsnd", hood);
            f = fopen(path, "rb");
        }
        if (!f) {
            playHoodMusic(zone);
            return;
        }
        fclose(f);
        tt::audio::stopMusic(300);
        tt::audio::playMusic(path, true, 300);
    };
    // Playground ambience: random one-shots (birds, gulls, wind) from *SafeZoneLoader.
    tt::audio::SfxId ambient[4] = {tt::audio::kInvalidSfx, tt::audio::kInvalidSfx, tt::audio::kInvalidSfx,
                                   tt::audio::kInvalidSfx};
    int numAmbient = 0;
    float ambientTimer = 6.0f;
    auto loadAmbient = [&](u32 zone) {
        for (int i = 0; i < numAmbient; i++) tt::audio::freeSfx(ambient[i]);
        numAmbient = 0;
        if (tt::world::placeKind(zone) != tt::world::PlaceKind::Playground) return;
        static const char* const kTt[] = {"romfs:/baked/audio/amb_tt_bird1.tsnd", "romfs:/baked/audio/amb_tt_bird2.tsnd",
                                          "romfs:/baked/audio/amb_tt_bird3.tsnd"};
        static const char* const kDd[] = {"sdmc:/3ds/toontown3ds/data/audio/amb_dd_seagull.tsnd"};
        static const char* const kBr[] = {"sdmc:/3ds/toontown3ds/data/audio/amb_br_wind1.tsnd",
                                          "sdmc:/3ds/toontown3ds/data/audio/amb_br_wind2.tsnd",
                                          "sdmc:/3ds/toontown3ds/data/audio/amb_br_wind3.tsnd"};
        static const char* const kDg[] = {"sdmc:/3ds/toontown3ds/data/audio/amb_dg_bird1.tsnd",
                                          "sdmc:/3ds/toontown3ds/data/audio/amb_dg_bird2.tsnd",
                                          "sdmc:/3ds/toontown3ds/data/audio/amb_dg_bird3.tsnd",
                                          "sdmc:/3ds/toontown3ds/data/audio/amb_dg_bird4.tsnd"};
        const char* const* list = nullptr;
        int n = 0;
        switch (zone) {
        case 2000: list = kTt, n = 3; break;
        case 1000: list = kDd, n = 1; break;
        case 3000: list = kBr, n = 3; break;
        case 5000: list = kDg, n = 4; break;
        default: break;
        }
        for (int i = 0; i < n; i++) {
            const tt::audio::SfxId id = tt::audio::loadSfx(list[i]);
            if (id != tt::audio::kInvalidSfx) ambient[numAmbient++] = id;
        }
    };
    loadAmbient(2000);
    const tt::audio::SfxId sfxDoorOpen = tt::audio::loadSfx("romfs:/baked/audio/door_open.tsnd");
    const tt::audio::SfxId sfxDoorClose = tt::audio::loadSfx("romfs:/baked/audio/door_close.tsnd");
    const tt::audio::SfxId sfxTeleport = tt::audio::loadSfx("romfs:/baked/audio/teleport.tsnd");
    const tt::audio::SfxId sfxChatOpen = tt::audio::loadSfx("romfs:/baked/audio/sc_open.tsnd");
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
    // The zone we're in (starts in the Toontown Central playground).
    static tt::world::Place place;
    place.load(kStartZone);

    // Toon parts/clothing shared by every toon (SD set, RomFS boot set fallback).
    static tt::world::ToonCache toonCache;
    toonCache.init();
    tt::world::AssetLoader::init(); // background .ttm/.tta loads (classic characters, boat)
    if (!toonCache.initFallback()) TT_LOG("boot: default toon failed to load");
    // Local toon: default dog until the server tells us our real DNA.
    static tt::world::ToonLook localLook;
    toonCache.acquire(tt::world::defaultToonDna(), tt::world::ToonLod::High, &localLook);
    bool localLookFromServer = false;
    tt::world::ToonAvatar localToon;
    localToon.setLook(&localLook, &toonCache.fallback());
    tt::ui::TailorPanel& tailor = ui.tailor();
    tailor.setPreview(&toonCache, &localLook);
    static tt::world::RemoteToons remoteToons;
    remoteToons.init(&toonCache);
    // NPCs and treasures of the playground (anchors from the zone's .pts sidecar).
    static tt::world::ZoneActors zoneActors;
    zoneActors.init(&toonCache, place.pointsPath(), "romfs:/baked/icecream.ttm");
    // Mickey and friends (DistributedCCharBase), walking their playground's graph.
    static tt::world::CCharActors cchars;
    static tt::world::BoatActor boat; // Donald's Dock ship (DistributedBoat) with Donald aboard
    cchars.setZone(kStartZone);
    // Fishing spots at the playground ponds (docs/design/fishing.md).
    static tt::world::Fishing fishing;
    cchars.setBoat(&boat);
    remoteToons.setBoat(&boat);
    // Cogs walking the street's suit paths (DistributedSuit); a box stands in for the model.
    static tt::world::SuitActors suits;
    suits.init();
    suits.setZone(kStartZone);
    Model dropShadow; // otp/avatar/ShadowCaster.py: drop_shadow at scale 0.4, black, gray level 0.5
    dropShadow.load("romfs:/baked/drop_shadow.ttm");
    u32 lastExitZone = 0;
    tt::world::ToonAnim loggedAnim = tt::world::kAnimNeutral;
    // Zone transitions fade the top screen to black (upstream: the toon walks into the
    // tunnel/door while the iris closes), load behind the black frame, then fade back in.
    u32 fadeTarget = 0;
    float fade = 0.0f; // 0 clear .. 1 black
    int fadeDir = 0;   // +1 closing, -1 opening
    bool fadeWalk = false;
    bool fadeDoor = false; // walking through a building door (door open/close sounds)
    unsigned hoodsAvailable = 0;
    for (int i = 0; i < tt::ui::kNumTeleportHoods; i++) {
        if (tt::world::Place::available(tt::ui::kTeleportHoods[i])) hoodsAvailable |= 1u << i;
    }       // exit trigger we're standing in (toast/zone change once per entry)
    const char* toast = nullptr;
    float toastAge = 99.0f;
    char toastBuf[96];
    const tt::audio::SfxId sfxTreasure = tt::audio::loadSfx("romfs:/baked/audio/treasure_grab.tsnd");
    u32 grabRequested = 0;
    u32 lastTouchedTreasure = 0;
    // Gag shop (docs/design/shop.md): walking into a clerk's tube sends avatarEnter once per contact.
    tt::ui::ShopPanel& shop = ui.shop();
    u32 lastTouchedClerk = 0;
    u32 lastTouchedTailor = 0;
    u32 lastTouchedQuestNpc = 0;
    // Talking to quest-giving NPC toons (ToonTasks; quest/quest_talk.*, ui/quest_panel.*).
    static tt::quest::Talk talk;
    tt::ui::Hud hud;
    hud.init();

    // Multiplayer: Open Toontown server from sdmc:/3ds/toontown3ds/server.txt.
    const tt::config::Settings settings = tt::config::load();
    static tt::net::Connection netConn;
    static tt::net::Client net;
    bool netStarted = false;
    bool netConnReady = false;
    // Starts the client; manual = the front end picks/creates the toon.
    auto startNet = [&](bool manual) {
        if (netStarted) return net.connect() || net.inZone();
        if (!netConnReady) netConnReady = netConn.init();
        if (!netConnReady) return false;
        tt::net::ClientConfig cfg;
        cfg.host = settings.host;
        cfg.port = static_cast<uint16_t>(settings.port);
        cfg.playToken = settings.token;
        cfg.manualAvatar = manual;
        netStarted = net.start(netConn, cfg);
        return netStarted;
    };
    u32 lastPosSendMs = 0;
    bool wasMovingNet = false;
    bool netOnBoat = false;   // what the server believes our setParent is (Donald's boat)
    bool boatRider = false;
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
    fishing.init(&player, &net);
    // Street cog battles (world/battle.*, docs/design/combat.md); the panel is ui/battle_panel.*.
    static tt::world::Battle battle;
    static tt::world::ToonClipSet battleClips; // the local toon's battle clips, loaded per fight
    // The playground trolley and the minigame it leads to (docs/design/trolley.md).
    static tt::world::TrolleyRide trolley;
    trolley.setZone(kStartZone);
    static tt::game::MinigameSession minigame;
    tt::world::BattleView battleView;
    fishing.setZone(kStartZone, place.pointsPath());
    player.setCollision(&place.collision());
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
    bool autotestGrabbed = false;
    bool autotestClerk = false, autotestBoarded = false, autotestBoardedTrolley = false, autotestBought = false;
    unsigned netInBps = 0, netOutBps = 0;
    uint32_t lastBytesIn = 0, lastBytesOut = 0;

    // Front end (title / pick / make a toon) unless the scripted autotest runs, which goes
    // straight into the world with automatic login (when online).
    static tt::game::FrontEnd frontEnd;
    frontEnd.init(&toonCache);
    bool inFrontEnd = !autotest.active() || autotest.frontEndScript();
    if (!inFrontEnd && settings.online) startNet(false);

    u64 lastTick = svcGetSystemTick();
    u64 fpsTick = lastTick;
    int fpsFrames = 0;
    float fps = 0.0f;
    tt::render::FrameStats lastStats;
    float lastCmdBufUsage = 0.0f;

    TT_LOG("boot: heap %u KB used, linear %lu KB free", static_cast<unsigned>(mallinfo().uordblks / 1024),
           static_cast<unsigned long>(linearSpaceFree() / 1024));

    tt::prof::init();
    while (aptMainLoop()) {
        tt::prof::frameBegin();
        tt::prof::logWindow(autotest.active() ? autotest.label() : "play");
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

        if (inFrontEnd) {
            if (netStarted) net.update(tt::net::plat::nowMs());
            autotest.apply(dt, in.pad, in.held, in.down, touch, uiState.stressToons);
            if (autotest.pendingShot()) shotName = autotest.pendingShot();
            const tt::game::Action action = frontEnd.update(in.down, in.held, touch, dt);
            if (action == tt::game::Action::StartOnline) {
                if (startNet(true)) {
                    frontEnd.setNet(&net);
                } else {
                    TT_LOG("net: could not start the network");
                }
            } else if (action == tt::game::Action::StartOffline || action == tt::game::Action::EnterWorld) {
                if (action == tt::game::Action::StartOffline && netStarted) net.disconnect();
                inFrontEnd = false;
                localLookFromServer = false;
                player.reset(kSpawnX, kSpawnY, 0.0f, 0.0f);
                tt::audio::playSfx(sfxClick);
            }
            if (in.down & (KEY_A | KEY_TOUCH)) tt::audio::playSfx(sfxClick);
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            if (shotName) {
                char path[96];
                snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_top.bin", shotName);
                tt::shot::dumpTarget(renderer.topLeft(), path);
                snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_bottom.bin", shotName);
                tt::shot::dumpTarget(bottom, path);
                shotName = nullptr;
            }
            frontEnd.drawTop(renderer);
            frontEnd.drawBottom(bottom);
            C3D_FrameEnd(0);
            tt::audio::update();
            continue;
        }
        if (gWokeFromSleep) {
            // The socket died with Wi-Fi during sleep and the server has (or soon will have)
            // timed us out. Reconnect now instead of waiting for the step timeout; manual
            // mode re-plays the toon we chose.
            gWokeFromSleep = false;
            TT_LOG("apt: woke from sleep");
            if (netStarted) net.forceReconnect("woke from sleep");
        }
        // Lost the server for good (e.g. logged in elsewhere): back to the title.
        if (netStarted && net.fatal()) {
            remoteToons.clear();
            fade = 0.0f; // a transition in progress is abandoned with the session
            fadeDir = 0;
            fadeTarget = 0;
            frontEnd.reset();
            inFrontEnd = true;
            continue;
        }
        if (secondElapsed && netStarted) {
            netInBps = net.bytesIn() - lastBytesIn;
            netOutBps = net.bytesOut() - lastBytesOut;
            lastBytesIn = net.bytesIn();
            lastBytesOut = net.bytesOut();
        }
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
        // A script started part-way (AUTOTEST_FROM) waits until we stand in a zone online.
        static float autotestSettle = 0.0f;
        if (!netStarted || net.inZone()) autotestSettle += dt;
        if (autotest.waiting() && autotestSettle > 3.0f) autotest.setReady();
        autotest.apply(dt, in.pad, in.held, in.down, touch, uiState.stressToons);
        if (autotest.pendingShot()) shotName = autotest.pendingShot();
        if (autotest.active() && !strncmp(autotest.label(), "exit: ", 6)) {
            // Test-only: stand inside the named trigger (tunnel owner / door owner prefix).
            const bool interiorDoor = !strcmp(autotest.label(), "exit: interior door");
            const bool streetDoor = !strcmp(autotest.label(), "exit: street door");
            const char* want = !strcmp(autotest.label(), "exit: tunnel 2132") ? "linktunnel_tt_2132"
                               : streetDoor                                   ? "tb"
                                                                              : "sz19:";
            const tt::world::CollisionWorld& cw = place.collision();
            for (int i = 0; i < cw.numTriggers(); i++) {
                const tt::world::CollTrigger& t = cw.trigger(i);
                if (interiorDoor ? !strstr(t.name, "door_trigger")
                                 : (strncmp(t.owner, want, strlen(want)) || !strstr(t.name, "trigger") ||
                                    (streetDoor && !strstr(t.name, "door")))) {
                    continue;
                }
                const float cx = (t.min[0] + t.max[0]) * 0.5f, cy = (t.min[1] + t.max[1]) * 0.5f;
                if (fabsf(player.x() - cx) > 0.5f || fabsf(player.y() - cy) > 0.5f) {
                    player.reset(cx, cy, t.min[2], player.headingDeg());
                    TT_LOG("autotest: moved into %s / %s", t.name, t.owner);
                }
                break;
            }
        }
        // Test-only: stand `dist` from a target, facing 30 degrees to its side so the toon
        // does not hide it from the camera.
        auto autotestFace = [&](float cx, float cy, float cz, float dist) {
            const float dx = player.x() - cx, dy = player.y() - cy, d = sqrtf(dx * dx + dy * dy);
            if (d > dist + 1.0f || d < dist - 1.0f) {
                const float ux = d > 0.1f ? dx / d : 0.0f, uy = d > 0.1f ? dy / d : -1.0f;
                player.reset(cx + ux * dist, cy + uy * dist, cz, atan2f(ux, -uy) / kDegToRad + 30.0f);
            }
        };
        if (autotest.active() && !strcmp(autotest.label(), "go fishing") && !fishing.active()) {
            // Test-only: walk onto the nearest fishing spot.
            float sx, sy, sz, sh;
            if (fishing.nearestSpot(player.x(), player.y(), &sx, &sy, &sz, &sh)) player.reset(sx, sy, sz + 0.5f, sh);
        }
        if (autotest.active() && !strcmp(autotest.label(), "board trolley") && !trolley.seated() &&
            !minigame.active() && !autotestBoardedTrolley) {
            // Test-only: walk into the TTC trolley station (trolley_sphere centre).
            float bx, by, bz;
            trolley.boardingPoint(&bx, &by, &bz);
            if (trolley.hasStation()) player.reset(bx, by, bz + 1.0f, player.headingDeg());
            autotestBoardedTrolley = true;
        }
        if (autotest.active() && !strncmp(autotest.label(), "toon hq", 7)) {
            // Test-only: teleport into the TTC Toon HQ (2520), walk into an officer, read every
            // page and take the first ToonTask offered.
            static u32 hqNextMs = 0;
            const u32 now = tt::net::plat::nowMs();
            if (place.zoneId() != 2520 && !uiState.teleportRequest && fadeDir == 0) uiState.teleportRequest = 2520;
            float npc[4];
            if (place.zoneId() == 2520 && !talk.locked() && zoneActors.shadowPositions(npc, 1) == 1 && now > hqNextMs) {
                player.reset(npc[0], npc[1] + 2.5f, npc[2], 180.0f);
                hqNextMs = now + 4000;
            }
            if (talk.phase() == tt::quest::Talk::Phase::Pages && now > hqNextMs - 2500) {
                talk.advance();
                hqNextMs = now + 4000;
            }
            if (talk.phase() == tt::quest::Talk::Phase::QuestChoice && !strcmp(autotest.label(), "toon hq: choose")) {
                talk.chooseQuest(0);
            }
        }
        if (autotest.active() && !strcmp(autotest.label(), "tailor visit") && autotest.stepTime() > 9.0f) {
            tailor.cancel(); // never leave the session open (DistributedNPCTailorAI unexpected-exit bug)
        }
        if (autotest.active() && !strcmp(autotest.label(), "tailor visit")) {
            // Test-only: teleport into the TTC Clothing Store (2521) and step onto the tailor.
            static u32 tailorNextMs = 0;
            const u32 now = tt::net::plat::nowMs();
            if (place.zoneId() != 2521 && !uiState.teleportRequest && fadeDir == 0) uiState.teleportRequest = 2521;
            float npc[4];
            if (place.zoneId() == 2521 && zoneActors.shadowPositions(npc, 1) == 1 && now > tailorNextMs) {
                player.reset(npc[0], npc[1] - 2.0f, npc[2], 0.0f);
                tailorNextMs = now + 5000;
            }
        }
        if (autotest.active() && !strncmp(autotest.label(), "go ", 3)) {
            // Test-only: teleport straight to a Cog HQ courtyard (upstream gets there by tunnel).
            static const char* lastGo = nullptr;
            const char* lbl = autotest.label();
            if (lbl != lastGo) {
                lastGo = lbl;
                uiState.teleportRequest = !strcmp(lbl, "go sellbot hq")   ? 11000
                                          : !strcmp(lbl, "go cashbot hq") ? 12000
                                          : !strcmp(lbl, "go lawbot hq")  ? 13000
                                                                          : 10000;
            }
        }
        if (autotest.active() && !strcmp(autotest.label(), "meet mickey")) {
            float cx, cy, cz;
            if (cchars.firstPosition(&cx, &cy, &cz)) autotestFace(cx, cy, cz, 9.0f);
        }
        if (autotest.active() && !strcmp(autotest.label(), "fight cog") && !battle.active()) {
            // Test-only: walk into the first cog; Battle::setAutoPlay picks the gags.
            float cx, cy, cz;
            if (suits.firstPosition(&cx, &cy, &cz)) player.reset(cx, cy - 0.5f, cz, 0.0f);
        }
        if (autotest.active() && !strcmp(autotest.label(), "see cog")) {
            float cx, cy, cz;
            if (suits.firstPosition(&cx, &cy, &cz)) autotestFace(cx, cy, cz, 9.0f);
            static u32 lastLog = 0;
            if (tt::net::plat::nowMs() - lastLog > 1000) {
                suits.logState();
                lastLog = tt::net::plat::nowMs();
            }
        }
        if (autotest.active() && !strcmp(autotest.label(), "visit clerk") && shop.active() && !autotestBought) {
            // Test-only: buy what we can afford of throw and squirt level 1, then Done.
            int bought = 0;
            for (int i = 0; i < 6; i++) bought += shop.buy(4, 0) + shop.buy(5, 0);
            TT_LOG("autotest: bought %d gags", bought);
            shop.finish(false);
            autotestBought = true;
        }
        if (autotest.active() && !strcmp(autotest.label(), "visit clerk") && !autotestClerk) {
            // Test-only: step onto the first NPC (the Gag Shop clerk) so the shop opens.
            float npc[4];
            if (zoneActors.shadowPositions(npc, 1) == 1) {
                player.reset(npc[0], npc[1] - 2.0f, npc[2], 0.0f);
                autotestClerk = true;
            }
        }
        if (autotest.active() && !strcmp(autotest.label(), "ride boat") && boat.active() && !autotestBoarded) {
            // Test-only: drop onto the middle of the deck.
            // Test-only: the deck floor (1000_boat.ttc, z 3.93) spans boat-local y 2..18; land mid-deck.
            const float bh = boat.heading() * kDegToRad;
            player.reset(boat.x() - 8.0f * sinf(bh), boat.y() + 8.0f * cosf(bh), boat.z() + 4.5f, boat.heading());
            autotestBoarded = true;
            TT_LOG("autotest: dropped on the boat at %.1f,%.1f,%.1f (deck %s, aboard %d)", double(player.x()),
                   double(player.y()), double(player.z()), boat.deck() ? "loaded" : "missing",
                   int(player.onPlatform()));
        }
        if (autotest.active() && !strcmp(autotest.label(), "see boat") && boat.active()) {
            autotestFace(boat.x(), boat.y(), boat.z() + 2.0f, 30.0f);
        }
        if (autotest.active() && !strcmp(autotest.label(), "grab treasure") && !autotestGrabbed) {
            // Test-only: stand on the nearest treasure so the grab path runs.
            float tx, ty, tz;
            if (zoneActors.nearestTreasure(player.x(), player.y(), &tx, &ty, &tz)) {
                player.reset(tx, ty, tz, player.headingDeg());
                autotestGrabbed = true;
                TT_LOG("autotest: moved onto treasure at %.1f,%.1f", double(tx), double(ty));
            }
        }

        const int prevTab = uiState.tab;
        // Buttons the UI used (e.g. A/B in the SpeedChat list) must not also reach the world.
        if (battle.active() && netStarted) {
            battle.fillView(net, tt::net::plat::nowMs(), &battleView);
            const tt::net::OwnStats& st = net.ownStats();
            ui.setBattle(&battleView, tt::ui::BattleGags{st.inventory, st.experience, st.trackAccess});
        } else {
            ui.setBattle(nullptr, tt::ui::BattleGags{});
        }
        ui.setTalk(&talk);
        in.down &= ~ui.handleInput(in.down, in.held, touch, uiState);
        tt::world::BattleAction battleAction;
        while (ui.battle().popAction(battleAction)) battle.perform(net, battleAction, tt::net::plat::nowMs());
        if (uiState.tab != prevTab) tt::audio::playSfx(uiState.tab == tt::ui::kTabChat ? sfxChatOpen : sfxClick);
        ambientTimer -= dt;
        if (ambientTimer <= 0.0f) {
            // Like the playgrounds' random ambient loops: a one-shot every 8-20 s, quietly,
            // only when voices are free so footsteps and UI clicks win.
            ambientTimer = 8.0f + static_cast<float>(rand() % 1200) / 100.0f;
            if (numAmbient > 0 && tt::audio::freeVoices() >= 3) {
                tt::audio::playSfx(ambient[rand() % numAmbient], 0.5f, static_cast<float>(rand() % 100 - 50) / 100.0f);
            }
        }
        int speechMsg;
        while (ui.popSpeech(speechMsg)) {
            if (!uiState.whisperTo) { // whispers are private: no balloon over our head
                myPhrase = tt::sc::phraseText(speechMsg);
                myPhraseAge = 0.0f;
            }
            tt::audio::playSfx(sfxClick);
            pendingSpeech = speechMsg;
        }
        myPhraseAge += dt;
        if (fadeDir > 0) {
            // Walking out through a tunnel or door: keep going forward, ignore the player.
            in.pad.dx = 0;
            in.pad.dy = fadeWalk ? 120 : 0;
            in.held &= ~(KEY_A | KEY_B);
            in.down &= ~(KEY_A | KEY_B);
        }
        tt::prof::lap(tt::prof::kInput);
        const circlePosition minigamePad = in.pad;
        talk.update(dt);
        if (talk.locked()) { // talking to an NPC: the dialog panel has the buttons
            in.pad.dx = 0;
            in.pad.dy = 0;
            in.held &= ~(KEY_A | KEY_B);
        }
        if (minigame.active() || trolley.seated()) { // seated or in a game: the toon isn't walking
            if (trolley.seated() && (in.down & KEY_B)) trolley.hopOff(net);
            in.pad.dx = 0;
            in.pad.dy = 0;
            in.held &= ~(KEY_A | KEY_B);
            in.down &= ~(KEY_A | KEY_B);
        }
        if (battle.active()) { // the battle places the toon; the circle pad and A/B do nothing
            in.pad.dx = 0;
            in.pad.dy = 0;
            in.held &= ~(KEY_A | KEY_B);
            in.down &= ~(KEY_A | KEY_B);
        }
        if (shop.active()) { // standing at the counter: no walking or jumping while the panel is open
            in.pad.dx = 0;
            in.pad.dy = 0;
            in.held &= ~(KEY_A | KEY_B);
        }
        if (tailor.active()) { // browsing clothes freezes the toon at the tailor
            in.pad.dx = 0;
            in.pad.dy = 0;
            in.held &= ~(KEY_A | KEY_B);
        }
        // Donald's boat: its deck collision carries the toon (Player::setPlatform); pose first.
        boat.setListener(player.cameraEye().x, player.cameraEye().y, player.cameraTarget().x, player.cameraTarget().y);
        boat.update(tt::net::plat::nowMs());
        player.setPlatform(boat.deck(), boat.x(), boat.y(), boat.z(), boat.heading());
        fishing.handleInput(in, dt, tt::net::plat::nowMs(), fadeDir == 0);
        player.update(in, dt);
        fishing.applyCamera();
        if (trolley.seated() || minigame.inScene()) {
            float sx, sy, sz, sh;
            C3D_FVec eye, target;
            if (minigame.inScene()) {
                minigame.toonPose(&sx, &sy, &sz, &sh);
                minigame.camera(&eye, &target);
            } else {
                trolley.seatPose(&sx, &sy, &sz, &sh);
                trolley.camera(&eye, &target);
            }
            player.reset(sx, sy, sz, sh);
            player.overrideCamera(eye, target);
        }
        if (battle.inBattle()) {
            tt::world::BattleActorPose lp;
            if (battle.localPose(&lp)) player.reset(lp.x, lp.y, lp.z, lp.h);
            float ex, ey, ez, tx, ty, tz;
            battle.cameraEye(&ex, &ey, &ez);
            battle.cameraTarget(&tx, &ty, &tz);
            player.overrideCamera(FVec3_New(ex, ey, ez), FVec3_New(tx, ty, tz));
        }
        if (player.onPlatform() != boatRider) boat.setRider(boatRider = player.onPlatform());
        float animRate = 1.0f;
        const tt::world::ToonAnim anim = player.animState(&animRate);
        if (!battle.inBattle()) localToon.play(anim, animRate); // in battle the movie picks the clip
        if (minigame.inScene()) localToon.play(minigame.toonRunning() ? tt::world::kAnimRun : tt::world::kAnimNeutral, 1.0f);
        if (battle.inBattle()) { // the battle movie animates the toon
            if (!battleClips.count && localLook.ready()) {
                battleClips.load(localLook, tt::world::kBattleClipNames, tt::world::kNumBattleClips);
            }
            tt::world::BattleActorPose lp;
            if (battle.localPose(&lp)) {
                if (lp.anim >= tt::world::kBattleAnimClip0) {
                    const int clip = lp.anim - tt::world::kBattleAnimClip0;
                    localToon.playClip(&battleClips, clip, clip == tt::world::kClipVictory);
                } else {
                    localToon.play(static_cast<tt::world::ToonAnim>(lp.anim), 1.0f);
                }
            }
        } else if (autotest.active() && !strncmp(autotest.label(), "clip: ", 6)) {
            // Test-only: play one battle clip on the local toon ("clip: throw", ...).
            if (!battleClips.count && localLook.ready()) {
                battleClips.load(localLook, tt::world::kBattleClipNames, tt::world::kNumBattleClips);
            }
            for (int i = 0; i < tt::world::kNumBattleClips; i++) {
                if (!strcmp(autotest.label() + 6, tt::world::kBattleClipNames[i])) localToon.playClip(&battleClips, i, true);
            }
        } else if (battleClips.count) {
            localToon.play(tt::world::kAnimNeutral, 1.0f);
            C3D_FrameSync(); // the last frame may still be drawing with them
            battleClips.unload();
        }
        if (anim != loggedAnim) { // anim changes are rare; useful when checking stairs/falls
            TT_LOG("player: anim %d at %.1f,%.1f,%.2f (ground %.2f)", int(anim), double(player.x()), double(player.y()),
                   double(player.z()), double(player.groundZ()));
            loggedAnim = anim;
        }
        if (anim != footstepAnim) {
            // LocalAvatar: looping walk/run footsteps follow the motion state; none while airborne.
            tt::audio::stopVoice(footsteps);
            footsteps = tt::audio::kNoVoice;
            if (anim == tt::world::kAnimWalk) footsteps = tt::audio::playSfx(sfxWalk, 1.0f, 0.0f, true);
            if (anim == tt::world::kAnimRun) footsteps = tt::audio::playSfx(sfxRun, 1.0f, 0.0f, true);
            footstepAnim = anim;
        }
        localToon.update(dt);
        tt::prof::lap(tt::prof::kPlayer);

        // Streets stream per visgroup; crossing into another server cell changes interest.
        place.update(player.x(), player.y(), player.z(), onStreetCell, nullptr);
        if (gStreetCell.pending && netStarted && net.inZone() && gStreetCell.cell != net.zoneId()) {
            if (net.changeStreetCell(gStreetCell.cell, gStreetCell.zones, gStreetCell.count)) {
                gStreetCell.pending = false;
            }
        } else if (gStreetCell.pending && (!netStarted || gStreetCell.cell == net.zoneId())) {
            gStreetCell.pending = false;
        }

        // Tunnels and building doors (QuietZone zone change). The new zone loads first, so
        // DNA-anchored objects (NPCs, doors) find their anchors when they generate.
        const tt::world::Exit exitHere = place.exitAt(player.x(), player.y(), player.z());
        u32 travelTarget = 0;
        if (fadeDir != 0 || minigame.active() || trolley.seated()) {
            // A transition is running, or we are on the trolley / away at a minigame: no exits.
        } else if (exitHere.kind != tt::world::Exit::None && exitHere.targetZone != lastExitZone) {
            if (!tt::world::Place::available(exitHere.targetZone)) {
                snprintf(toastBuf, sizeof(toastBuf), "Zone %lu isn't installed yet. Coming soon!",
                         static_cast<unsigned long>(exitHere.targetZone));
                TT_LOG("place: exit %s -> zone %lu (not installed)", exitHere.name,
                       static_cast<unsigned long>(exitHere.targetZone));
                toast = toastBuf;
                toastAge = 0.0f;
            } else {
                travelTarget = exitHere.targetZone;
            }
        }
        if (uiState.teleportRequest && fadeDir == 0) {
            // Map tab teleport to a playground (upstream: Shticker Book map page).
            if (tt::world::Place::available(uiState.teleportRequest)) travelTarget = uiState.teleportRequest;
            fadeWalk = false;
        } else if (travelTarget) {
            fadeWalk = true;
        }
        uiState.teleportRequest = 0;
        if (travelTarget && travelTarget != place.zoneId() && (!netStarted || net.inZone())) {
            fadeTarget = travelTarget; // start closing; travel once the screen is black
            fadeDir = 1;
            fadeDoor = fadeWalk && exitHere.kind == tt::world::Exit::Door;
            if (fadeDoor) tt::audio::playSfx(sfxDoorOpen);       // DistributedDoor: door opens
            else if (!fadeWalk) tt::audio::playSfx(sfxTeleport); // Toon.getTeleportOutTrack
        }
        travelTarget = 0;
        if (fadeDir != 0) {
            const float before = fade;
            fade += fadeDir * dt / kFadeSec;
            if (fadeDir > 0 && fade >= 1.0f) {
                fade = 1.0f;
                // Travel on the frame after one fully black frame has been shown.
                if (before >= 1.0f) {
                    travelTarget = fadeTarget;
                    fadeDir = -1;
                }
            } else if (fadeDir < 0 && fade <= 0.0f) {
                fade = 0.0f;
                fadeDir = 0;
            }
        }
        if (travelTarget && travelTarget != place.zoneId() && (!netStarted || net.inZone())) {
            const u32 fromZone = place.zoneId();
            remoteToons.clear();
            if (place.load(travelTarget)) {
                zoneActors.loadPoints(place.pointsPath());
                cchars.setZone(travelTarget);
                fishing.setZone(travelTarget, place.pointsPath());
                trolley.setZone(travelTarget);
                suits.setZone(travelTarget);
                float ax, ay, az, ah;
                place.arrival(fromZone, &ax, &ay, &az, &ah);
                player.reset(ax, ay, az, ah);
                if (netStarted) net.changeZone(travelTarget);
                place.finishLoading(ax, ay, az, onStreetCell, nullptr);
                lastExitZone = place.exitAt(ax, ay, az).targetZone; // don't bounce straight back
                if (tt::world::hoodOf(travelTarget) != tt::world::hoodOf(fromZone) ||
                    tt::world::placeKind(travelTarget) != tt::world::placeKind(fromZone)) {
                    playPlaceMusic(travelTarget);
                }
                loadAmbient(travelTarget);
                if (fadeDoor) tt::audio::playSfx(sfxDoorClose); // the door shuts behind us
                continue;
            }
            place.load(fromZone); // fall back to where we were
        }
        lastExitZone = exitHere.kind != tt::world::Exit::None ? exitHere.targetZone : 0;
        toastAge += dt;
        if (toastAge > 3.0f) toast = nullptr;

        tt::prof::lap(tt::prof::kPlace);
        if (netStarted) {
            const u32 nowMs = tt::net::plat::nowMs();
            net.update(nowMs);
            tt::net::Event ev;
            while (net.pollEvent(ev)) {
                remoteToons.handleEvent(ev, net);
                zoneActors.handleEvent(ev, net);
                cchars.handleEvent(ev, net, nowMs);
                fishing.handleEvent(ev);
                battle.handleEvent(ev, net, nowMs);
                trolley.handleEvent(ev, net, nowMs);
                if (ev.type == tt::net::EventType::NpcMovie) {
                    tt::net::NpcMovie movie;
                    while (net.pollNpcMovie(movie)) talk.onMovie(movie);
                } else if (ev.type == tt::net::EventType::NpcFree) {
                    talk.onFree(ev.doId);
                } else if (ev.type == tt::net::EventType::NpcDespawn) {
                    talk.onNpcGone(ev.doId);
                }
                minigame.handleEvent(ev, net, nowMs);
                boat.handleEvent(ev, net);
                suits.handleEvent(ev, net, nowMs);
                if (ev.type == tt::net::EventType::ShopMovie) {
                    shop.onMovie(net, nowMs);
                    if (const char* note = shop.takeNotice()) {
                        toast = note;
                        toastAge = 0.0f;
                    }
                }
                if (ev.type == tt::net::EventType::TailorMovie) {
                    tailor.onMovie(net, nowMs);
                    if (const char* note = tailor.takeNotice()) {
                        toast = note;
                        toastAge = 0.0f;
                    }
                }
                if (ev.type == tt::net::EventType::TreasureGone && ev.doId == grabRequested) {
                    tt::audio::playSfx(sfxTreasure);
                    grabRequested = 0;
                }
            }
            shop.update(net, nowMs);
            tailor.update(net, nowMs);
            {
                // Whispers and friend invites (net/client_friends.cpp) as top-screen toasts.
                tt::net::FriendEvent fe;
                while (net.pollFriendEvent(fe)) {
                    if (fe.type == tt::net::FriendEventType::Whisper && net.numWhispers() > 0) {
                        const tt::net::Whisper& w = net.whisperAt(net.numWhispers() - 1);
                        const char* said = w.kind == tt::net::WhisperKind::SpeedChat ? tt::sc::phraseText(w.index) : w.text;
                        snprintf(toastBuf, sizeof(toastBuf), "%s whispers: %s", w.name[0] ? w.name : "A toon",
                                 said ? said : "...");
                        toast = toastBuf;
                        toastAge = 0.0f;
                    } else if (fe.type == tt::net::FriendEventType::InviteIn && net.pendingInvite()) {
                        snprintf(toastBuf, sizeof(toastBuf), "%s wants to be friends!", net.pendingInvite()->name);
                        toast = toastBuf;
                        toastAge = 0.0f;
                    }
                }
            }
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
                const bool movingNow = !battle.active() &&
                                       (player.moving() || player.airborne() || anim != tt::world::kAnimNeutral);
                // On the boat (DDPlayground.enterOnBoat): setParent(SPDonaldsBoat), then poses are boat-local.
                float nx, ny, nz, nh;
                player.netPose(&nx, &ny, &nz, &nh);
                const bool reparent = player.onPlatform() != netOnBoat;
                if (reparent) {
                    net.sendSetParent(player.onPlatform() ? tt::net::kParentDonaldsBoat : tt::net::kParentRender);
                    netOnBoat = player.onPlatform();
                }
                if (movingNow && nowMs - lastPosSendMs >= kPosSendIntervalMs) {
                    net.sendPosHpr(nx, ny, nz, nh, 0.0f, 0.0f);
                    lastPosSendMs = nowMs;
                } else if ((!movingNow && wasMovingNet) || (reparent && !movingNow)) {
                    net.sendPosHpr(nx, ny, nz, nh, 0.0f, 0.0f);
                    net.sendStop();
                } else if (reparent) {
                    net.sendPosHpr(nx, ny, nz, nh, 0.0f, 0.0f); // the new frame's pose right after setParent
                    lastPosSendMs = nowMs;
                }
                if (netOnBoat) net.setOwnWorldPos(player.x(), player.y(), player.z()); // chat targets use world
                wasMovingNet = movingNow;
                // Anim *states*, as LocalAvatar sends them: "run" covers all ground locomotion
                // (others derive walk/run/neutral from our smoothed speed), "jumpAirborne" in the air.
                const char* netState = player.airborne() ? "jumpAirborne" : "run";
                if (netState != sentState) {
                    net.sendAnimState(netState, 1.0f);
                    sentState = netState;
                }
                if (pendingSpeech >= 0) {
                    // A whisper goes only to that toon (DistributedToon.setWhisperSCFrom), said aloud otherwise.
                    if (uiState.whisperTo) net.sendWhisperSC(uiState.whisperTo, static_cast<uint16_t>(pendingSpeech));
                    else net.sendSpeedChat(static_cast<uint16_t>(pendingSpeech));
                }
                // Entering a treasure's sphere asks the AI for it once (DistributedTreasure.
                // handleEnterSphere). The AI refuses while laff is full (validAvatar: hp < maxHp).
                const u32 touching = zoneActors.touchingTreasure(player.x(), player.y(), player.z());
                if (touching && touching != lastTouchedTreasure) {
                    net.sendRequestGrab(touching);
                    grabRequested = touching;
                }
                lastTouchedTreasure = touching;
                const u32 clerk = zoneActors.touchingNpc(player.x(), player.y(), player.z(), net,
                                                         tt::dc::DistributedNPCClerk::kClassId);
                if (clerk && clerk != lastTouchedClerk && !shop.active() && fadeDir == 0) net.sendClerkAvatarEnter(clerk);
                lastTouchedClerk = clerk;
                const u32 tailorNpc = zoneActors.touchingNpc(player.x(), player.y(), player.z(), net,
                                                             tt::dc::DistributedNPCTailor::kClassId);
                if (tailorNpc && tailorNpc != lastTouchedTailor && !tailor.active() && !shop.active() && fadeDir == 0)
                    net.sendTailorAvatarEnter(tailorNpc);
                lastTouchedTailor = tailorNpc;
                // Quest givers: walking into one starts the conversation (DistributedNPCToon.avatarEnter).
                u32 giver = zoneActors.touchingNpc(player.x(), player.y(), player.z(), net,
                                                   tt::dc::DistributedNPCToon::kClassId);
                if (!giver) {
                    giver = zoneActors.touchingNpc(player.x(), player.y(), player.z(), net,
                                                   tt::dc::DistributedNPCSpecialQuestGiver::kClassId);
                }
                if (giver && giver != lastTouchedQuestNpc && !talk.locked() && !shop.active() && fadeDir == 0) {
                    talk.setLocal(net.avatarId(), net.avatarName());
                    talk.begin(giver);
                    TT_LOG("quest: talking to NPC %u", unsigned(giver));
                }
                lastTouchedQuestNpc = giver;
                tt::quest::TalkAction ta;
                while (talk.popAction(ta)) {
                    switch (ta.kind) {
                    case tt::quest::TalkAction::AvatarEnter: net.sendNpcAvatarEnter(ta.npcDoId); break;
                    case tt::quest::TalkAction::ChooseQuest:
                        net.sendChooseQuest(ta.npcDoId, static_cast<uint16_t>(ta.value));
                        break;
                    case tt::quest::TalkAction::ChooseTrack: net.sendChooseTrack(ta.npcDoId, static_cast<int8_t>(ta.value)); break;
                    case tt::quest::TalkAction::MovieDone: net.sendNpcMovieDone(ta.npcDoId); break;
                    }
                }
            } else {
                sentState = nullptr; // resend after (re)entering
                netOnBoat = false;   // entering a zone sends setParent(render)
            }
        }
        pendingSpeech = -1;
        tt::prof::lap(tt::prof::kNet);
        remoteToons.update(dt);
        tt::prof::lap(tt::prof::kRemote);
        zoneActors.update(dt);
        cchars.update(dt, tt::net::plat::nowMs(), &place.collision());
        suits.update(dt, tt::net::plat::nowMs(), &place.collision());
        if (netStarted && net.inZone()) {
            const u32 nowMs = tt::net::plat::nowMs();
            // Bumping into a walking cog starts a fight (DistributedSuit.handleEnterCollision);
            // walking into someone else's fight joins it.
            if (!battle.active() && fadeDir == 0 && !shop.active() && !fishing.active() && !minigame.active() &&
                !trolley.seated()) {
                if (const u32 cog = suits.touching(player.x(), player.y(), player.z())) {
                    float cx, cy, cz, ch;
                    if (suits.pose(cog, &cx, &cy, &cz, &ch)) {
                        battle.requestBattle(net, cog, cx, cy, cz, ch, player.x(), player.y(), player.z(),
                                             player.headingDeg(), nowMs);
                    }
                }
                battle.checkJoin(net, player.x(), player.y(), player.z(), player.headingDeg(), nowMs);
            }
            if (!battle.active() && !minigame.active() && fadeDir == 0) {
                trolley.update(net, player.x(), player.y(), player.z(), net.ownStats().hp, nowMs);
            }
            {
                int seat;
                if (trolley.popExit(&seat)) { // hopped off: stand beside the station
                    float x, y, z, h;
                    trolley.exitPose(seat, &x, &y, &z, &h);
                    player.reset(x, y, z, h);
                }
                u32 mgZone;
                u16 mgGame;
                if (trolley.popMinigame(&mgZone, &mgGame)) {
                    minigame.begin(net, mgZone, mgGame, trolley.hood(), nowMs);
                    player.setCollision(nullptr); // the stage is flat; the playground stays loaded
                    remoteToons.clear();
                }
                minigame.setAutoPlay(autotest.active()); // test-only: run under the fruit
                minigame.update(net, minigamePad, dt, nowMs);
                if (const u32 home = minigame.popReturn()) {
                    // Back from the game: teleport into the playground (Playground.enterTeleportIn).
                    player.setCollision(&place.collision());
                    net.changeZone(home);
                    float ax, ay, az, ah;
                    place.arrival(0, &ax, &ay, &az, &ah);
                    player.reset(ax, ay, az, ah);
                    net.sendSetParent(tt::net::kParentRender);
                    tt::audio::playSfx(sfxTeleport);
                }
                if (const char* m = trolley.popMessage()) {
                    toast = m;
                    toastAge = 0.0f;
                }
                const int left = trolley.seated() ? trolley.countdown(net, nowMs) : -1;
                if (left >= 0) {
                    snprintf(toastBuf, sizeof(toastBuf), "The trolley leaves in %d...  (B: hop off)", left);
                    toast = toastBuf;
                    toastAge = 0.0f;
                }
            }
            battle.setAutoPlay(autotest.active());
            battle.update(net, nowMs, dt, tt::world::CogPoseSource{tt::world::SuitActors::poseThunk, &suits});
            for (int i = 0; i < battle.numSuitPoses(); i++) {
                const tt::world::BattleActorPose& p = battle.suitPose(i);
                suits.overridePose(p.doId, p.x, p.y, p.z + p.hop, p.h, p.hidden, p.flash);
            }
            if (const char* m = battle.popMessage()) {
                toast = m;
                toastAge = 0.0f;
            }
            if (battle.popWentSad()) uiState.teleportRequest = tt::world::hoodOf(place.zoneId()); // back to the playground
        }
        for (int i = 0; i < uiState.stressToons && i < kMaxStressToons; i++) stressToons[i].update(dt);
        applyToonColors(uiState, tints);
        tt::prof::lap(tt::prof::kActors);

        const float slider = osGet3DSliderState();
        const bool wantStereo = uiState.stereo && slider > 0.0f;
        if (wantStereo != stereoActive) {
            gfxSet3D(wantStereo);
            stereoActive = wantStereo;
        }

        tt::prof::lap(tt::prof::kActors); // stereo toggle
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        tt::prof::lap(tt::prof::kWait);
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

            if (minigame.active()) {
                minigame.draw(renderer); // the playground is hidden while we are away
            } else {
                place.draw(renderer, player.cameraEye());
            }
            tt::prof::lap(tt::prof::kDrawPlace);

            // Drop shadows under every toon, on the ground just above it (FloorOffset 0.025).
            {
                float pos[4 * (tt::net::kMaxRemoteToons + tt::net::kMaxNpcs + tt::net::kMaxCChars + tt::net::kMaxSuits + 1)];
                int n = 0;
                pos[0] = player.x();
                pos[1] = player.y();
                pos[2] = player.groundZ();
                pos[3] = localLook.valid() ? localLook.scale : 1.0f;
                n = 1;
                n += remoteToons.shadowPositions(pos + 4 * n, tt::net::kMaxRemoteToons);
                n += zoneActors.shadowPositions(pos + 4 * n, tt::net::kMaxNpcs);
                n += cchars.shadowPositions(pos + 4 * n, tt::net::kMaxCChars);
                n += suits.shadowPositions(pos + 4 * n, tt::net::kMaxSuits);
                renderer.setBaseTint(0.0f, 0.0f, 0.0f, kShadowGray);
                for (int i = 0; i < n; i++) {
                    const float sc = kShadowScale * pos[4 * i + 3];
                    Mtx_Identity(&m);
                    Mtx_Translate(&m, pos[4 * i], pos[4 * i + 1], pos[4 * i + 2] + 0.05f, true);
                    Mtx_Scale(&m, sc, sc, sc);
                    renderer.drawModel(dropShadow, m);
                }
                renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
            }
            tt::prof::lap(tt::prof::kDrawShadow);
            player.worldMatrix(&m);
            // Like upstream's camera-too-close fade: don't draw the toon around the camera.
            // The Toon tab colour picker is an offline preview; online the server's DNA wins.
            if (player.cameraDistance() > kHideToonCameraDistance) {
                localToon.draw(renderer, m, localLookFromServer ? nullptr : &tints);
            }
            remoteToons.draw(renderer, player.x(), player.y());
            tt::prof::lap(tt::prof::kDrawToons);
            zoneActors.draw(renderer, player.cameraEye());
            boat.draw(renderer);
            cchars.draw(renderer);
            if (netStarted) trolley.draw(renderer, net, tt::net::plat::nowMs());
            fishing.draw(renderer);
            suits.draw(renderer);
            if (autotest.active() && !strcmp(autotest.label(), "cog showcase")) {
                // Test-only: four real Cogs (one per department) in front of the toon.
                static const char* const kShow[4] = {"f", "bf", "sc", "cc"};
                const float hh = player.headingDeg() * kDegToRad;
                for (int i = 0; i < 4; i++) {
                    const float side = (i - 1.5f) * 4.0f, ahead = 12.0f;
                    suits.drawShowcase(renderer, kShow[i], player.x() - sinf(hh) * ahead + cosf(hh) * side,
                                       player.y() + cosf(hh) * ahead + sinf(hh) * side, player.z(),
                                       player.headingDeg() + 180.0f, i % 2 == 1, static_cast<float>(tt::net::plat::nowMs() % 100000) / 1000.0f);
                }
            }
            tt::prof::lap(tt::prof::kDrawActors);
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
            tt::prof::lap(tt::prof::kDrawToons);
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
            {
                u32 talker = 0;
                const char* said = talk.speech(&talker);
                zoneActors.setSpeech(talker, said);
            }
            zoneActors.addBalloons(balloons, proj, view);
            cchars.addBalloons(balloons, proj, view);
            suits.addBalloons(balloons, proj, view);
            for (int i = 0; i < battle.numPopups(); i++) { // damage numbers over the fighters
                const tt::world::BattlePopup& p = battle.popup(i);
                float sx, sy, depth;
                if (tt::ui::projectToScreen(proj, view, p.x, p.y, p.z, &sx, &sy, &depth)) {
                    balloons.addSpeaker(sx, sy - p.rise * p.age, p.text, nullptr, 99.0f, 0.0f, p.color);
                }
            }
            balloons.draw(renderer.topLeft());
            if (stereoActive) balloons.draw(renderer.topRight());
            tt::prof::lap(tt::prof::kBalloons);
            tt::ui::HudValues hv;
            hv.online = netStarted && net.inZone();
            if (hv.online) {
                const tt::net::OwnStats& st = net.ownStats();
                hv.hp = st.hp;
                hv.maxHp = st.maxHp;
                hv.money = st.money;
                hv.maxMoney = st.maxMoney;
                for (int i = 0; i < 3; i++) hv.headRgb[i] = localLook.tints.rgba[tt::render::kTintHead][i];
            }
            hv.toast = toast;
            hud.draw(renderer.topLeft(), hv);
            if (fade > 0.0f) {
                const u32 black = C2D_Color32(0, 0, 0, static_cast<u8>(fade * 255.0f));
                for (int e = 0; e < (stereoActive ? 2 : 1); e++) {
                    C2D_SceneBegin(e == 0 ? renderer.topLeft() : renderer.topRight());
                    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
                    C2D_DrawRectSolid(0, 0, 0.9f, 400, 240, black);
                }
                C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL); // as hud.cpp / balloons.cpp restore it
            }
            tt::prof::lap(tt::prof::kHud);
        }
        const struct mallinfo mi = mallinfo();
        tt::ui::Context ctx{};
        ctx.playerX = player.x();
        ctx.playerY = player.y();
        ctx.playerHeadingDeg = player.headingDeg();
        ctx.zoneMinX = place.boundsMin()[0];
        ctx.zoneMinY = place.boundsMin()[1];
        ctx.zoneMaxX = place.boundsMax()[0];
        ctx.zoneMaxY = place.boundsMax()[1];
        // Static landmarks plus other players (blue).
        tt::ui::MapMarker mapMarkers[kMaxMapMarkers];
        int numMapMarkers = 0;
        if (place.zoneId() == kStartZone) {
            for (const tt::ui::MapMarker& mk : markers) mapMarkers[numMapMarkers++] = mk;
        }
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
        ctx.zoneId = place.zoneId();
        ctx.currentZone = place.zoneId();
        ctx.currentHood = tt::world::hoodOf(place.zoneId());
        ctx.hoodsAvailable = hoodsAvailable;
        if (netStarted) {
            ctx.netState = tt::net::stateName(net.state());
            ctx.netInBps = netInBps;
            ctx.netOutBps = netOutBps;
            ctx.remoteToons = remoteToons.count();
            const tt::net::OwnStats& st = net.ownStats();
            if (st.hasGags) {
                ctx.gagExp = st.experience;
                ctx.gagCount = st.inventory;
                ctx.trackAccess = st.trackAccess;
                ctx.maxCarry = st.maxCarry;
            }
        }
        {
            // Baked minimap per exterior zone file: playgrounds by id, street cells by branch.
            const u32 z = place.zoneId();
            const tt::world::PlaceKind kind = tt::world::placeKind(z);
            ui.setMapZone(kind == tt::world::PlaceKind::Interior ? 0
                          : kind == tt::world::PlaceKind::Street ? z - z % 100 : z);
        }
        ctx.fishing = fishing.view();
        if (netStarted && net.inZone()) {
            ctx.quests = &net.ownQuests();
            for (int i = 0; i < tt::net::kMaxRemoteToons && ctx.numNearby < 8; i++) {
                const tt::net::RemoteToon& t = net.remoteToonSlot(i);
                if (!t.active) continue;
                ctx.nearbyIds[ctx.numNearby] = t.doId;
                ctx.nearbyNames[ctx.numNearby++] = t.name;
            }
        }
        if (minigame.active()) {
            ctx.infoTitle = minigame.title();
            for (int i = 0; i < 3; i++) ctx.infoLines[i] = minigame.line(i);
        }
        ui.draw(bottom, uiState, ctx);
        tt::prof::lap(tt::prof::kBottomUi);

        C3D_FrameEnd(0);
        tt::prof::lap(tt::prof::kFrameEnd);
        tt::audio::update();
        tt::prof::lap(tt::prof::kAudio);
    }

    frontEnd.shutdown();
    hud.shutdown();
    zoneActors.shutdown();
    cchars.shutdown();
    fishing.shutdown();
    minigame.shutdown();
    trolley.shutdown();
    boat.shutdown();
    tt::world::AssetLoader::shutdown();
    suits.shutdown();
    if (netStarted) net.stop();
    if (netConnReady) netConn.shutdown();
    remoteToons.clear();
    toonCache.release(&localLook);
    toonCache.shutdown();
    dropShadow.unload();
    place.unload();
    balloons.shutdown();
    ui.shutdown();
    renderer.shutdown();
    C2D_Fini();
    C3D_Fini();
    tt::audio::shutdown();
    aptUnhook(&gAptCookie);
    tt::log::shutdown();
    romfsExit();
    gfxExit();
    return 0;
}
