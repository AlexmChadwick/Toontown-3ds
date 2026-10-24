#include "world/boat.h"

#include <cmath>

#include "core/log.h"
#include "world/boat_paths.h"

namespace tt::world {

namespace {

constexpr const char* kModelPath = "sdmc:/3ds/toontown3ds/data/zones/1000_boat.ttm";
constexpr const char* kDeckPath = "sdmc:/3ds/toontown3ds/data/zones/1000_boat.ttc";
constexpr const char* kSfxPaths[] = {
    "sdmc:/3ds/toontown3ds/data/audio/boat_bell.tsnd",
    "sdmc:/3ds/toontown3ds/data/audio/boat_foghorn.tsnd",
    "sdmc:/3ds/toontown3ds/data/audio/boat_creak.tsnd",
    "sdmc:/3ds/toontown3ds/data/audio/boat_lap.tsnd",
};
constexpr float kDegToRad = 3.14159265f / 180.0f;
// Source clip length (seconds), to end-align the foghorn (ParallelEndTogether), and
// DistributedBoat PIER_TIME, for the creak of the rising pier (also end-aligned).
constexpr float kFoghornSecs = 2.194f;
constexpr float kPierTime = 5.0f;
// Positional sound: full volume inside kNear, 1/d beyond, silent past kFar (the boat is a
// big source; Panda's 3D audio drops off with distance too).
constexpr float kNear = 30.0f;
constexpr float kFar = 250.0f;
// A sound whose start time was crossed longer ago than this (joined mid-sail) is skipped.
constexpr float kLateSecs = 0.75f;

// Samples a baked Mopath at t seconds (clamped to its duration): linear between samples.
void samplePath(const float (*pts)[4], int count, float duration, float t, float* out) {
    if (t < 0) t = 0;
    if (t > duration) t = duration;
    float f = t / kBoatPathStep;
    int i = static_cast<int>(f);
    if (i > count - 2) i = count - 2;
    f -= static_cast<float>(i);
    if (f > 1.0f) f = 1.0f;
    for (int k = 0; k < 4; k++) out[k] = pts[i][k] + (pts[i + 1][k] - pts[i][k]) * f;
}

} // namespace

void BoatActor::handleEvent(const net::Event& ev, const net::Client& client) {
    using net::EventType;
    if (ev.type == EventType::BoatGone) {
        if (active_ && doId_ == ev.doId) shutdown();
        return;
    }
    if (ev.type != EventType::BoatSpawn && ev.type != EventType::BoatState) return;
    const net::Boat* b = client.boat();
    if (!b || b->doId != ev.doId) return;
    if (!active_) {
        wantModel_ = true; // requested on the loader thread from update()
        if (!deck_.load(kDeckPath)) TT_LOG("boat: no deck collision %s; the toon cannot ride", kDeckPath);
        sfxLoaded_ = 0;
        fired_ = 0;
        active_ = true;
        Mtx_Identity(&matrix_);
        doId_ = b->doId;
    }
    // Upstream finish()es the previous track on a state change, which leaves the boat at the
    // end pose that the next state starts from, so only the state and its clock change.
    if (state_ != b->state || stateStartMs_ != b->stateStartMs) fired_ = 0;
    state_ = b->state;
    stateStartMs_ = b->stateStartMs;
}

// DistributedBoat state machine (docs/design/boat.md):
//   DockedEast  = end of dd-w-e    SailingWest = dd-e-w from t
//   DockedWest  = end of dd-e-w    SailingEast = dd-w-e from t
void BoatActor::pose(u32 nowMs) {
    const s32 ms = static_cast<s32>(nowMs - stateStartMs_);
    const float t = ms > 0 ? ms / 1000.0f : 0.0f;
    float p[4];
    switch (state_) {
    case net::BoatState::SailingWest:
        samplePath(kBoatEW, kBoatEWCount, kBoatEWDuration, t, p);
        break;
    case net::BoatState::DockedWest:
        samplePath(kBoatEW, kBoatEWCount, kBoatEWDuration, kBoatEWDuration, p);
        break;
    case net::BoatState::SailingEast:
        samplePath(kBoatWE, kBoatWECount, kBoatWEDuration, t, p);
        break;
    default: // DockedEast
        samplePath(kBoatWE, kBoatWECount, kBoatWEDuration, kBoatWEDuration, p);
        break;
    }
    x_ = p[0], y_ = p[1], z_ = p[2], h_ = p[3];
}

void BoatActor::update(u32 nowMs) {
    // The model loads on the background loader; swap it in when it is ready.
    if (wantModel_ && modelTicket_ == kNoTicket) modelTicket_ = AssetLoader::requestModel(kModelPath);
    if (modelTicket_ != kNoTicket) {
        const LoadStatus st = AssetLoader::poll(modelTicket_);
        if (st == LoadStatus::Ready) {
            AssetLoader::takeModel(modelTicket_, model_);
            modelTicket_ = kNoTicket;
            wantModel_ = false;
            TT_LOG("boat: model ready (%u tris)", unsigned(model_.numTriangles()));
        } else if (st != LoadStatus::Pending) {
            TT_LOG("boat: no model %s (bake tools/pipeline/bake_boat.py)", kModelPath);
            AssetLoader::cancel(modelTicket_);
            modelTicket_ = kNoTicket;
            wantModel_ = false;
        }
    }
    if (!active()) return;
    pose(nowMs);
    Mtx_Identity(&matrix_);
    Mtx_Translate(&matrix_, x_, y_, z_, true);
    Mtx_RotateZ(&matrix_, h_ * kDegToRad, true);
    const s32 ms = static_cast<s32>(nowMs - stateStartMs_);
    sailT_ = ms > 0 ? ms / 1000.0f : 0.0f;
    updateSounds();
}

// Positional one-shot: gain falls off with distance from the camera, pan follows its right vector.
void BoatActor::playAt(int sfx, float x, float y) {
    if (sfx_[sfx] == audio::kInvalidSfx) return;
    const float dx = x - listener_[0], dy = y - listener_[1];
    const float d = sqrtf(dx * dx + dy * dy);
    if (d >= kFar) return;
    const float gain = d <= kNear ? 1.0f : kNear / d;
    float fx = listener_[2] - listener_[0], fy = listener_[3] - listener_[1];
    const float fl = sqrtf(fx * fx + fy * fy);
    float pan = 0.0f;
    if (fl > 0.01f && d > 1.0f) {
        fx /= fl, fy /= fl;
        pan = (dx * fy - dy * fx) / d * 0.8f; // right = (fy, -fx)
    }
    audio::playSfx(sfx_[sfx], gain, pan);
}

// DistributedBoat tracks: bell at the start of a sail, foghorn end-aligned, the dock creak of
// the pier tilting away at t = 0 (departure side) and of the other pier rising at the end.
void BoatActor::updateSounds() {
    if (sfxLoaded_ < kSfxCount && audio::available()) { // one file per frame: no hitch
        sfx_[sfxLoaded_] = audio::loadSfx(kSfxPaths[sfxLoaded_]);
        if (sfx_[sfxLoaded_] == audio::kInvalidSfx) TT_LOG("boat: no sound %s", kSfxPaths[sfxLoaded_]);
        sfxLoaded_++;
    }
    setRider(rider_); // restarts the loop if its voice was stolen
    const bool west = state_ == net::BoatState::SailingWest;
    if (!west && state_ != net::BoatState::SailingEast) return;
    const float dur = west ? kBoatEWDuration : kBoatWEDuration;
    // Pier docks = ends of the paths: east = start of dd-e-w, west = its end.
    const float* eastDock = kBoatEW[0];
    const float* westDock = kBoatEW[kBoatEWCount - 1];
    const float* downDock = west ? eastDock : westDock;
    const float* upDock = west ? westDock : eastDock;
    struct Ev {
        float t;
        int sfx;
        float x, y;
    };
    const Ev events[4] = {{0.0f, kBell, x_, y_},
                          {0.0f, kCreak, downDock[0], downDock[1]},
                          {dur - kPierTime, kCreak, upDock[0], upDock[1]},
                          {dur - kFoghornSecs, kFoghorn, x_, y_}};
    for (int i = 0; i < 4; i++) {
        if ((fired_ & (1 << i)) || sailT_ < events[i].t) continue;
        fired_ |= static_cast<u8>(1 << i);
        if (sailT_ - events[i].t < kLateSecs) playAt(events[i].sfx, events[i].x, events[i].y);
    }
}

void BoatActor::setRider(bool onDeck) {
    rider_ = onDeck;
    if (onDeck && active_ && sfx_[kLap] != audio::kInvalidSfx) {
        if (!audio::voicePlaying(lapVoice_)) lapVoice_ = audio::playSfx(sfx_[kLap], 1.0f, 0.0f, true);
    } else if (lapVoice_ != audio::kNoVoice) {
        audio::stopVoice(lapVoice_);
        lapVoice_ = audio::kNoVoice;
    }
}

void BoatActor::stopSounds() {
    if (lapVoice_ != audio::kNoVoice) audio::stopVoice(lapVoice_);
    lapVoice_ = audio::kNoVoice;
    for (audio::SfxId& id : sfx_) {
        if (id != audio::kInvalidSfx) audio::freeSfx(id);
        id = audio::kInvalidSfx;
    }
    sfxLoaded_ = 0;
}

void BoatActor::draw(render::Renderer& renderer) {
    if (!active() || !model_.loaded()) return;
    renderer.drawModel(model_, matrix_);
}

void BoatActor::shutdown() {
    if (modelTicket_ != kNoTicket) AssetLoader::cancel(modelTicket_);
    modelTicket_ = kNoTicket;
    wantModel_ = false;
    stopSounds();
    rider_ = false;
    if (model_.loaded()) {
        C3D_FrameSync();
        model_.unload();
    }
    deck_.unload();
    active_ = false;
    state_ = net::BoatState::Off;
}

} // namespace tt::world
