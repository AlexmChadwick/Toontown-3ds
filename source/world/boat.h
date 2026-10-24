// Donald's Dock boat (DistributedBoat, zone 1000): the moving ship that Donald (class
// DistributedDonaldDock) stands on. tt::net::Client parses setState(state, ts); this class
// replays the upstream track (DistributedBoat.setupTracks) from the synced timestamp.
//
// Files (tools/pipeline/bake_boat.py; docs/design/boat.md):
//   sdmc:/3ds/toontown3ds/data/zones/1000_boat.ttm   boat geometry, boat-local frame
//   source/world/boat_paths.h                        Mopath dd-e-w / dd-w-e samples (generated)
//   sdmc:/3ds/toontown3ds/data/zones/1000_boat.ttc   deck collision (boat-local frame): the local toon
//                                                    rides it (Player::setPlatform), see deck()
//   sdmc:/3ds/toontown3ds/data/audio/boat_{bell,foghorn,creak,lap}.tsnd   (tools/pipeline/bake_audio.py --set boat)
// The model loads on the shared background loader (world/asset_loader.h); nothing is drawn until
// it is ready. Collision (4 KB) loads synchronously, sounds one per frame after spawn.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "audio/audio.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "world/asset_loader.h"
#include "world/collision.h"

namespace tt::world {

class BoatActor {
public:
    // Donald's attachment point on the boat (DistributedDonaldDock.generate: setPos(0, -1, 3.95)).
    static constexpr float kDonaldOffset[3] = {0.0f, -1.0f, 3.95f};

    void handleEvent(const net::Event& ev, const net::Client& client);
    void update(u32 nowMs);
    // Listener (camera) for the boat's positional sounds; call before update().
    void setListener(float eyeX, float eyeY, float targetX, float targetY) {
        listener_[0] = eyeX, listener_[1] = eyeY, listener_[2] = targetX, listener_[3] = targetY;
    }
    // The local toon is on deck (DDPlayground.enterOnBoat/exitOnBoat): loops the water-lap sound.
    void setRider(bool onDeck);
    void draw(render::Renderer& renderer);
    void shutdown();

    // True while a DistributedBoat exists and has a pose (not "off").
    bool active() const { return active_ && state_ != net::BoatState::Off; }
    // Boat-local -> zone transform (translation + heading), valid when active() after update().
    const C3D_Mtx& matrix() const { return matrix_; }
    // Deck collision in the boat-local frame while the boat is active and the file loaded, else null.
    const CollisionWorld* deck() const { return active() && deck_.loaded() ? &deck_ : nullptr; }
    float x() const { return x_; }
    float y() const { return y_; }
    float z() const { return z_; }
    float heading() const { return h_; }

private:
    void pose(u32 nowMs);
    void updateSounds();
    void playAt(int sfx, float x, float y);
    void stopSounds();

    enum Sfx { kBell, kFoghorn, kCreak, kLap, kSfxCount };

    render::Model model_;
    LoadTicket modelTicket_ = kNoTicket;
    bool wantModel_ = false;
    CollisionWorld deck_;
    audio::SfxId sfx_[kSfxCount] = {audio::kInvalidSfx, audio::kInvalidSfx, audio::kInvalidSfx, audio::kInvalidSfx};
    int sfxLoaded_ = 0; // next sound to load (one per frame)
    audio::VoiceHandle lapVoice_ = audio::kNoVoice;
    bool rider_ = false;
    u8 fired_ = 0; // sail sound events already played in this state
    float listener_[4] = {};
    bool active_ = false;
    u32 doId_ = 0;
    net::BoatState state_ = net::BoatState::Off;
    u32 stateStartMs_ = 0;
    float x_ = 0, y_ = 0, z_ = 0, h_ = 0;
    float sailT_ = 0; // seconds into the current Sailing* state
    C3D_Mtx matrix_;
};

} // namespace tt::world
