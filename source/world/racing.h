#pragma once
#include "ui/hud.h"
#include "ui/racing_view.h"
#include "world/asset_batch.h"
#include "world/clerk_kart_preview.h"
#include "world/player.h"
#include "world/racing_track.h"
#include "world/race_town.h"
#include "world/remote_toons.h"

namespace tt::world {
class Racing {
  public:
    void handleEvent(const net::Event &event, const net::Client &client);
    void update(net::Client &client, const Input &input, float dt, float x, float y, float z,
                uint32_t now);
    void draw(render::Renderer &renderer, const net::Client &client);
    void drawToons(render::Renderer &renderer, ToonAvatar &local, const RemoteToons &remote,
                   uint32_t own);
    void animate(ToonAvatar &avatar) const;
    void camera(C3D_FVec *eye, C3D_FVec *target) const;
    void fillView(ui::RacingView *view, const net::Client &client) const;
    // RaceGUI race mode + the speedometer for the top-screen HUD (inactive outside a race).
    void fillHud(ui::RaceHud *hud, const net::Client &client) const;
    void addBalloons(ui::Balloons &balloons, const C3D_Mtx &proj, const C3D_Mtx &view,
                     const net::Client &client) const;
    void shutdown();
    void pumpAssets();
    bool loadingAssets() const { return assetBatch_.loading(); }
    bool active() const { return race_ != 0; }
    bool urban() const { return town_.urban(); } // loadUrbanTrack: fog, far 650
    // The urban track skyline after the sky, before the track (DistributedRace.loadUrbanTrack).
    void drawBackground(render::Renderer &renderer) { town_.drawSkyline(renderer, physics_.position.x, physics_.position.y); }
    bool driving() const { return active() || seated_ != 0; }
    bool hudVisible() const { return driving() || refusalUntil_ > now_; }
    const CollisionWorld *collision() const { return collision_.loaded() ? &collision_ : nullptr; }
    uint32_t popDestination();
    bool spawnPose(float *x, float *y, float *z, float *h);
    float speed() const { return physics_.speed(); }
    uint8_t lap() const { return uint8_t(std::max(0, progress_.laps) + 1); }
    uint8_t place() const { return place_; }
    const char *hudTitle() const { return "Goofy Speedway"; }
    const char *hudLine(int i) const { return i >= 0 && i < 3 ? hudLines_[i] : ""; }
    const racing::Track &metadata() const { return metadata_; }
    const racing::Vehicle &vehicle() const { return physics_; }
    void autopilot(Input *input) const;

  private:
    struct Racer {
        ClerkKartPreview kart;
        ToonAvatar seated;
        uint32_t avatar = 0, vehicle = 0;
        float x = 0, y = 0, z = 0, h = 0;
        racing::Vector velocity;
        int body = 0, lod = 0;
        int bodyColor = -1, accessoryColor = -1; // KartDNA colour indices
        bool initialized = false;
    } racers_[4];
    // RaceGUI's lap timer: maxLapHit and the start of its lap (raceTimeDelta), per-lap times.
    int maxLapHit_ = 0;
    uint32_t lapStartMs_ = 0;
    float lapSeconds_[ui::RaceHud::kMaxLaps] = {};
    int lapNotice_ = 0; // last lap notice: n = "Lap n", -1 = "Final Lap!"
    uint32_t lapNoticeMs_ = 0;
    bool photoFinish_ = false;
    void loadAssets(uint16_t track);
    void unloadAssets();
    void board(net::Client &client, const Input &input, float dt, float x, float y, float z);
    BasicAssetBatch<8> assetBatch_;
    render::Model track_;
    RaceTown town_;
    render::Model gagBox_, banana_, anvil_, pie_;
    struct PieActor {
        racing::Pie physics;
        uint32_t target = 0;
    } pies_[16];
    uint32_t shotSerial_ = 0;
    CollisionWorld collision_;
    racing::Track metadata_;
    racing::Vehicle physics_;
    racing::Vector boardingOrigin_, exitPosition_;
    bool exitPosePending_ = false;
    float boardingSide_ = -3;
    racing::Progress progress_;
    uint32_t race_ = 0, vehicle_ = 0, destination_ = 0, seated_ = 0;
    uint32_t lastTickets_ = 0, lastDestination_ = 0;
    // The pad of the last block we sat on: its setRaceZone can arrive in the same turn as the
    // AI clearing our seat (the race launch), and still sends us to the race.
    uint32_t seatPad_ = 0, seatLeftMs_ = 0;
    uint32_t now_ = 0, lastPose_ = 0, lastProgress_ = 0, phaseMs_ = 0;
    uint32_t hitStart_ = 0, hitUntil_ = 0;
    uint32_t consumedHazards_[32] = {};
    uint32_t movieRevision_ = 0, hitRevision_ = 0, refusalUntil_ = 0, heldSince_ = 0,
             turboStart_ = 0;
    uint16_t acknowledged_[8] = {};
    int acknowledgedCount_ = 0;
    uint8_t phase_ = 255, place_ = 1, heldGag_ = 0;
    uint8_t inBoost_ = 0; // boost arrow tubes the kart is inside (enter events)
    uint16_t loadedTrack_ = 0xffff;
    bool spawned_ = false, requestedKart_ = false, finished_ = false, left_ = false;
    uint8_t movie_ = 0;
    bool movieDone_ = false;
    float movieAge_ = 0, gravityVelocity_ = 0;
    char hudLines_[3][192] = {};
};
} // namespace tt::world
