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
#include "core/crash.h"
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
// ---- cogdo place runtime ----
#include "world/cogdo_runtime.h"
// ---- end cogdo ----
#include "world/boss_battle.h"
#include "world/cchar.h"
#include "world/cog_buildings.h"
#include "world/cog_interior.h"
#include "world/level.h"
#include "world/estate.h"
#include "world/pets.h"
// ---- clerks: compose the existing kart geometry from authoritative DNA ----
#include "world/clerk_kart_preview.h"
// ---- end clerks ----
#include "world/racing.h"
#include "world/racing_autotest.h" // ---- racing2 ----
#include "world/suits.h"
#include "game/minigame_session.h"
#include "quest/quest_talk.h"
#include "world/battle.h"
#include "world/battle_fx.h"
#include "world/trolley.h"
#include "world/fishing.h"
#include "world/golf.h"
#include "world/place.h"
#include "world/player.h"
#include "world/remote_toons.h"
#include "world/toon_avatar.h"
#include "world/toon_cache.h"
#include "world/toon_dna.h"
#include "world/transition.h"
// ---- tutorial ----
#include "world/tutorial.h"
// ---- end tutorial ----
#include "world/zone_actors.h"
// [interior life]
#include "world/interior_life.h"
#include "world/water.h"
// [end interior life]

namespace {

// Scenes that own the toon (water checks pause while one runs).
bool featureBusyForWater(bool fishing, bool trolley, bool minigame, bool transition) {
    return fishing || trolley || minigame || transition;
}

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

// The main thread's stack (libctru's weak default is 32 KB). main() alone keeps a 16 KB frame of
// subsystems, so 32 KB left callees too little: a startup crash (PC 0) after the polish merge.
extern "C" {
u32 __stacksize__ = 256 * 1024;
}

namespace {
// Main-thread work per frame. citro3d's processing time runs until the GPU queue drains, which in
// Azahar includes emulated GPU latency (100+ ms readings at 27 fps), so prefer the profiler's busy
// time (frame total minus the vblank wait).
float frameCpuMs() {
#if TT_PROFILE
    return tt::prof::busyAvgMs();
#else
    return C3D_GetProcessingTime();
#endif
}
} // namespace

int main() {
    gfxInitDefault();
    gfxSet3D(false); // stereo is opt-in only
    romfsInit();
    tt::log::init();
    tt::crash::installForThisThread(); // a CPU fault on the main thread is logged, not silent
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
    const tt::audio::SfxId sfxDecompress = tt::audio::loadSfx("sdmc:/3ds/toontown3ds/data/audio/toon_decompress.tsnd");
    float squishAge = -1.0f; // Toon.enterSquish / stunToon after a factory stomper (world/level.*)
    float zapStunAge = -1.0f; // stunToon after a goon's zap
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
    if (!toonCache.init()) { TT_LOG("boot: Toon stream pool allocation failed"); return 1; }
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
    // [interior life]
    static tt::world::InteriorLife interiorLife;
    interiorLife.init();
    // [end interior life]
    Model dropShadow; // otp/avatar/ShadowCaster.py: drop_shadow at scale 0.4, black, gray level 0.5
    dropShadow.load("romfs:/baked/drop_shadow.ttm");
    u32 lastExitZone = 0;
    tt::world::ToonAnim loggedAnim = tt::world::kAnimNeutral;
    // Place transitions: tunnels, building doors, teleports, and death.
    tt::world::Transition transition;
    transition.init();
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
    // ---- clerks: service panels share the existing bottom-screen lifecycle ----
    tt::ui::ServiceClerkPanel &serviceClerks = ui.serviceClerks();
    u32 lastTouchedServiceClerk = 0;
    static tt::world::ClerkKartPreview clerkKartPreview;
    // ---- end clerks ----
    u32 lastTouchedClerk = 0;
    u32 lastTouchedTailor = 0;
    u32 lastTouchedQuestNpc = 0;
    // Talking to quest-giving NPC toons (ToonTasks; quest/quest_talk.*, ui/quest_panel.*).
    static tt::quest::Talk talk;
    // ---- tutorial ----
    static tt::world::TutorialFlow tutorial;
    // ---- end tutorial ----
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
    static tt::world::BattleFx battleFx;
    battleFx.init();
    battle.setFx(&battleFx);
    struct FxToons { const tt::net::Client* net; const tt::world::ToonAvatar* local; const tt::world::RemoteToons* remote; };
    static FxToons fxToons{&net, &localToon, &remoteToons};
    battleFx.setToonLookup([](void* user, uint32_t doId) -> const tt::world::ToonAvatar* {
        const FxToons& t = *static_cast<FxToons*>(user);
        return doId == t.net->avatarId() ? t.local : t.remote->avatar(doId);
    }, &fxToons);
    static tt::world::ToonClipSet battleClips; // the local toon's battle clips, loaded per fight
    // The playground trolley and the minigame it leads to (docs/design/trolley.md).
    static tt::world::TrolleyRide trolley;
    trolley.setZone(kStartZone);
    static tt::game::MinigameSession minigame;
    static tt::world::GolfSession golf;
    static tt::world::CogBuildings cogBuildings;
    static tt::world::CogInterior cogInterior;
    static tt::world::Level level; // facility levels: the Sellbot factory (world/level.*)
    static tt::world::Estate estate;
    static tt::world::Pets pets;
    static tt::world::Racing racing;
    cogBuildings.setZone(kStartZone);
    cogBuildings.syncAnchors(place);
    tt::world::BattleView battleView;
    fishing.setZone(kStartZone, place.pointsPath());
    player.setCollision(&place.collision());
    player.reset(kSpawnX, kSpawnY, 0.0f, 0.0f);
    // ---- tutorial ----
    // enterTutorial already moved our Astron interest to the dynamic shop zone.  Load its
    // canonical bake before that interest generates Tutorial Tom and the interior objects.
    auto loadTutorialPlace = [&](u32 networkZone) {
        const u32 canonical = net.tutorialZones().toCanonical(networkZone);
        if (!tt::world::TutorialZones::isTutorialPlace(canonical) || place.zoneId() == canonical) return;
        if (!place.load(canonical)) {
            TT_LOG("tutorial: missing bake for canonical zone %lu", static_cast<unsigned long>(canonical));
            return;
        }
        zoneActors.loadPoints(place.pointsPath());
        cchars.setZone(canonical);
        fishing.setZone(canonical, place.pointsPath());
        suits.setZone(canonical);
        float x, y, z, h;
        place.arrival(0, &x, &y, &z, &h);
        player.reset(x, y, z, h);
        TT_LOG("tutorial: loaded canonical zone %lu for server zone %lu", static_cast<unsigned long>(canonical),
               static_cast<unsigned long>(networkZone));
    };
    // ---- end tutorial ----
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

    TT_LOG("boot: heap %u KB used, linear %lu KB free, vram %lu KB free",
           static_cast<unsigned>(mallinfo().uordblks / 1024),
           static_cast<unsigned long>(linearSpaceFree() / 1024),
           static_cast<unsigned long>(vramSpaceFree() / 1024));

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
        if (autotest.active() && secondElapsed) {
            TT_LOG("stats: [%s] fps %.1f cpu %.2f gpu %.2f cmdbuf %.0f%% skipped %lu draws %lu tris %lu uniforms %lu texbinds %lu vbo %lu",
                   autotest.label(), fps, frameCpuMs(), C3D_GetDrawingTime(), lastCmdBufUsage * 100.0f,
                   static_cast<unsigned long>(lastStats.modelsSkipped),
                   static_cast<unsigned long>(lastStats.drawCalls), static_cast<unsigned long>(lastStats.triangles),
                   static_cast<unsigned long>(lastStats.uniformUploads),
                   static_cast<unsigned long>(lastStats.textureBinds),
                   static_cast<unsigned long>(lastStats.bufferBinds));
            TT_LOG("stats: [%s] heap %uK linfree %luK vramfree %luK toons %d remote %d pos %.1f,%.1f,%.1f h %.0f",
                   autotest.label(), static_cast<unsigned>(mallinfo().uordblks / 1024),
                   static_cast<unsigned long>(linearSpaceFree() / 1024),
                   static_cast<unsigned long>(vramSpaceFree() / 1024), uiState.stressToons, remoteToons.count(),
                   player.x(), player.y(), player.z(), player.headingDeg());
        }
        tt::prof::lap(tt::prof::kInput);
        if (inFrontEnd) {
            if (in.down & KEY_START) break;
            if (netStarted) net.update(tt::net::plat::nowMs());
            tt::prof::lap(tt::prof::kNet);
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
            tt::prof::lap(tt::prof::kFrontUpdate);
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            tt::prof::lap(tt::prof::kWait);
            renderer.resetStats();
            toonCache.pump(); // previous GPU frame is complete: publish streamed poses
            tt::prof::lap(tt::prof::kLoads);
            if (shotName) {
                char path[96];
                snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_top.bin", shotName);
                tt::shot::dumpTarget(renderer.topLeft(), path);
                snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_bottom.bin", shotName);
                tt::shot::dumpTarget(bottom, path);
                shotName = nullptr;
                tt::prof::lap(tt::prof::kCapture);
            }
            tt::prof::lap(tt::prof::kLoads);
            frontEnd.drawTop(renderer);
            tt::prof::lap(tt::prof::kFrontDraw);
            frontEnd.drawBottom(bottom);
            tt::prof::lap(tt::prof::kBottomUi);
            lastStats = renderer.stats();
            lastCmdBufUsage = C3D_GetCmdBufUsage();
            C3D_FrameEnd(0);
            tt::prof::lap(tt::prof::kFrameEnd);
            tt::audio::update();
            tt::prof::lap(tt::prof::kAudio);
            continue;
        }
        if ((in.down & KEY_START) && (in.held & KEY_SELECT)) break;
        if (uiState.quitRequested) break;
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
            transition.reset(); // a transition in progress is abandoned with the session
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
        // A script started part-way (AUTOTEST_FROM) waits until we stand in a zone online.
        static float autotestSettle = 0.0f;
        if (!netStarted || net.inZone()) autotestSettle += dt;
        if (autotest.waiting() && autotestSettle > 3.0f) autotest.setReady();
        // ---- qafix: game-agnostic trolley completion gates ----
        autotest.setTrolleyProgress({trolley.seated(), minigame.startedGame(), minigame.endedGame(),
                                    minigame.completedPurchase() && !minigame.active()});
        // ---- end qafix ----
        autotest.apply(dt, in.pad, in.held, in.down, touch, uiState.stressToons);
        if (autotest.pendingShot()) shotName = autotest.pendingShot();
        // ---- tutorial ----
        // The Toontorial pass uses the same UI acknowledgements as a player.  QuestParser's
        // WAIT_EVENT gates are posted only when that exact generated script asks for them.
        if (autotest.tutorialPath()) {
            static u32 tutorialZone = 0;
            static bool tutorialSawSuit = false;
            static bool tutorialBattleWon = false;
            static bool tutorialHqVisited = false;
            static bool tutorialHqTalked = false;
            static bool tutorialShopTalked = false;
            const u32 zone = place.zoneId();
            if (zone != tutorialZone) {
                tutorialZone = zone;
                TT_LOG("tutorial: zone canonical %lu server %lu", static_cast<unsigned long>(zone),
                       static_cast<unsigned long>(net.zoneId()));
            }
            if (tutorial.active()) {
                in.down |= KEY_A; // confirm every generated CHAT_CONFIRM page
                const char* event = tutorial.waitingEvent();
                if (event && tutorial.postEvent(event)) TT_LOG("tutorial: QuestScript event %s", event);
            }
            if (talk.phase() == tt::quest::Talk::Phase::Pages) talk.advance();
            if (talk.phase() == tt::quest::Talk::Phase::QuestChoice) talk.chooseQuest(0);
            if (talk.phase() == tt::quest::Talk::Phase::TrackChoice) talk.chooseTrack(0);
            if (battle.tutorialBattle()) {
                battle.setAutoPlay(true, -1); // select an available cupcake/flower as InventoryNew permits
            }
            if (battle.tutorialRewardPending()) in.down |= KEY_A;
            if (!net.inTutorial() && net.tutorialFinished()) autotest.finishStep();

            // The points below are emitted by the tutorial bakers: never synthesize a position.
            if (net.tutorialSuitId()) tutorialSawSuit = true;
            if (tutorialSawSuit && !net.tutorialSuitId() && net.inTutorial()) tutorialBattleWon = true;
            if (talk.locked() && zone == tt::world::kTutorialHq) tutorialHqTalked = true;
            if (talk.locked() && zone == tt::world::kTutorialShop && tutorialHqTalked) tutorialShopTalked = true;
            if (!tutorial.active() && !battle.active() && transition.idle()) {
                if (zone == tt::world::kTutorialShop) {
                    if (tutorialHqTalked && !tutorialShopTalked) {
                        float npc[4];
                        if (zoneActors.shadowPositions(npc, 1) == 1) {
                            player.reset(npc[0], npc[1] + 2.5f, npc[2], 180.0f);
                            TT_LOG("tutorial: talk to Tutorial Tom at %.1f %.1f %.1f", double(npc[0]), double(npc[1]),
                                   double(npc[2]));
                        }
                    } else if (const tt::world::Place::Point* p = place.point("door_origin")) {
                        player.reset(p->x, p->y, p->z, p->h);
                        TT_LOG("tutorial: leave Tutorial Tom via %s", p->name);
                    }
                } else if (zone == tt::world::kTutorialStreet) {
                    float sx, sy, sz, sh;
                    if (!tutorialBattleWon && suits.pose(net.tutorialSuitId(), &sx, &sy, &sz, &sh)) {
                        player.reset(sx, sy, sz, sh);
                        TT_LOG("tutorial: approach Flunky %.1f %.1f %.1f", double(sx), double(sy), double(sz));
                    } else if (!tutorialHqVisited) {
                        if (const tt::world::Place::Point* p = place.point("tb1:toon_landmark_hqTT_DNARoot/door_0")) {
                            player.reset(p->x, p->y, p->z, p->h);
                            TT_LOG("tutorial: enter HQ via %s", p->name);
                        }
                    } else if (!tutorialShopTalked) {
                        if (const tt::world::Place::Point* p = place.point("tb2:toon_landmark_TT_A1_DNARoot_door_origin")) {
                            player.reset(p->x, p->y, p->z, p->h);
                            TT_LOG("tutorial: enter gag shop via %s", p->name);
                        }
                    } else if (const tt::world::Place::Point* p = place.point("linktunnel_tt_0_DNARoot_origin")) {
                        player.reset(p->x, p->y, p->z, p->h);
                        TT_LOG("tutorial: leave for Toontown Central via %s", p->name);
                    }
                } else if (zone == tt::world::kTutorialHq) {
                    if (tutorialHqTalked) {
                        if (const tt::world::Place::Point* p = place.point("door_origin_1")) {
                            player.reset(p->x, p->y, p->z, p->h);
                            TT_LOG("tutorial: leave HQ via %s", p->name);
                        }
                    } else {
                        float npc[4];
                        if (zoneActors.shadowPositions(npc, 1) == 1) {
                            player.reset(npc[0], npc[1] + 2.5f, npc[2], 180.0f);
                            tutorialHqVisited = true;
                            TT_LOG("tutorial: talk at NPC anchor %.1f %.1f %.1f", double(npc[0]), double(npc[1]),
                                   double(npc[2]));
                        }
                    }
                }
            }
        }
        // ---- end tutorial ----
        if (autotest.active()) {
            if (!strcmp(autotest.label(), "factory: crates") && autotest.stepTime() > 3.0f) {
                in.pad.dy = 127;
            } else if (!strcmp(autotest.label(), "da office: lasers") && autotest.stepTime() > 3.0f) {
                in.pad.dy = 60;
            }
        }
        // Boss place: one grouped hook owns both screens until the VP encounter ends.
        {
            static tt::world::BossBattle bossBattle;
            tt::world::BossFrame bossFrame{net, player, place, battle, battleFx, suits, remoteToons,
                localToon, toonCache, renderer, ui, uiState, autotest, bottom, in, touch, dt,
                shotName, lastStats, lastCmdBufUsage, stereoActive};
            // ---- cogdo: own both displays while the interior is present ----
            static tt::world::CogdoRuntime cogdo;
            if (netStarted && cogdo.frame(bossFrame)) continue;
            // ---- end cogdo ----
            if (netStarted && bossBattle.frame(bossFrame)) continue;
        }
        if (autotest.active() && !strncmp(autotest.label(), "book: ", 6)) {
            const char* page = autotest.label() + 6;
            if (!strcmp(page, "gags")) ui.openBookPage(tt::ui::Book::kPageGags);
            else if (!strcmp(page, "tasks")) ui.openBookPage(tt::ui::Book::kPageTasks);
            else if (!strcmp(page, "cogs")) ui.openBookPage(tt::ui::Book::kPageCogs);
            else if (!strcmp(page, "disguise")) ui.openBookPage(tt::ui::Book::kPageDisguise);
            else if (!strcmp(page, "fish")) ui.openBookPage(tt::ui::Book::kPageFish);
            else if (!strcmp(page, "trophies")) ui.openBookPage(tt::ui::Book::kPageTrophies);
            else if (!strcmp(page, "sos")) ui.openBookPage(tt::ui::Book::kPageSos);
            else if (!strcmp(page, "options")) ui.openBookPage(tt::ui::Book::kPageOptions);
        } else if (autotest.active() && ui.book().isOpen() && strncmp(autotest.label(), "book", 4) != 0 &&
                   strncmp(autotest.label(), "pad: book", 9) != 0) { // padnav: those steps drive the book with buttons
            ui.book().close();
        }
        if (autotest.active() && (!strncmp(autotest.label(), "exit: ", 6) || !strncmp(autotest.label(), "move: ", 6))) {
            // Test-only: stand inside the named trigger (tunnel owner / door owner prefix).
            const bool interiorDoor = !strcmp(autotest.label(), "exit: interior door");
            const bool streetDoor = !strcmp(autotest.label(), "exit: street door");
            const bool isTunnel = !strcmp(autotest.label(), "exit: tunnel 2132") || !strcmp(autotest.label(), "move: tunnel");
            const char* want = isTunnel ? "linktunnel_tt_2132"
                               : streetDoor ? "tb"
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
            if (place.zoneId() != 2520 && !uiState.teleportRequest && transition.idle()) uiState.teleportRequest = 2520;
            float npc[4];
            if (place.zoneId() == 2520 && !talk.locked() && zoneActors.shadowPositions(npc, 1) == 1 && now > hqNextMs) {
                player.reset(npc[0], npc[1] + 2.5f, npc[2], 180.0f);
                hqNextMs = now + 4000;
                TT_LOG("autotest: hq officer at %.1f,%.1f,%.1f; toon at %.1f,%.1f,%.1f; touching %u, talk locked %d",
                       double(npc[0]), double(npc[1]), double(npc[2]), double(player.x()), double(player.y()),
                       double(player.z()),
                       unsigned(netStarted ? zoneActors.touchingNpc(player.x(), player.y(), player.z(), net,
                                                                    tt::dc::DistributedNPCToon::kClassId)
                                           : 0u),
                       talk.locked() ? 1 : 0);
            }
            if (talk.phase() == tt::quest::Talk::Phase::Pages && now > hqNextMs - 2500) {
                talk.advance();
                hqNextMs = now + 4000;
            }
            if (talk.phase() == tt::quest::Talk::Phase::QuestChoice && !strcmp(autotest.label(), "toon hq: choose")) {
                talk.chooseQuest(0);
            }
        }
        // ---- clerks: diagnostics walk toward the actual DNA anchor ----
        if (autotest.active() && !strncmp(autotest.label(), "clerk: ", 7)) {
            const char *label = autotest.label();
            const bool pet = !strcmp(label, "clerk: pet shop") || !strcmp(label, "clerk: adopt");
            const bool kart = !strcmp(label, "clerk: kart shop");
            const bool fisherman = !strcmp(label, "clerk: fisherman");
            const u32 target = pet ? 2522 : kart ? 8501 : 2000;
            if (place.zoneId() != target) {
                if (!uiState.teleportRequest && transition.idle())
                    uiState.teleportRequest = target;
            } else if (netStarted && net.inZone() && transition.idle()) {
                const uint16_t type = pet         ? tt::dc::DistributedNPCPetclerk::kClassId
                                      : kart      ? tt::dc::DistributedNPCKartClerk::kClassId
                                      : fisherman ? tt::dc::DistributedNPCFisherman::kClassId
                                                  : tt::dc::DistributedNPCPartyPerson::kClassId;
                for (int slot = 0; slot < tt::net::kMaxNpcs; ++slot) {
                    const auto &npc = net.npcSlot(slot);
                    if (!npc.active || npc.classId != type)
                        continue;
                    char name[48];
                    if (fisherman)
                        snprintf(name, sizeof(name), "npc_fisherman_origin_%u", unsigned(npc.posIndex));
                    else if (!pet && !kart)
                        snprintf(name, sizeof(name), "party_person_%s", npc.posIndex % 2 ? "right" : "left");
                    else
                        snprintf(name, sizeof(name), "npc_origin_%u", unsigned(npc.posIndex));
                    const auto *anchor = place.point(name);
                    if (!anchor)
                        continue;
                    const float angle = anchor->h * 3.14159265f / 180.0f;
                    // Walk in from 9 units out, then onto the contact tube's axis (1 unit in front,
                    // DistributedNPCToonBase CollisionTube (0, 1, 0)): the fisherman's and party
                    // planner's tubes are only 1.0 wide.
                    const float approach = autotest.stepTime() < 3 ? 9.0f : 1.0f;
                    const float dx = anchor->x - sinf(angle) * approach - player.x();
                    const float dy = anchor->y + cosf(angle) * approach - player.y();
                    // From far away a straight walk runs into walls: start 9 units in front.
                    if (!serviceClerks.active() && dx * dx + dy * dy > 15.0f * 15.0f) {
                        player.reset(anchor->x - sinf(angle) * 9.0f, anchor->y + cosf(angle) * 9.0f, anchor->z + 0.5f,
                                     anchor->h + 180.0f);
                        break;
                    }
                    if (!serviceClerks.active() && dx * dx + dy * dy > .25f) {
                        player.setHeading(atan2f(-dx, dy) * 180.0f / 3.14159265f);
                        in.pad.dx = 0;
                        in.pad.dy = 90;
                    }
                    if (serviceClerks.active() && autotest.stepTime() > 4)
                        serviceClerks.diagnostic(label);
                    break;
                }
            }
        }
        // ---- end clerks ----
        // ---- cog building autotest ----
        // Test-only: visit streets until one has a Cog-owned building, then stand in its elevator
        // (CogBuildings boards on contact); the floors then play out with battle autoplay, and the
        // battle step ends once the server has put us back on the street.
        if (autotest.active() && !strcmp(autotest.label(), "cog building: floor") && netStarted) {
            static const u32 kStreets[] = {2100, 2200, 2300, 1100, 1200, 1300, 5100, 5200, 5300, 4100, 4200,
                                           4300, 3100, 3200, 3300, 9100, 9200};
            static int street = 0;
            static u32 arrivedMs = 0;
            static bool boarded = false;
            const u32 now = tt::net::plat::nowMs();
            if (autotest.stepBegan()) street = 0, arrivedMs = 0, boarded = false;
            const u32 here = place.zoneId() - place.zoneId() % 100;
            const bool onStreet = tt::world::placeKind(place.zoneId()) == tt::world::PlaceKind::Street;
            if (cogInterior.active() || boarded) {
                // inside, or riding the elevator up
                if (cogInterior.active() && autotest.stepTime() > 2.0f) autotest.finishStep();
            } else if (here != kStreets[street] || !onStreet) {
                if (!uiState.teleportRequest && transition.idle()) uiState.teleportRequest = kStreets[street];
                arrivedMs = 0;
            } else if (transition.idle() && net.inZone()) {
                if (arrivedMs == 0) arrivedMs = now;
                float ex, ey, ez, eh;
                if (cogBuildings.suitElevatorPoint(net, &ex, &ey, &ez, &eh)) {
                    player.reset(ex, ey, ez + 0.5f, eh);
                    boarded = true;
                    TT_LOG("autotest: cog building elevator in %lu at %.1f,%.1f", static_cast<unsigned long>(here),
                           double(ex), double(ey));
                } else if (now - arrivedMs > 4000) {
                    TT_LOG("autotest: no Cog building in %lu", static_cast<unsigned long>(here));
                    street = (street + 1) % int(sizeof(kStreets) / sizeof(kStreets[0]));
                    arrivedMs = 0;
                }
            }
        }
        if (autotest.active() && !strcmp(autotest.label(), "cog building: battle")) {
            static bool wasInside = false;
            if (autotest.stepBegan()) wasInside = false;
            if (cogInterior.active()) wasInside = true;
            if (cogInterior.active() && !battle.active() && !cogInterior.seated()) {
                float ex, ey, ez, eh;
                if (cogInterior.elevatorOutPoint(&ex, &ey, &ez, &eh))
                    player.reset(ex, ey, ez + 0.5f, eh);
            }
            if (wasInside && !cogInterior.active() && transition.idle() && autotest.stepTime() > 10.0f)
                autotest.finishStep();
        }
        // ---- end cog building autotest ----
        if (autotest.active() && !strcmp(autotest.label(), "swim: harbor")) {
            // Test-only: drop into Donald's Dock harbour (open water south of the west pier) and
            // swim forward with the normal camera (DDPlayground.__checkToonUnderwater). Started
            // elsewhere, teleport there first.
            static bool dropped = false;
            if (autotest.stepBegan()) dropped = false;
            if (place.zoneId() != 1000 && !uiState.teleportRequest && transition.idle()) {
                uiState.teleportRequest = 1000;
            } else if (place.zoneId() == 1000 && transition.idle() && !dropped) {
                player.reset(0.0f, -40.0f, 2.0f, 180.0f);
                dropped = true;
            }
        }
        if (autotest.active() && !strcmp(autotest.label(), "tailor visit") && autotest.stepTime() > 9.0f) {
            tailor.cancel(); // never leave the session open (DistributedNPCTailorAI unexpected-exit bug)
        }
        if (autotest.active() && !strcmp(autotest.label(), "go home") && autotest.stepBegan()) {
            uiState.homeRequest = true; // test-only: the Map tab's Home button
        }
        if (autotest.active() && !strncmp(autotest.label(), "golf: ", 6)) {
            // GZHoodDataAI places the DistributedGolfKarts in Chip 'n Dale's MiniGolf, reached by
            // the tunnel from Acorn Acres.
            if (!golf.active() && place.zoneId() != 17000 && !uiState.teleportRequest && transition.idle()) {
                uiState.teleportRequest = 17000;
            }
            const tt::net::GolfKart* kart = netStarted ? net.golfKart() : nullptr;
            if (kart && !golf.active() && !strcmp(autotest.label(), "golf: board")) {
                player.reset(kart->x, kart->y, kart->z, kart->h);
                net.sendGolfKartBoard();
            }
        }
        if (autotest.active() && !strncmp(autotest.label(), "go speedway", 11) &&
            place.zoneId() != 8000 && !uiState.teleportRequest && transition.idle()) {
            uiState.teleportRequest = 8000;
        }
        // ---- racing2 ----
        static tt::world::RacingAutotest racingAutotest;
        racingAutotest.update(autotest, net, racing, place, player, &in, transition.idle());
        // ---- end racing2 ----
        if (autotest.active() && !level.active()) {
            // Test-only: step into a facility's elevator, re-trying every 6 s: the factory's, the Coin
            // Mint's (DistributedMintElevatorExt origin 1: CashbotMintIntA), DA Office A's
            // (DistributedLawOfficeElevatorExt entrance 0), or the first Bossbot golf kart
            // (DistributedCogKart, placed by setPosHpr).
            struct Board { const char* label; u32 zone; const char* anchor; };
            static const Board kBoards[] = {{"factory: board", 11200, "elevator_origin_0"},
                                            {"mint: board", 12000, "elevator_origin_1"},
                                            {"da office: board", 13200, "elevator_origin_0"},
                                            {"club: board", 10000, nullptr}};
            static u32 boardNextMs = 0;
            for (const Board& b : kBoards) {
                if (strcmp(autotest.label(), b.label) || place.zoneId() != b.zone || tt::net::plat::nowMs() < boardNextMs)
                    continue;
                float bx = 0, by = 0, bz = 0, bh = 0;
                bool found = false;
                if (const tt::world::Place::Point* p = b.anchor ? place.point(b.anchor) : nullptr) {
                    bx = p->x, by = p->y, bz = p->z, bh = p->h;
                    found = true;
                }
                for (int n = 0; !b.anchor && !found && netStarted && n < tt::net::kMaxCogElevators; ++n) {
                    const tt::net::CogElevator& e = net.cogElevatorSlot(n);
                    if (e.active && e.hasPose) bx = e.x, by = e.y, bz = e.z, bh = e.h, found = true;
                }
                if (found) {
                    player.reset(bx, by, bz + 0.5f, bh);
                    boardNextMs = tt::net::plat::nowMs() + 6000;
                }
            }
        }
        if (autotest.active() && !strcmp(autotest.label(), "tailor visit")) {
            // Test-only: teleport into the TTC Clothing Store (2521) and step onto the tailor.
            static u32 tailorNextMs = 0;
            const u32 now = tt::net::plat::nowMs();
            if (place.zoneId() != 2521 && !uiState.teleportRequest && transition.idle()) uiState.teleportRequest = 2521;
            float npc[4];
            if (place.zoneId() == 2521 && zoneActors.shadowPositions(npc, 1) == 1 && now > tailorNextMs) {
                player.reset(npc[0], npc[1] - 2.0f, npc[2], 0.0f);
                tailorNextMs = now + 5000;
            }
        }
        if (autotest.active() && !strncmp(autotest.label(), "go ", 3)) {
            // Test-only: teleport straight to a Cog HQ courtyard or a facility's entrance (upstream
            // gets there by tunnels and doors). "go speedway", "go fishing" and "go home" have their own.
            struct Go { const char* label; u32 zone; };
            static const Go kGo[] = {{"go sellbot hq", 11000}, {"go cashbot hq", 12000},  {"go lawbot hq", 13000},
                                     {"go bossbot hq", 10000}, {"go factory", 11200},     {"go mint", 12000},
                                     {"go da office", 13200},  {"go country club", 10000}};
            for (const Go& g : kGo)
                if (!strcmp(autotest.label(), g.label) && place.zoneId() != g.zone && !uiState.teleportRequest &&
                    transition.idle())
                    uiState.teleportRequest = g.zone;
        }
        if (autotest.active() && !strcmp(autotest.label(), "meet mickey")) {
            float cx, cy, cz;
            if (cchars.firstPosition(&cx, &cy, &cz)) autotestFace(cx, cy, cz, 9.0f);
        }
        if (autotest.active() && !battle.active() &&
            (!strcmp(autotest.label(), "fight cog") || !strncmp(autotest.label(), "battle: ", 8))) {
            // Test-only: walk into the first cog; Battle::setAutoPlay picks the gags (the
            // "battle: <track>" steps force that track, each in a fight of its own).
            float cx, cy, cz;
            if (suits.firstPosition(&cx, &cy, &cz)) player.reset(cx, cy - 0.5f, cz, 0.0f);
        }
        // Won: the teleport back to Sellbot HQ has landed, so the step's remaining wait is idle.
        if (autotest.active() && !strcmp(autotest.label(), "factory: foreman") && autotest.stepTime() > 5.0f &&
            !level.active() && transition.idle())
            autotest.finishStep();
        if (autotest.active() && level.active() && !battle.active()) {
            // Test-only: look at the nearest factory cog, then walk into it (Battle autoplay fights).
            float cx, cy, cz;
            // Test-only: drop onto a gear / paint mixer / the conveyor and ride it.
            struct Drop { const char* label; const char* type; float dz; };
            static const Drop kDrops[] = {{"factory: gear", "gear", 26.5f},
                                          {"factory: mixer", "paintMixer", 2.0f},
                                          {"factory: conveyor", "conveyorBelt", 2.0f}};
            for (const Drop& drop : kDrops) {
                float gx, gy, gz;
                if (!strcmp(autotest.label(), drop.label) && autotest.stepBegan() &&
                    level.specPosition(drop.type, &gx, &gy, &gz))
                    player.reset(gx, gy, gz + drop.dz, 90.0f);
            }
            {
                // Test-only: stand in front of a goon (its radar zaps us) / drop onto its head (stun).
                // The crate room shows no goons or Cogs: the steps after it start back where the
                // crates step began, and each looks every frame until a goon is in view.
                static float preCrates[3] = {};
                static bool hasPreCrates = false;
                static bool goonPlaced = false;
                if (!strcmp(autotest.label(), "factory: crates") && autotest.stepBegan()) {
                    preCrates[0] = player.x(), preCrates[1] = player.y(), preCrates[2] = player.z();
                    hasPreCrates = true;
                }
                const bool zap = !strcmp(autotest.label(), "factory: goon zap");
                const bool stun = !strcmp(autotest.label(), "factory: goon stun");
                if (zap && autotest.stepBegan() && hasPreCrates) {
                    player.reset(preCrates[0], preCrates[1], preCrates[2] + 0.2f, player.headingDeg());
                    hasPreCrates = false;
                }
                if ((zap || stun) && autotest.stepBegan()) goonPlaced = false;
                float gx = 0, gy = 0, gz = 0, gh = 0;
                if ((zap || stun) && !goonPlaced && autotest.stepTime() > 1.0f &&
                    level.nearestGoon(player.x(), player.y(), player.z(), &gx, &gy, &gz, &gh)) {
                    goonPlaced = true;
                    TT_LOG("autotest: goon at %.1f,%.1f,%.1f h %.0f", double(gx), double(gy), double(gz), double(gh));
                    const float r = gh * kDegToRad;
                    if (zap) player.reset(gx - sinf(r) * 8.0f, gy + cosf(r) * 8.0f, gz + 0.2f, gh + 180.0f);
                    else player.reset(gx - sinf(r) * 0.8f, gy + cosf(r) * 0.8f, gz + 6.6f, gh); // just above, led
                }
            }
            if (!strcmp(autotest.label(), "factory: barrel") && autotest.stepBegan()) {
                float bx, by, bz;
                if (level.specPosition("gagBarrel", &bx, &by, &bz)) player.reset(bx, by - 4.0f, bz + 0.5f, 0.0f);
            }
            if (!strcmp(autotest.label(), "factory: foreman") && autotest.stepBegan()) {
                float fx, fy, fz;
                if (level.foremanPosition(&fx, &fy, &fz)) player.reset(fx, fy - 6.0f, fz + 0.3f, 0.0f); // walk in
            }
            if (!strcmp(autotest.label(), "factory: lift") && autotest.stepBegan()) {
                float lx, ly, lz;
                if (level.specPosition("lift", &lx, &ly, &lz)) player.reset(lx, ly, lz + 1.0f, 0.0f); // onto its floor
            }
            if (!strcmp(autotest.label(), "factory: stomper") && autotest.stepBegan()) {
                float sx, sy, sz;
                if (level.specPosition("stomper", &sx, &sy, &sz)) player.reset(sx, sy, sz + 0.5f, 0.0f); // under it
            }
            if (!strcmp(autotest.label(), "factory: button") && autotest.stepBegan()) {
                float bx, by, bz;
                if (level.nearestEntity(tt::dc::DistributedButton::kClassId, player.x(), player.y(), player.z(), &bx,
                                        &by, &bz))
                    player.reset(bx, by, bz + 2.5f, 0.0f); // drop onto it and stand still
            }
            if (!strcmp(autotest.label(), "factory: door")) {
                float dx, dy, dz, dh;
                if (autotest.stepBegan() &&
                    level.nearestDoor(player.x(), player.y(), player.z(), &dx, &dy, &dz, &dh)) {
                    player.reset(dx, dy, dz + 0.2f, dh); // stand at the door: walking in asks it to open
                }
            }
            {
                // Test-only: the room games (world/level_games.*). On the step's first frame go to the
                // game's spec position (which brings its zone / room into view, so the AI's object
                // generates), then 2 s later to the live entity, offset to where the game is played.
                struct Game {
                    const char* label;
                    const char* specType;
                    uint16_t classId;
                    float dx, dy, dz, h;
                };
                static const Game kGames[] = {
                    {"factory: crates", "crate", tt::dc::DistributedCrate::kClassId, -3.5f, 0, 0.1f, -90}, // face +x
                    {"da office: lasers", "laserField", tt::dc::DistributedLaserField::kClassId, 4, 4, 0.1f, 0},
                    {"da office: security cam", "securityCamera", tt::dc::DistributedSecurityCamera::kClassId, 2, 2,
                     0.1f, 0},
                    {"club: maze", "maze", tt::dc::DistributedMaze::kClassId, 0, 2, 0.1f, 0},
                    {"club: moles", "moleField", tt::dc::DistributedMoleField::kClassId, 0, 0, 0.1f, 0},
                    {"club: golf green", "golfGreenGame", tt::dc::DistributedGolfGreenGame::kClassId, 3, 13, 0.1f,
                     180},
                };
                static bool gameFound = false;
                for (const Game& g : kGames) {
                    if (strcmp(autotest.label(), g.label)) continue;
                    float gx, gy, gz;
                    if (autotest.stepBegan()) {
                        gameFound = false;
                        if (level.specPosition(g.specType, &gx, &gy, &gz)) player.reset(gx + g.dx, gy + g.dy, gz + 1.0f, g.h);
                        else TT_LOG("autotest: no %s on this level", g.specType);
                    } else if (!gameFound && autotest.stepTime() > 2.0f) {
                        gameFound = true;
                        float w[12];
                        if (level.nearestEntityTransform(g.classId, player.x(), player.y(), player.z(), w)) {
                            const float wx = w[0] * g.dx + w[3] * g.dy + w[6] * g.dz + w[9];
                            const float wy = w[1] * g.dx + w[4] * g.dy + w[7] * g.dz + w[10];
                            const float wz = w[2] * g.dx + w[5] * g.dy + w[8] * g.dz + w[11];
                            const float entH = std::atan2(-w[3], w[4]) / kDegToRad;
                            player.reset(wx, wy, wz, entH + g.h);
                            TT_LOG("autotest: %s at %.1f,%.1f,%.1f (toon at %.1f,%.1f,%.1f h: %.1f)",
                                   g.specType, double(w[9]), double(w[10]), double(w[11]),
                                   double(wx), double(wy), double(wz), double(entH + g.h));
                        } else {
                            TT_LOG("autotest: no live %s generated", g.specType);
                        }
                    }
                }
            }
            if (!strcmp(autotest.label(), "factory: crates")) {
                // Test-only: CrushableEntityGrid refuses a push into a blocked cell (setReject), so
                // every 3 s without a setMoveTo try the next face, facing the crate.
                static const float kFaces[4][2] = {{-3.5f, 0}, {3.5f, 0}, {0, -3.5f}, {0, 3.5f}};
                static int crateFace = 0;
                if (autotest.stepBegan()) crateFace = 0;
                bool moved = false;
                for (int n = 0; n < tt::net::kMaxLevelEntities; ++n) {
                    const tt::net::LevelEntityObj& e = net.levelEntitySlot(n);
                    moved |= e.active && e.classId == tt::dc::DistributedCrate::kClassId && e.moving;
                }
                float w[12];
                if (!moved && crateFace < 3 && autotest.stepTime() > 5.0f + 3.0f * float(crateFace) &&
                    level.nearestEntityTransform(tt::dc::DistributedCrate::kClassId, player.x(), player.y(),
                                                 player.z(), w)) {
                    ++crateFace;
                    const float fx = kFaces[crateFace][0];
                    const float fy = kFaces[crateFace][1];
                    const float wx = w[0] * fx + w[3] * fy + w[9];
                    const float wy = w[1] * fx + w[4] * fy + w[10];
                    const float faceH = std::atan2(wx - w[9], -(wy - w[10])) / kDegToRad; // forward = toward crate
                    player.reset(wx, wy, w[11] + 0.1f, faceH);
                    TT_LOG("autotest: crate face %d (toon at %.1f,%.1f)", crateFace, double(wx), double(wy));
                }
            }
            const char* label = autotest.label();
            auto facilityStep = [label](const char* step) { // "mint: <step>", "da office: <step>", "club: <step>"
                const char* colon = strstr(label, ": ");
                return colon && !strcmp(colon + 2, step) &&
                       (!strncmp(label, "mint", 4) || !strncmp(label, "da office", 9) || !strncmp(label, "club", 4));
            };
            if (facilityStep("walk")) {
                // Test-only: through the floor one piece (room or hallway) every 2 s.
                static u32 walkNextMs = 0;
                static int walkPiece = 0;
                const u32 now = tt::net::plat::nowMs();
                if (autotest.stepBegan()) walkPiece = 0, walkNextMs = 0;
                float px, py, pz;
                if (now >= walkNextMs && walkPiece < level.numPieces() &&
                    level.pieceCenter(walkPiece, &px, &py, &pz)) {
                    player.reset(px, py, pz + 1.0f, 0.0f);
                    TT_LOG("autotest: floor piece %d of %d", walkPiece, level.numPieces());
                    walkPiece++;
                    walkNextMs = now + 2000;
                }
            }
            if (facilityStep("next floor")) {
                // Into the floor elevator's doors (again after any battle there) until we have a seat.
                static u32 elevatorNextMs = 0;
                const u32 now = tt::net::plat::nowMs();
                if (autotest.stepBegan()) elevatorNextMs = 0;
                float ex, ey, ez, eh;
                if (!level.aboardElevator() && now >= elevatorNextMs &&
                    level.elevatorPosition(&ex, &ey, &ez, &eh)) {
                    player.reset(ex, ey, ez + 0.5f, eh);
                    elevatorNextMs = now + 10000;
                }
            }
            if (facilityStep("boss") && autotest.stepBegan()) {
                float fx, fy, fz;
                if (level.foremanPosition(&fx, &fy, &fz)) player.reset(fx, fy - 6.0f, fz + 0.3f, 0.0f); // walk in
            }
            const bool look = !strcmp(autotest.label(), "factory: entities");
            const bool fight = !strcmp(autotest.label(), "factory: battle") || facilityStep("battle");
            if ((look || fight) && level.nearestCog(player.x(), player.y(), player.z(), &cx, &cy, &cz)) {
                if (look) autotestFace(cx, cy, cz, 12.0f);
                else player.reset(cx, cy - 0.5f, cz, 0.0f);
            }
        }
        if (autotest.active() && !strcmp(autotest.label(), "see cog")) {
            // Started mid-script: go to a street with cogs (Silly Street) first.
            if (tt::world::placeKind(place.zoneId()) != tt::world::PlaceKind::Street &&
                !uiState.teleportRequest && transition.idle())
                uiState.teleportRequest = 2100;
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
        // [interior life]
        if (autotest.active() && !strcmp(autotest.label(), "interior: hq board")) {
            if (place.zoneId() != 2520 && !uiState.teleportRequest && transition.idle()) uiState.teleportRequest = 2520;
            if (place.zoneId() == 2520) {
                // Face the empty_board at (-47.4427, 29.6598, 8.15647); the rows are whatever
                // the server's setLeaderBoard holds (often empty: '-').
                player.reset(-37.0f, 29.65f, 4.0f, 270.0f);
            }
        }
        if (autotest.active() && !strcmp(autotest.label(), "interior: pet fish")) {
            if (place.zoneId() != 2522 && !uiState.teleportRequest && transition.idle()) uiState.teleportRequest = 2522;
            if (place.zoneId() == 2522) {
                // Face the fish tank at (0, 6, -4)
                player.reset(0.0f, -2.0f, 0.0f, 0.0f);
            }
        }
        if (autotest.active() && !strcmp(autotest.label(), "interior: door leaves")) {
            // The previous step leaves us in the pet shop: walk into its real exit door, which
            // plays the door-out movie (right leaf) and the door-in movie outside (left leaf).
            static bool doorWalked = false;
            if (autotest.stepBegan()) doorWalked = false;
            const tt::world::CollisionWorld& cw = place.collision();
            for (int i = 0; !doorWalked && transition.idle() && place.zoneId() % 1000 >= 500 && i < cw.numTriggers(); i++) {
                const tt::world::CollTrigger& t = cw.trigger(i);
                if (!strstr(t.name, "door_trigger")) continue;
                player.reset((t.min[0] + t.max[0]) * 0.5f, (t.min[1] + t.max[1]) * 0.5f, t.min[2], player.headingDeg());
                TT_LOG("autotest: walking into %s for the door leaves", t.name);
                doorWalked = true;
            }
        }
        if (autotest.active() && !strcmp(autotest.label(), "street: knock knock")) {
            if (place.zoneId() != 2100 && !uiState.teleportRequest && transition.idle()) uiState.teleportRequest = 2100;
            // Walk into the first knock-knock door sphere once, and let the AI start the joke.
            static bool knockPlaced = false;
            if (autotest.stepBegan()) knockPlaced = false;
            float kx, ky, kz;
            if (place.zoneId() / 100 == 21 && transition.idle() && !knockPlaced && interiorLife.knockSphere(0, &kx, &ky, &kz)) {
                player.reset(kx, ky, kz + 0.1f, 0.0f); // walls push the toon out in front of the door
                knockPlaced = true;
            }
        }
        // [end interior life]
        // ---- padnav: "pad: ..." autotest steps (docs/design/pad_navigation.md) press only
        // buttons; these hooks put the toon where a panel opens and wait for it to open.
        if (autotest.active() && !strncmp(autotest.label(), "pad: ", 5)) {
            constexpr float kPadPagePressSec = 2.0f;  // one A per dialog page
            constexpr float kPadAwaySec = 2.0f;       // off the clerk first: the NPC wants a new contact
            constexpr float kPadAwayDist = 8.0f;
            constexpr float kPadSafetySec = 1.5f;     // a press step that left its panel open closes it
            constexpr u32 kPadTailorRetryMs = 5000;
            const char* padStep = autotest.label();
            if (!strcmp(padStep, "pad: battle start")) {
                // Walk into a cog like "fight cog"; the gag choice is left to the pad steps.
                float cx, cy, cz;
                if (!battle.active() && suits.firstPosition(&cx, &cy, &cz)) player.reset(cx, cy - 0.5f, cz, 0.0f);
                if (battle.active() && battle.phase() == tt::world::BattlePhase::Input) autotest.finishStep();
            }
            if (!strcmp(padStep, "pad: quest pages")) {
                static float pagePress = 0.0f;
                if (autotest.stepBegan()) pagePress = 0.0f;
                pagePress += dt;
                if (talk.phase() == tt::quest::Talk::Phase::Pages && pagePress >= kPadPagePressSec) {
                    in.down |= KEY_A; // a button press, as a player would
                    pagePress = 0.0f;
                }
                if (talk.phase() == tt::quest::Talk::Phase::QuestChoice) autotest.finishStep();
            }
            if (!strcmp(padStep, "pad: shop open")) {
                float npc[4];
                if (!shop.active() && zoneActors.shadowPositions(npc, 1) == 1) {
                    const float back = autotest.stepTime() < kPadAwaySec ? kPadAwayDist : 2.0f;
                    player.reset(npc[0], npc[1] - back, npc[2], 0.0f);
                }
                if (shop.active()) autotest.finishStep();
            }
            if (!strcmp(padStep, "pad: shop press") && autotest.stepTime() > kPadSafetySec && shop.active()) {
                shop.finish(true);
            }
            if (!strcmp(padStep, "pad: tailor open")) {
                static u32 tailorRetryMs = 0;
                const u32 now = tt::net::plat::nowMs();
                if (place.zoneId() != 2521 && !uiState.teleportRequest && transition.idle()) uiState.teleportRequest = 2521;
                float npc[4];
                if (!tailor.active() && place.zoneId() == 2521 && zoneActors.shadowPositions(npc, 1) == 1 &&
                    now > tailorRetryMs) {
                    const bool away = autotest.stepTime() < kPadAwaySec;
                    player.reset(npc[0], npc[1] - (away ? kPadAwayDist : 2.0f), npc[2], 0.0f);
                    if (!away) tailorRetryMs = now + kPadTailorRetryMs;
                }
                if (tailor.active()) autotest.finishStep();
            }
            if (!strcmp(padStep, "pad: tailor press") && autotest.stepTime() > kPadSafetySec && tailor.active()) {
                tailor.cancel();
            }
        }
        // ---- end padnav ----
        // Buttons the UI used (e.g. A/B in the SpeedChat list) must not also reach the world.
        if (battle.active() && netStarted) {
            battle.fillView(net, tt::net::plat::nowMs(), &battleView);
            const tt::net::OwnStats& st = net.ownStats();
            ui.setBattle(&battleView, tt::ui::BattleGags{st.inventory, st.experience, st.trackAccess});
        } else {
            ui.setBattle(nullptr, tt::ui::BattleGags{});
        }
        ui.setTalk(&talk);
        // ---- qafix: retire context panels before dispatching any input ----
        const bool exclusivePanel = battle.active() || trolley.seated() || minigame.active() || golf.active() || racing.driving(); // ---- racing2 ----
        const bool lostPanelContext = netStarted && !net.inZone();
        if (exclusivePanel || lostPanelContext || (fishing.active() && !transition.idle())) fishing.cancel();
        ui.syncContext(net, place.zoneId(), lostPanelContext, exclusivePanel, fishing.active(),
                       trolley.seated() || minigame.active() || golf.active() || racing.hudVisible()); // ---- racing2 ----
        // ---- end qafix ----
        ui.setRacingInput(racing.hudVisible()); // ---- racing2 ----
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
        ui.setEmoteAccess(netStarted && net.ownStats().hasEmotes?net.ownStats().emoteAccess:nullptr);
        auto tryEmote = [&](int emote) {
            if(!battle.active() && !fishing.active() && !trolley.seated() && !minigame.active() && transition.idle() &&
               !localToon.emoteActive() && !uiState.whisperTo && (!netStarted || net.sendEmote(emote))) {
                const bool ok = localToon.playEmote(emote);
                TT_LOG("emote: %d %s", emote, ok ? "playing" : "no clip");
            } else {
                TT_LOG("emote: %d refused", emote);
            }
        };
        static const char* emoteStep = nullptr; // autotest "emote: <TTEmote id> <name>", once per step
        if (autotest.active() && !strncmp(autotest.label(), "emote: ", 7) && autotest.label() != emoteStep) {
            emoteStep = autotest.label();
            if (localToon.emoteActive()) localToon.cancel(); // back-to-back steps; shots land mid-clip
            tryEmote(atoi(autotest.label() + 7));
        }
        int speechMsg;
        while (ui.popSpeech(speechMsg)) {
            if(speechMsg<0) {
                tryEmote(-1-speechMsg);
                continue;
            }
            if (!uiState.whisperTo) { // whispers are private: no balloon over our head
                myPhrase = tt::sc::phraseText(speechMsg);
                myPhraseAge = 0.0f;
            }
            tt::audio::playSfx(sfxClick);
            pendingSpeech = speechMsg;
        }
        myPhraseAge += dt;
        if (localToon.bodyLocked() && !racing.driving()) {
            in.pad.dx = in.pad.dy = 0;
            in.down &= ~(KEY_A | KEY_B);
            in.held &= ~(KEY_A | KEY_B);
        }
        transition.applyInput(in.pad, in.held, in.down);
        tt::prof::lap(tt::prof::kInput);
        const circlePosition minigamePad = in.pad;
        talk.update(dt);
        if (talk.locked()) { // talking to an NPC: the dialog panel has the buttons
            in.pad.dx = 0;
            in.pad.dy = 0;
            in.held &= ~(KEY_A | KEY_B);
        }
        if (cogInterior.seated()) cogInterior.handleInput(net, in.down);
        if (minigame.active() || golf.active() || trolley.seated() || cogInterior.seated()) { // seated or in a game: the toon isn't walking
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
        // ---- clerks: the panel owns input while talking to the counter ----
        if (serviceClerks.active()) {
            in.pad = {};
            in.down &= ~(KEY_A | KEY_B);
            in.held &= ~(KEY_A | KEY_B);
        }
        // ---- end clerks ----
        // Donald's boat: its deck collision carries the toon (Player::setPlatform); pose first.
        boat.setListener(player.cameraEye().x, player.cameraEye().y, player.cameraTarget().x, player.cameraTarget().y);
        boat.update(tt::net::plat::nowMs());
        player.setPlatform(boat.deck(), boat.x(), boat.y(), boat.z(), boat.heading());
        fishing.handleInput(in, dt, tt::net::plat::nowMs(), transition.idle() && !exclusivePanel &&
                            !shop.active() && !tailor.active() && !serviceClerks.active() && !talk.locked());
        tt::prof::lap(tt::prof::kNet);
        racing.update(net, in, dt, player.x(), player.y(), player.z(), tt::net::plat::nowMs());
        tt::prof::lap(tt::prof::kRacing);
        // The facility level first: its lifts/stompers must be where they are *this* frame
        // before the toon stands on them (it uses last frame's feet).
        tt::world::LevelBattleAction levelAction;
        if (level.active() && netStarted && net.inZone()) {
            level.update(net, suits, player.x(), player.y(), player.z(), dt, tt::net::plat::nowMs(),
                         battle.idle() && transition.idle(), &levelAction);
            float sx, sy, sz, sh;
            if (level.popSeat(&sx, &sy, &sz, &sh)) player.reset(sx, sy, sz, sh); // a floor elevator seat
        }
        // A DA office / country club floor elevator left with us: ride to the next floor's zone
        // (the old floor is already gone; the new one and its rooms load there like the first).
        if (const u32 next = netStarted && net.inZone() ? level.popNextFloorZone() : 0) {
            TT_LOG("elevator: riding to the next floor, zone %lu", static_cast<unsigned long>(next));
            net.changeZone(next);
            remoteToons.clear();
        }
        player.setDynamic(level.solids()); // facility doors, buttons, lifts (nullptr elsewhere)
        {
            float floorZ;
            if (level.active() && level.popSquish(&floorZ) > 0) {
                squishAge = 0.0f;
                player.reset(player.x(), player.y(), floorZ, player.headingDeg()); // setZ(stomper + 0.025)
            }
            if (level.active() && level.popOuch() > 0) zapStunAge = 0.0f; // a goon zap: stunToon only
            if (zapStunAge >= 0.0f && (zapStunAge += dt) > 4.0f) { // stunToon's seven blinks
                zapStunAge = -1.0f;
                if (squishAge < 0.0f) level.setStunned(false);
            }
            if (squishAge >= 0.0f) {
                const float before = squishAge;
                squishAge += dt;
                if (before < 1.1f && squishAge >= 1.1f) { // the pop: jump from 0.2 s and the decompress
                    tt::audio::playSfx(sfxDecompress);
                    localToon.playAction(tt::world::kAnimJump, tt::world::ClipMode::Once, 1.0f, 0.2f);
                }
                if (squishAge < 1.7f) { // the 'squished' place state: no walking
                    in.pad.dx = in.pad.dy = 0;
                    in.down &= ~KEY_A;
                }
                if (squishAge > 4.0f) { // stunToon's seven half-second blinks are over
                    squishAge = -1.0f;
                    if (zapStunAge < 0.0f) level.setStunned(false);
                }
            }
        }
        if (!racing.driving() && !((level.active() && level.loadingAssets()) ||
                                   (cogInterior.active() && cogInterior.loadingAssets())))
            player.update(in, dt);
        fishing.applyCamera();
        // ---- qafix: upstream NPC talk camera, with normal collision pull-in ----
        static u32 talkCameraNpc = 0;
        static bool talkCameraChoice = false;
        static float talkCameraAge = 0;
        static C3D_FVec talkFromEye{}, talkFromTarget{};
        const bool choiceCamera = talk.phase() == tt::quest::Talk::Phase::QuestChoice ||
                                  talk.phase() == tt::quest::Talk::Phase::TrackChoice;
        C3D_FVec talkEye, talkTarget;
        if (talk.locked() && zoneActors.talkCamera(talk.npcDoId(), choiceCamera, &talkEye, &talkTarget)) {
            if (talkCameraNpc != talk.npcDoId() || talkCameraChoice != choiceCamera) {
                talkCameraNpc = talk.npcDoId();
                talkCameraChoice = choiceCamera;
                talkCameraAge = 0;
                talkFromEye = player.cameraEye();
                talkFromTarget = player.cameraTarget();
                TT_LOG("quest camera: npc %lu choice %u", static_cast<unsigned long>(talkCameraNpc), unsigned(choiceCamera));
            }
            talkCameraAge = fminf(1.0f, talkCameraAge + dt);
            const float blend = 1 - (1 - talkCameraAge) * (1 - talkCameraAge);
            auto mixTalk = [blend](const C3D_FVec& a, const C3D_FVec& b) {
                return FVec3_New(a.x + (b.x-a.x)*blend, a.y + (b.y-a.y)*blend, a.z + (b.z-a.z)*blend);
            };
            player.overrideCameraCollide(mixTalk(talkFromEye, talkEye), mixTalk(talkFromTarget, talkTarget));
        } else {
            talkCameraNpc = 0;
        }
        // ---- end qafix ----
        if (trolley.seated() || minigame.inScene() || golf.active()) {
            float sx, sy, sz, sh;
            C3D_FVec eye, target;
            if (golf.active()) {
                golf.toonPose(&sx, &sy, &sz, &sh);
                golf.camera(&eye, &target);
            } else if (minigame.inScene()) {
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
        tt::world::ToonAnim anim = player.animState(&animRate);
        // ---- water: swimming in Donald's Dock / Acorn Acres (world/water.*) ----
        static tt::world::Water water;
        if (place.zoneId() != water.zone()) water.setZone(place.zoneId(), renderer);
        const bool swimming = !battle.inBattle() && !featureBusyForWater(fishing.active(), trolley.seated(),
                                                                       minigame.inScene(), transition.active()) &&
                              water.update(player.x(), player.y(), player.z(), player.cameraEye().z,
                                           anim != tt::world::kAnimNeutral, netStarted ? &net : nullptr, renderer);
        if (swimming) anim = tt::world::kAnimSwim; // Toon.enterSwim loops 'swim'
        // ---- end water ----
        // In battle the movie picks the clip (and the autotest's "clip: *" steps play one).
        const bool clipTest = autotest.active() && !strncmp(autotest.label(), "clip: ", 6);
        const bool featureAnimation = fishing.active() || trolley.seated() || minigame.inScene() ||
            transition.active();
        if (!racing.driving() && !battle.inBattle() && !clipTest && !featureAnimation && !localToon.actionActive()) localToon.play(anim,animRate);
        // ---- racing2 ----
        if (racing.driving())
            racing.animate(localToon);
        // ---- end racing2 ----
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
        if(!battle.inBattle() && !clipTest) {
            localToon.setHeadOnly(false);localToon.setBodyPose();
            if(minigame.inScene()) minigame.applyAnimation(localToon);
            else if(trolley.seated()) trolley.applyAnimation(localToon,tt::net::plat::nowMs());
            else if(fishing.active()) fishing.applyAnimation(localToon);
            else if(transition.active()) {
                transition.applyAnimation(localToon, dt);
            }
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
        if(localToon.popEmoteFinished() && netStarted) net.sendEmote(-1);
        tt::prof::lap(tt::prof::kPlayer);

        // Streets stream per visgroup; crossing into another server cell changes interest.
        place.update(player.x(), player.y(), player.z(), onStreetCell, nullptr);
        // ---- tutorial: the server owns our tutorial zones (enterTutorial). A street-cell change
        // from the start zone's playground would move us out of Tutorial Tom's interior before
        // its place loads, and the tutorial street is a single cell anyway. ----
        if (gStreetCell.pending && netStarted && net.tutorialZones().active()) {
            gStreetCell.pending = false;
        } else if (gStreetCell.pending && netStarted && net.inZone() && gStreetCell.cell != net.zoneId()) {
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
        if (transition.active() || minigame.active() || trolley.seated()) {
            // A transition is running, or we are on the trolley / away at a minigame: no exits.
        // ---- tutorial ----
        } else if (netStarted && net.inTutorial() && place.zoneId() == tt::world::kTutorialStreet &&
                   exitHere.kind == tt::world::Exit::Tunnel && exitHere.targetZone != lastExitZone) {
            // TutorialStreet.handleEnterTunnel posts stopTutorial before TTStreet starts the
            // normal TTC tunnel.  allDone tears down dynamic objects; the next location is TTC.
            if (net.sendTutorialAllDone()) {
                tutorial.stop();
                travelTarget = tt::world::kToontownCentral;
                TT_LOG("tutorial: tunnel exit to Toontown Central");
            }
        // ---- end tutorial ----
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
        if (uiState.teleportRequest && (minigame.active() || trolley.seated() || battle.active())) {
            // The Shticker Book (and its map teleport) is disabled on the trolley, in a minigame
            // or its gag purchase, and in battle upstream; leaving here would strand the session.
            TT_LOG("teleport to %lu refused (busy)", static_cast<unsigned long>(uiState.teleportRequest));
            uiState.teleportRequest = 0;
        } else if (uiState.teleportRequest && transition.idle()) {
            // Map tab teleport to a playground (upstream: Shticker Book map page).
            if (tt::world::Place::available(uiState.teleportRequest)) {
                transition.startTeleportOut(uiState.teleportRequest, player, netStarted ? &net : nullptr);
            }
            uiState.teleportRequest = 0;
        } else if (travelTarget && transition.idle()) {
            const bool leavingLevel = level.active();
            if ((travelTarget != place.zoneId() || leavingLevel) && (!netStarted || net.inZone())) {
                if (exitHere.kind == tt::world::Exit::Door) {
                    u32 doorDoId = 0;
                    if (netStarted) {
                        u32 block = 0;
                        if (!strncmp(exitHere.name, "sz", 2) || !strncmp(exitHere.name, "tb", 2)) {
                            block = static_cast<u32>(strtoul(exitHere.name + 2, nullptr, 10));
                        } else if (place.kind() == tt::world::PlaceKind::Interior) {
                            block = place.zoneId() % 100;
                        }
                        if (block > 0) {
                            doorDoId = net.findDoor(block);
                        }
                    }
                    transition.startDoorOut(travelTarget, exitHere, player, place, doorDoId, netStarted ? &net : nullptr);
                } else {
                    transition.startTunnelOut(travelTarget, exitHere, player, place, netStarted ? &net : nullptr);
                }
            }
        }
        if (uiState.homeRequest && transition.idle() && netStarted) {
            if (!net.requestHome()) {
                // Stock Open Toontown never generates an EstateManager (its estate AI classes
                // are empty stubs), so there is no home to go to on that server.
                toast = "Estates aren't available on this server.";
                toastAge = 0.0f;
            }
            uiState.homeRequest = false;
        }
        uiState.teleportRequest = 0;

        transition.update(dt, player, place, netStarted ? &net : nullptr);

        if (transition.readyToLoad()) {
            const u32 dest = transition.targetZone();
            const u32 fromZone = transition.fromZone();
            remoteToons.clear();
            if (place.load(dest)) {
                // Facilities are entered by elevator, never by a transition, so any arrival means we
                // left one (won, went sad, teleported): end it even if its gone events never come.
                if (level.active()) {
                    TT_LOG("level: left for zone %u", unsigned(dest));
                    level.shutdown();
                }
                zoneActors.loadPoints(place.pointsPath());
                cchars.setZone(dest);
                fishing.setZone(dest, place.pointsPath());
                trolley.setZone(dest);
                cogBuildings.setZone(dest);
                cogBuildings.syncAnchors(place);
                suits.setZone(dest);
                float ax, ay, az, ah;
                place.arrival(fromZone, &ax, &ay, &az, &ah);
                player.reset(ax, ay, az, ah);
                if (netStarted) net.changeZone(dest);
                place.finishLoading(ax, ay, az, onStreetCell, nullptr);
                const tt::world::Exit arrivalExit = place.exitAt(ax, ay, az);
                lastExitZone = arrivalExit.kind != tt::world::Exit::None ? arrivalExit.targetZone : fromZone; // don't bounce straight back
                if (tt::world::hoodOf(dest) != tt::world::hoodOf(fromZone) ||
                    tt::world::placeKind(dest) != tt::world::placeKind(fromZone)) {
                    playPlaceMusic(dest);
                }
                loadAmbient(dest);
                transition.notifyLoaded(dest, place, player, netStarted ? &net : nullptr);
                continue;
            }
            place.load(fromZone); // fall back to where we were
            transition.reset();
        }
        if (!transition.active()) {
            lastExitZone = exitHere.kind != tt::world::Exit::None ? exitHere.targetZone : 0;
        }
        toastAge += dt;
        if (toastAge > 3.0f) toast = nullptr;

        tt::prof::lap(tt::prof::kPlace);
        // ---- tutorial ----
        // enterTutorial can arrive while the front end still owns the event queue, so the world
        // loads Tutorial Tom's interior (or whichever tutorial place the server has us in) here.
        if (netStarted && net.tutorialZones().active()) {
            static u32 tutorialDiagMs = 0;
            const u32 diagNow = tt::net::plat::nowMs();
            if (diagNow - tutorialDiagMs > 2000 &&
                place.zoneId() != net.tutorialZones().toCanonical(net.zoneId())) {
                tutorialDiagMs = diagNow;
                TT_LOG("tutorial: place %lu, server zone %lu -> %lu, in zone %d, transition idle %d",
                       static_cast<unsigned long>(place.zoneId()), static_cast<unsigned long>(net.zoneId()),
                       static_cast<unsigned long>(net.tutorialZones().toCanonical(net.zoneId())),
                       net.inZone() ? 1 : 0, transition.idle() ? 1 : 0);
            }
        }
        if (netStarted && net.inZone() && net.tutorialZones().active() && transition.idle()) {
            const u32 canonical = net.tutorialZones().toCanonical(net.zoneId());
            if (tt::world::TutorialZones::isTutorialPlace(canonical) && place.zoneId() != canonical)
                loadTutorialPlace(net.zoneId());
        }
        // ---- end tutorial ----
        if (netStarted) {
            const u32 nowMs = tt::net::plat::nowMs();
            net.update(nowMs);
            tt::net::Event ev;
            while (net.pollEvent(ev)) {
                // ---- tutorial ----
                if (ev.type == tt::net::EventType::TutorialEnter) {
                    loadTutorialPlace(net.zoneId());
                } else if (ev.type == tt::net::EventType::TutorialInterior) {
                    tutorial.start(tt::quest::ScriptId::TutorialMickey, false);
                } else if (ev.type == tt::net::EventType::BlockerMovie) {
                    tt::net::Client::BlockerMovie movie;
                    while (net.pollBlockerMovie(movie)) {
                        if (movie.avId == net.avatarId() && movie.mode == 1)
                            tutorial.start(tt::quest::ScriptId::TutorialBlocker, false);
                    }
                } else if (ev.type == tt::net::EventType::Quests && autotest.tutorialPath()) {
                    const tt::net::OwnQuests& quests = net.ownQuests();
                    TT_LOG("tutorial: quest update %d carried", quests.count);
                    for (int i = 0; i < quests.count; ++i) {
                        TT_LOG("tutorial: quest %lu from %lu to %lu progress %lu",
                               static_cast<unsigned long>(quests.quests[i].questId),
                               static_cast<unsigned long>(quests.quests[i].fromNpc),
                               static_cast<unsigned long>(quests.quests[i].toNpc),
                               static_cast<unsigned long>(quests.quests[i].progress));
                    }
                }
                // ---- end tutorial ----
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
                golf.handleEvent(ev, net, nowMs);
                boat.handleEvent(ev, net);
                suits.handleEvent(ev, net, nowMs);
                cogBuildings.handleEvent(ev, net);
                cogInterior.handleEvent(ev, net, nowMs);
                level.handleEvent(ev, net);
                racing.handleEvent(ev, net);
                estate.handleEvent(ev, net);
                pets.handleEvent(ev);
                if (ev.type == tt::net::EventType::ClosetUp) {
                    net.sendClosetEnter(ev.doId);
                    ui.closet().open(net, nowMs);
                }
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
            // ---- clerks: ordered replies retain COMPLETE even when CLEAR follows ----
            serviceClerks.update(net, nowMs);
            if (serviceClerks.active())
                ui.book().close();
            // ---- end clerks ----
            shop.update(net, nowMs);
            tailor.update(net, nowMs);
            ui.closet().update(net);
            cogInterior.update(net, player.x(), player.y(), player.z(), nowMs);
            if (const char* m = cogInterior.popMessage()) {
                toast = m;
                toastAge = 0.0f;
            }
            tt::prof::lap(tt::prof::kNet);
            // ---- racing2 ----
            // Racing was already simulated once, before the player update.
            // ---- end racing2 ----
            tt::prof::lap(tt::prof::kNet);
            estate.update(net, player.x(), player.y(), player.z(), nowMs);
            tt::prof::lap(tt::prof::kEstate);
            pets.update(net);
            // ---- tutorial ----
            tutorial.update(dt);
            if (tutorial.active() && (in.down & KEY_A)) tutorial.advance();
            // ---- end tutorial ----
            const bool facilityActive = cogInterior.active() || level.active();
            if (cogInterior.active() && cogInterior.collision()) player.setCollision(cogInterior.collision());
            else if (level.active() && level.collision()) player.setCollision(level.collision());
            else if (estate.active() || minigame.active() || golf.active()) player.setCollision(nullptr); // flat stages
            else player.setCollision(&place.collision());
            if (racing.active() && racing.collision()) player.setCollision(racing.collision());
            float rx, ry, rz, rh;
            // ---- racing2 ----
            if (racing.spawnPose(&rx, &ry, &rz, &rh)) {
                player.reset(rx, ry, rz, rh);
                C3D_FVec eye, target;
                racing.camera(&eye, &target);
                player.overrideCamera(eye, target);
            }
            // ---- end racing2 ----
            if (const u32 raceZone = racing.popDestination()) {
                net.changeZone(raceZone);
            }
            if (facilityActive) {
                float sx, sy, sz, sh;
                if (cogInterior.spawnPose(&sx, &sy, &sz, &sh) ||
                    level.spawnPose(&sx, &sy, &sz, &sh)) {
                    player.reset(sx, sy, sz, sh);
                }
                if (cogInterior.elevatorSeatPose(&sx, &sy, &sz, &sh))
                    player.reset(sx, sy, sz, sh);
            }
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
                bool movingNowGame = false; // ---- minigame broadcast ----
                bool movingNow = !battle.active() &&
                                 (player.moving() || player.airborne() || anim != tt::world::kAnimNeutral);
                // On the boat (DDPlayground.enterOnBoat): setParent(SPDonaldsBoat), then poses are boat-local.
                float nx, ny, nz, nh;
                player.netPose(&nx, &ny, &nz, &nh);
                // ---- minigame broadcast: LocalAvatar keeps broadcasting setSmPosHpr in the
                // game's scene, so the others' clients see our minigame toon, not our playground pose.
                static float lastGamePose[4] = {};
                const bool inMinigame = minigame.inGame();
                if (inMinigame) {
                    minigame.toonPose(&nx, &ny, &nz, &nh);
                    const float dx = nx - lastGamePose[0], dy = ny - lastGamePose[1], dz = nz - lastGamePose[2];
                    const float dh = nh - lastGamePose[3];
                    movingNowGame = dx * dx + dy * dy + dz * dz > 1e-4f || dh * dh > 1e-2f;
                    lastGamePose[0] = nx, lastGamePose[1] = ny, lastGamePose[2] = nz, lastGamePose[3] = nh;
                }
                // ---- end minigame broadcast ----
                if (inMinigame) movingNow = movingNowGame; // ---- minigame broadcast ----
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
                const char* netState = trolley.seated()?trolley.toonNetworkState(nowMs):fishing.active()?"off":minigame.inScene()?minigame.toonNetworkState():
                    transition.active() ? transition.toonNetworkState() : water.swimming() ? "swim" : player.airborne() ? "jumpAirborne" : "run";
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
                // ---- clerks: touch the real anchored NPC, once per contact ----
                u32 serviceNpc = 0;
                const uint16_t serviceClasses[] = {
                    tt::dc::DistributedNPCPetclerk::kClassId, tt::dc::DistributedNPCKartClerk::kClassId,
                    tt::dc::DistributedNPCFisherman::kClassId, tt::dc::DistributedNPCPartyPerson::kClassId};
                for (uint16_t type : serviceClasses) {
                    serviceNpc = zoneActors.touchingNpc(player.x(), player.y(), player.z(), net, type);
                    if (serviceNpc)
                        break;
                }
                // A touch counts once it is acted on: one made while busy (a teleport still landing
                // inside the NPC's sphere, another panel open) fires when the toon is free.
                const bool npcFree = !serviceClerks.active() && !shop.active() && !tailor.active() && !talk.locked() &&
                                     transition.idle();
                if (serviceNpc && serviceNpc != lastTouchedServiceClerk && npcFree)
                    net.sendServiceClerkEnter(serviceNpc);
                if (npcFree || !serviceNpc) lastTouchedServiceClerk = serviceNpc;
                // ---- end clerks ----
                const u32 clerk = zoneActors.touchingNpc(player.x(), player.y(), player.z(), net,
                                                         tt::dc::DistributedNPCClerk::kClassId);
                if (clerk && clerk != lastTouchedClerk && npcFree) net.sendClerkAvatarEnter(clerk);
                if (npcFree || !clerk) lastTouchedClerk = clerk;
                const u32 tailorNpc = zoneActors.touchingNpc(player.x(), player.y(), player.z(), net,
                                                             tt::dc::DistributedNPCTailor::kClassId);
                if (tailorNpc && tailorNpc != lastTouchedTailor && npcFree) net.sendTailorAvatarEnter(tailorNpc);
                if (npcFree || !tailorNpc) lastTouchedTailor = tailorNpc;
                // Quest givers: walking into one starts the conversation (DistributedNPCToon.avatarEnter).
                u32 giver = zoneActors.touchingNpc(player.x(), player.y(), player.z(), net,
                                                   tt::dc::DistributedNPCToon::kClassId);
                if (!giver) {
                    giver = zoneActors.touchingNpc(player.x(), player.y(), player.z(), net,
                                                   tt::dc::DistributedNPCSpecialQuestGiver::kClassId);
                }
                if (giver && giver != lastTouchedQuestNpc && npcFree) {
                    talk.setLocal(net.avatarId(), net.avatarName());
                    talk.begin(giver);
                    TT_LOG("quest: talking to NPC %u", unsigned(giver));
                }
                if (npcFree || !giver) lastTouchedQuestNpc = giver;
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
        // [interior life]
        if (place.zoneId() != interiorLife.zone()) interiorLife.setZone(place.zoneId());
        {
            tt::world::InteriorLife::ToonLine line;
            if (interiorLife.update(dt, player.x(), player.y(), player.z(), net, tt::net::plat::nowMs(), &line)) {
                // The toon's knock-knock lines go to its own chat balloon (setChatAbsolute).
                if (!netStarted || line.avatarId == net.avatarId()) {
                    myPhrase = line.phrase;
                    myPhraseAge = 0.0f;
                } else {
                    remoteToons.say(line.avatarId, line.phrase);
                }
            }
        }
        // [end interior life]
        if (netStarted && net.inZone()) {
            const u32 nowMs = tt::net::plat::nowMs();
            if (const u32 hood = level.popWinDestination()) {
                if (!uiState.teleportRequest) uiState.teleportRequest = hood; // the factory is won
            }
            if (level.active()) {
                // Facility cogs walk/chase (level.update ran before the toon moved, see above);
                // touching one (or a battle blocker) fights.
                const tt::world::LevelBattleAction act = levelAction;
                if (act.cogDoId) {
                    battle.requestLevelBattle(net, act.cogDoId, act.x, act.y, act.z, act.h, player.x(), player.y(),
                                              player.z(), player.headingDeg(), nowMs);
                } else if (act.joinBattle) {
                    battle.requestJoin(net, act.joinBattle, player.x(), player.y(), player.z(), player.headingDeg(),
                                       nowMs);
                }
                // DistributedLevelBattle.lockLevelViz: keep the battle's zone in view.
                const tt::net::Battle* nb = battle.active() ? net.findBattle(battle.battleId()) : nullptr;
                if (nb && level.ownsLevel(nb->levelDoId)) level.lockVisibility(nb->zoneId);
                else level.unlockVisibility();
            }
            // Bumping into a walking cog starts a fight (DistributedSuit.handleEnterCollision);
            // walking into someone else's fight joins it.
            if (!battle.active() && transition.idle() && !shop.active() && !fishing.active() && !minigame.active() &&
                !trolley.seated()) {
                uint32_t facilityCog = 0;
                float facilityX = 0, facilityY = 0, facilityZ = 0, facilityH = 0;
                bool hasFacilityCog = cogInterior.battleTarget(net, &facilityCog, &facilityX, &facilityY, &facilityZ, &facilityH);
                const float fdx = player.x() - facilityX, fdy = player.y() - facilityY;
                if (hasFacilityCog && fdx * fdx + fdy * fdy < 36.0f) {
                    battle.requestBattle(net, facilityCog, facilityX, facilityY, facilityZ, facilityH,
                                         player.x(), player.y(), player.z(), player.headingDeg(), nowMs);
                }
                if (const u32 cog = suits.touching(player.x(), player.y(), player.z())) {
                    float cx, cy, cz, ch;
                    if (suits.pose(cog, &cx, &cy, &cz, &ch)) {
                        battle.requestBattle(net, cog, cx, cy, cz, ch, player.x(), player.y(), player.z(),
                                             player.headingDeg(), nowMs);
                    }
                }
                battle.checkJoin(net, player.x(), player.y(), player.z(), player.headingDeg(), nowMs);
            }
            if (!battle.active() && !minigame.active() && !golf.active() && transition.idle()) {
                trolley.update(net, player.x(), player.y(), player.z(), net.ownStats().hp, nowMs);
            }
            if (!golf.active()) {
                const tt::net::GolfKart* kart = net.golfKart();
                if (kart) {
                    const float dx = player.x() - kart->x, dy = player.y() - kart->y;
                    if (dx * dx + dy * dy < 9.0f && (in.down & KEY_A)) net.sendGolfKartBoard();
                }
            }
            cogBuildings.update(net, player.x(), player.y(), player.z(), nowMs);
            if (const u32 dest = cogBuildings.popDestination()) {
                // The elevator left with us: into the Cog building / facility zone. The playground
                // or HQ stays loaded underneath; the interior runtimes take over when their objects
                // generate (cog_interior / level: factories, mints, DA offices, country clubs).
                TT_LOG("elevator: riding to zone %lu", static_cast<unsigned long>(dest));
                net.sendSetParent(1); // hidden, like the trolley hand-off
                net.changeZone(dest);
                remoteToons.clear();
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
                tt::prof::lap(tt::prof::kNet);
                minigame.update(net, minigamePad, dt, nowMs);
                tt::prof::lap(tt::prof::kMinigame);
                // ---- minigame Cogs: real suit actors through the shared Suits system ----
                {
                    // Local-only actor ids, far above any Astron doId the server allocates.
                    constexpr u32 kMinigameCogBase = 0xFFF00000u;
                    static int shownCogs = 0;
                    tt::game::MinigameCog mgCogs[tt::game::kMaxMinigameCogs];
                    const int numCogs = minigame.cogs(mgCogs, tt::game::kMaxMinigameCogs);
                    for (int i = 0; i < numCogs; ++i) {
                        const tt::game::MinigameCog& c = mgCogs[i];
                        suits.setExternal(kMinigameCogBase + u32(i), c.code, 0, c.x, c.y, c.z, c.h, c.walking, true);
                    }
                    for (int i = numCogs; i < shownCogs; ++i) suits.removeExternal(kMinigameCogBase + u32(i));
                    shownCogs = numCogs;
                }
                // ---- end minigame Cogs ----
                if (const u32 home = minigame.popReturn()) {
                    // Back from the game: teleport into the playground (Playground.enterTeleportIn).
                    player.setCollision(&place.collision());
                    net.changeZone(home);
                    float ax, ay, az, ah;
                    place.arrival(0, &ax, &ay, &az, &ah);
                    player.reset(ax, ay, az, ah);
                    net.sendSetParent(tt::net::kParentRender);
                    transition.startTeleportIn(player, &net);
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
                tt::prof::lap(tt::prof::kNet);
                golf.update(net, minigamePad, in.down, dt, nowMs);
                tt::prof::lap(tt::prof::kGolf);
                if (const u32 home = golf.popReturn()) {
                    golf.shutdown();
                    net.changeZone(home);
                    net.sendSetParent(tt::net::kParentRender);
                    player.setCollision(&place.collision());
                    float ax, ay, az, ah;
                    place.arrival(0, &ax, &ay, &az, &ah);
                    player.reset(ax, ay, az, ah);
                }
            }
            if (autotest.active()) {
                const char* lbl = autotest.label();
                int forced = -1;
                if (!strncmp(lbl, "battle: heal", 12)) forced = 0;
                else if (!strncmp(lbl, "battle: trap", 12)) forced = 1;
                else if (!strncmp(lbl, "battle: lure", 12)) forced = 2;
                else if (!strncmp(lbl, "battle: sound", 13)) forced = 3;
                else if (!strncmp(lbl, "battle: throw", 13)) forced = 4;
                else if (!strncmp(lbl, "battle: squirt", 14)) forced = 5;
                else if (!strncmp(lbl, "battle: drop", 12)) forced = 6;
                // [cog attacks]
                else if (!strncmp(lbl, "battle: cog attacks", 19)) forced = tt::world::Battle::kAutoPass;
                battle.setAutoPlay(true, forced);

                if (forced >= 0) {
                    static float battleWaitTime = 0.0f;
                    if (autotest.stepBegan()) battleWaitTime = 0.0f;
                    battleWaitTime += dt;
                    if ((!battle.active() || battle.phase() < tt::world::BattlePhase::Input) && battleWaitTime < 15.0f) {
                        autotest.hold(dt);
                    }
                }
            } else {
                battle.setAutoPlay(false);
            }
            // ---- padnav: the "pad: battle" steps choose with buttons; "pad: battle rounds" finishes ----
            if (autotest.active() && !strncmp(autotest.label(), "pad: battle", 11) &&
                strcmp(autotest.label(), "pad: battle rounds") != 0) {
                battle.setAutoPlay(false);
            }
            // ---- end padnav ----
            battle.update(net, nowMs, dt, tt::world::CogPoseSource{tt::world::SuitActors::poseThunk, &suits});
            // ---- tutorial ----
            // Movie.playTutorialReward waits for its final acknowledgement instead of the normal
            // RewardPanel timer.  The compact client presents that acknowledgement on A.
            if (battle.tutorialRewardPending() && (in.down & KEY_A)) {
                battle.finishTutorialReward(net);
                TT_LOG("tutorial: acknowledged Flunky reward");
            }
            // ---- end tutorial ----
            for (int i = 0; i < battle.numSuitPoses(); i++) {
                const tt::world::BattleActorPose& p = battle.suitPose(i);
                // A level battle can start in an adjacent room whose Cog has not been drawn yet.
                // BattleBase creates every member regardless of camera visibility, so materialize
                // the actor before applying the battle-owned pose.
                tt::net::Client::CogIdentity cog;
                if (net.cogIdentity(p.doId, &cog) && cog.hasDna) {
                    suits.setExternal(p.doId, cog.name, cog.levelDist,
                                      p.x, p.y, p.z + p.hop, p.h,
                                      false, !p.hidden, cog.skelecog);
                }
                suits.overridePose(p.doId, p.x, p.y, p.z + p.hop, p.h,
                                  p.hidden, p.flash, p.anim, p.clipArg,
                                  p.clipStartMs);
            }
            battleFx.update(dt, nowMs, &battle, localLook.valid() ? &localLook : nullptr);
            if (const char* m = battle.popMessage()) {
                toast = m;
                toastAge = 0.0f;
            }
            if (battle.popWentSad()) {
                // DistributedToon.died: ZoneUtil.getSafeZoneId(defaultZone), the toon's own playground
                // (a Cog HQ maps to its neighbour: HQToSafezone).
                u32 hood = tt::world::hoodOf(net.ownStats().defaultZone);
                if (hood == 11000) hood = 5000;       // Sellbot HQ -> Daisy Gardens
                else if (hood == 12000) hood = 9000;  // Cashbot HQ -> Donald's Dreamland
                else if (hood == 13000) hood = 3000;  // Lawbot HQ -> The Brrrgh
                else if (hood == 10000) hood = 1000;  // Bossbot HQ -> Donald's Dock
                transition.startDied(hood, player, &net);
            }
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
        // ---- clerks: publish preview resources only after the GPU fence ----
        int8_t clerkKartDna[9];
        clerkKartPreview.select(serviceClerks.kartPreview(clerkKartDna) ? clerkKartDna : nullptr);
        clerkKartPreview.pump();
        serviceClerks.setPreviewFailed(clerkKartPreview.failed());
        if (clerkKartPreview.active())
            player.overrideCamera(FVec3_New(player.x() + 18, player.y() - 18, player.z() + 12),
                                  FVec3_New(player.x() + 5, player.y() + 6, player.z() + 2));
        // ---- end clerks ----
        toonCache.pump(); // previous GPU frame is complete: publish streamed poses
        level.pumpAssets();
        pets.pumpAssets();
        cogInterior.pumpAssets();
        minigame.pumpAssets();
        suits.pumpAssets();
        golf.pumpAssets();
        racing.pumpAssets();
        estate.pumpAssets();
        tt::prof::lap(tt::prof::kLoads);
        if (shotName) {
            // Previous frame is complete after FrameBegin; dump it before clearing.
            char path[96];
            snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_top.bin", shotName);
            tt::shot::dumpTarget(renderer.topLeft(), path);
            snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_bottom.bin", shotName);
            tt::shot::dumpTarget(bottom, path);
            shotName = nullptr;
            tt::prof::lap(tt::prof::kCapture);
        }
        renderer.resetStats();
        C3D_Mtx m;
        const int eyes = stereoActive ? 2 : 1;
        for (int e = 0; e < eyes; e++) {
            C3D_RenderTarget* target = e == 0 ? renderer.topLeft() : renderer.topRight();
            const int eyeSign = stereoActive ? (e == 0 ? -1 : 1) : 0;
            // Indoors (interiors, facilities) gaps show OTPGlobals.DefaultBackgroundColor (0.3 grey),
            // not the playground sky blue.
            const bool indoors = level.active() || tt::world::placeKind(place.zoneId()) == tt::world::PlaceKind::Interior;
            renderer.setBackground(indoors ? 0x4C4C4CFF : 0x6AB0D9FF);
            renderer.beginWorld(target, player.cameraEye(), player.cameraTarget(), slider / 3.0f, eyeSign);

            if (golf.active()) {
                golf.draw(renderer);
                tt::prof::lap(tt::prof::kGolfDraw);
            } else if (minigame.active()) {
                // ---- qafix: Photo uses the real hood, not a viewfinder model as scenery ----
                if (minigame.photoScene()) place.draw(renderer, player.cameraEye());
                // ---- end qafix ----
                minigame.draw(renderer);
                suits.draw(renderer); // ---- minigame Cogs (MinigameSession::cogs) ----
                {
                    // The other participants a game places from its own state (Race, Pattern).
                    tt::game::MinigamePlayer placed[tt::net::kMinigamePlayers];
                    const int numPlaced = minigame.placedPlayers(net, placed, tt::net::kMinigamePlayers);
                    for (int i = 0; i < numPlaced; ++i)
                        remoteToons.placeExternal(placed[i].doId, placed[i].x, placed[i].y, placed[i].z, placed[i].h);
                    if (numPlaced > 0) {
                        float tx, ty, tz, th;
                        minigame.toonPose(&tx, &ty, &tz, &th);
                        remoteToons.draw(renderer, tx, ty);
                    }
                }
                if (minigame.inGame() && minigame.smoothedPlayers()) {
                    // The other participants, at their broadcast poses (startSmooth games).
                    float tx, ty, tz, th;
                    minigame.toonPose(&tx, &ty, &tz, &th);
                    remoteToons.draw(renderer, tx, ty);
                }
                tt::prof::lap(tt::prof::kMinigameDraw); // the playground is hidden while we are away
            } else if (level.active()) {
                level.draw(renderer, player.cameraEye());
            } else if (racing.active()) {
                racing.draw(renderer, net);
                tt::prof::lap(tt::prof::kRacingDraw);
            } else if (estate.active()) {
                estate.draw(renderer, net);
                tt::prof::lap(tt::prof::kEstateDraw);
            } else if (cogInterior.active()) {
                cogInterior.draw(renderer);
            } else {
                place.draw(renderer, player.cameraEye());
            }
            // ---- clerks: preview the selected body/equipped accessory components ----
            if (clerkKartPreview.active())
                clerkKartPreview.draw(renderer, player.x() + 5, player.y() + 6, player.z(),
                                      float(tt::net::plat::nowMs() % 12000) * .03f);
            // ---- end clerks ----
            pets.draw(renderer, net);
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
            if (squishAge >= 0.0f && squishAge < 1.7f) {
                // Toon.enterSquish: to (2, 2, 0.025) in 0.1 s, hold 1 s, then after 0.4 s pop
                // 1.4 -> 0.8 -> 1 (0.1 / 0.05 / 0.033 s).
                auto smooth = [](float u) { return u * u * (3 - 2 * u); };
                const float t = squishAge;
                float sx = 1, sz = 1;
                if (t < 0.1f) {
                    const float u = smooth(t / 0.1f);
                    sx = 1 + u, sz = 1 + (0.025f - 1) * u;
                } else if (t < 1.5f) {
                    sx = 2, sz = 0.025f;
                } else if (t < 1.6f) {
                    const float u = smooth((t - 1.5f) / 0.1f);
                    sx = 2 + (1.4f - 2) * u, sz = 0.025f + (1.4f - 0.025f) * u;
                } else if (t < 1.65f) {
                    sx = sz = 1.4f + (0.8f - 1.4f) * smooth((t - 1.6f) / 0.05f);
                } else {
                    sx = sz = 0.8f + 0.2f * smooth(std::min(1.0f, (t - 1.65f) / 0.033f));
                }
                Mtx_Scale(&m, sx, sx, sz);
            }
            // Like upstream's camera-too-close fade: don't draw the toon around the camera.
            // The Toon tab colour picker is an offline preview; online the server's DNA wins.
            if (!racing.driving() && player.cameraDistance() > kHideToonCameraDistance && transition.isToonVisible()) { // ---- racing2 ----
                if (transition.toonScale() != 1.0f) {
                    const float ts = transition.toonScale();
                    Mtx_Scale(&m, ts, ts, ts);
                }
                localToon.draw(renderer, m, localLookFromServer ? nullptr : &tints);
            }
            // ---- racing2 ----
            if (racing.driving() && !racing.active())
                racing.draw(renderer, net);
            if (racing.driving())
                racing.drawToons(renderer, localToon, remoteToons, net.avatarId());
            else
                remoteToons.draw(renderer, player.x(), player.y());
            // ---- end racing2 ----
            tt::prof::lap(tt::prof::kDrawToons);
            zoneActors.draw(renderer, player.cameraEye());
            // [interior life]
            interiorLife.draw(renderer);
            // [end interior life]
            boat.draw(renderer);
            cchars.draw(renderer);
            if (netStarted && !golf.active()) {
                trolley.draw(renderer, net, tt::net::plat::nowMs());
                golf.drawKart(renderer, net);
            }
            fishing.draw(renderer);
            suits.draw(renderer);
            if (battle.active()) {
                battleFx.draw(renderer, &battle, localLook.valid() ? &localLook : nullptr);
            }
            if (netStarted) cogBuildings.draw(renderer, net);
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
            transition.drawWorld(renderer);
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
            // ---- racing2 ----
            if (racing.active())
                racing.addBalloons(balloons, proj, view, net);
            else
                remoteToons.addBalloons(balloons, proj, view);
            // ---- end racing2 ----
            {
                u32 talker = 0;
                const char* said = talk.speech(&talker);
                // ---- tutorial ----
                if (tutorial.active() && net.tutorialInterior().active && tutorial.text()[0]) {
                    talker = net.tutorialInterior().npcDoId;
                    said = tutorial.text();
                }
                // ---- end tutorial ----
                zoneActors.setSpeech(talker, said);
            }
            zoneActors.addBalloons(balloons, proj, view);
            cchars.addBalloons(balloons, proj, view);
            suits.addBalloons(balloons, proj, view);
            // [interior life]
            interiorLife.addBalloons(balloons, proj, view, localLook.valid() ? localLook.nametagHeight : 4.0f);
            // [end interior life]
            for (int i = 0; i < battle.numPopups(); i++) { // damage numbers over the fighters
                const tt::world::BattlePopup& p = battle.popup(i);
                float sx, sy, depth;
                if (tt::ui::projectToScreen(proj, view, p.x, p.y, p.z, &sx, &sy, &depth)) {
                    balloons.addSpeaker(sx, sy - p.rise * p.age, p.text, nullptr, 99.0f, 0.0f, p.color);
                }
            }
            balloons.draw(renderer.topLeft());
            if (stereoActive) balloons.draw(renderer.topRight());
            // [interior life]
            interiorLife.draw2D(proj, view);
            // [end interior life]
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
                tt::world::ToonDna hudDna;
                if (tt::world::parseNetString(net.avatarDna(), tt::net::kDnaSize, &hudDna)) {
                    hv.species = static_cast<int>(tt::world::species(hudDna)); // the laff meter's head
                }
            }
            hv.toast = toast;
            if (racing.driving() || racing.active()) {
                hv.racing = true;
                hv.raceSpeed = int(racing.speed() + 0.5f);
                hv.raceLap = racing.lap();
                hv.raceLaps = net.racingRace() ? net.racingRace()->laps : 3;
                hv.racePlace = racing.place();
            }
            hud.draw(renderer.topLeft(), hv);
            transition.drawFade(renderer, stereoActive);
            tt::prof::lap(tt::prof::kHud);
        }
        const struct mallinfo mi = mallinfo();
        // ---- racing2 ----
        static tt::ui::RacingView racingView;
        if (racing.active())
            racing.fillView(&racingView, net);
        // ---- end racing2 ----
        tt::ui::Context ctx{};
        if (racing.hudVisible()) { // ---- racing2 ----
            ctx.racing = racing.active() ? &racingView : nullptr; // ---- racing2 ----
            ctx.infoTitle = racing.hudTitle();
            ctx.infoLines[0] = racing.hudLine(0);
            ctx.infoLines[1] = racing.hudLine(1);
            ctx.infoLines[2] = racing.hudLine(2);
        }
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
        ctx.cpuMs = frameCpuMs();
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
            // ---- clerks: authoritative Doodle owner state in the Toon tab ----
            ctx.clerkProfile = &net.clerkProfile();
            for (int slot = 0; slot < tt::net::kMaxPets; ++slot) {
                const auto &pet = net.petSlot(slot);
                if (pet.active && pet.doId == net.clerkProfile().petId)
                    ctx.petName = pet.name;
            }
            // ---- end clerks ----
            ctx.stats = &net.ownStats();
            ctx.book = &net.ownBook();
            ctx.fish = &net.ownFish();
            ctx.districts = net.districts();
            ctx.numDistricts = net.numDistricts();
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
        // ---- qafix: trolley owns its panel before the minigame hand-off ----
        if (trolley.seated()) {
            ctx.infoTitle = "Trolley";
            ctx.infoLines[0] = "Waiting for the trolley to leave...";
            ctx.infoLines[1] = "B: hop off before departure";
        }
        // ---- end qafix ----
        if (cogInterior.seated()) {
            const int left = cogInterior.elevatorCountdown(net, tt::net::plat::nowMs());
            ctx.infoTitle = "Cog elevator";
            ctx.infoLines[0] = left >= 0 ? "Leaving in a few seconds..." : "Riding to the next floor...";
            ctx.infoLines[1] = "B: leave elevator";
            if (left >= 0) {
                snprintf(toastBuf, sizeof(toastBuf), "Cog elevator leaves in %d...  (B: leave)", left);
                toast = toastBuf;
                toastAge = 0.0f;
            }
        }
        if (minigame.active()) {
            ctx.infoTitle = minigame.title();
            for (int i = 0; i < 3; i++) ctx.infoLines[i] = minigame.line(i);
        }
        if (golf.active()) {
            ctx.infoTitle = golf.title();
            for (int i = 0; i < 3; i++) ctx.infoLines[i] = golf.line(i);
        }
        if (level.active() && level.loadingAssets()) {
            ctx.infoTitle = "Loading facility props";
            ctx.infoLines[0] = "Reading models and animations from SD...";
        }
        if ((golf.active() && golf.loadingAssets()) || (racing.active() && racing.loadingAssets()) ||
            (estate.active() && estate.loadingAssets())) {
            ctx.infoTitle = "Loading scene models";
            ctx.infoLines[0] = "Reading models from SD...";
        }
        if (minigame.inScene() && minigame.loadingAssets()) {
            ctx.infoTitle = "Loading minigame";
            ctx.infoLines[0] = "Reading models before ready...";
        }
        if (cogInterior.active() && cogInterior.loadingAssets()) {
            ctx.infoTitle = "Loading Cog building";
            ctx.infoLines[0] = "Reading floor models...";
        }
        if (suits.loadingAssets() && !ctx.infoTitle && !battle.inBattle()) {
            ctx.infoTitle = "Streaming Cog actors";
            ctx.infoLines[0] = "Reading models and clips...";
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
    golf.shutdown();
    racing.shutdown(); // ---- racing2: release before AssetLoader and ToonCache ----
    trolley.shutdown();
    boat.shutdown();
    // ---- clerks: release preview before the loader ----
    clerkKartPreview.shutdown();
    // ---- end clerks ----
    tt::world::AssetLoader::shutdown();
    suits.shutdown();
    // [interior life]
    interiorLife.shutdown();
    // [end interior life]
    battleFx.shutdown();
    if (netStarted) net.stop();
    if (netConnReady) netConn.shutdown();
    remoteToons.clear();
    toonCache.release(&localLook);
    toonCache.shutdown();
    transition.shutdown();
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
