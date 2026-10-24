#include "world/fishing.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "net/net_platform.h"

namespace tt::world {

namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr const char* kDataDir = "sdmc:/3ds/toontown3ds/data/fishing/";

// DistributedFishingSpot.getSphereRadius() = 1.5, plus the toon's own radius.
constexpr float kEnterRadius = 2.0f;
constexpr float kRearmRadius = 3.2f;
constexpr float kEnterZRange = 4.0f;
constexpr float kRequestTimeoutSec = 3.0f;
constexpr float kEnterSec = 1.2f;      // sit-down movie (upstream: run to the spot + 'pole' clip)
constexpr float kCamLerpSec = 1.5f;    // enterWaiting: camera LerpPosHprInterval duration
constexpr float kLeaveSec = 0.5f;      // fish-end clip + pole shrinking (LerpScale 0.5 s)
constexpr float kChargeSec = 1.2f;     // hold A from 0 to full power
constexpr float kMinPower = 0.08f;     // below this a release is not a cast
constexpr float kAimRate = 60.0f;      // deg/s with the circle pad
constexpr float kReelSec = 1.0f;
constexpr float kBobSink = 0.08f;      // __doBobBob amplitude
constexpr float kPondMargin = 3.0f;    // a bob this far outside the fish region still counts
constexpr float kMissSec = 1.2f;
// DistributedFishingSpot.enterWaiting: camera at (0, -12, 15) pitched -38 degrees relative to the toon.
constexpr float kCamBack = 12.0f, kCamUp = 15.0f, kCamPitchDeg = 38.0f, kCamLook = 30.0f;
// Rod in the right hand (toon space: +X is the toon's right) and its joint_attachBill (line start).
constexpr float kHand[3] = {0.55f, 0.45f, 2.0f};
constexpr float kRodTip[3] = {-0.1331f, 6.3506f, 0.472f};
constexpr float kPadDeadzone = 24.0f, kPadMax = 150.0f;

float padAxis(s16 v) {
    float f = static_cast<float>(v);
    if (fabsf(f) < kPadDeadzone) return 0.0f;
    f = (f > 0 ? f - kPadDeadzone : f + kPadDeadzone) / (kPadMax - kPadDeadzone);
    return f > 1.0f ? 1.0f : (f < -1.0f ? -1.0f : f);
}

float smooth(float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t); // easeInOut
}

// Panda heading rotation of the horizontal vector (x, y).
void rotH(float h, float x, float y, float* ox, float* oy) {
    const float c = cosf(h * kDegToRad), s = sinf(h * kDegToRad);
    *ox = x * c - y * s;
    *oy = x * s + y * c;
}

void applyMtx(const C3D_Mtx& m, const float* p, float* out) {
    out[0] = m.r[0].x * p[0] + m.r[0].y * p[1] + m.r[0].z * p[2] + m.r[0].w;
    out[1] = m.r[1].x * p[0] + m.r[1].y * p[1] + m.r[1].z * p[2] + m.r[1].w;
    out[2] = m.r[2].x * p[0] + m.r[2].y * p[1] + m.r[2].z * p[2] + m.r[2].w;
}

} // namespace

// ---------------------------------------------------------------- lifecycle
void Fishing::init(Player* player, net::Client* net) {
    player_ = player;
    net_ = net;
    rng_.seed(net::plat::nowMs() | 1u);
}

void Fishing::shutdown() {
    unloadAssets();
    for (audio::SfxId& s : splash_) {
        if (s != audio::kInvalidSfx) audio::freeSfx(s);
        s = audio::kInvalidSfx;
    }
}

void Fishing::loadAssets(int rod) {
    char path[96];
    if (!linesLoaded_) {
        snprintf(path, sizeof(path), "%sbob.ttm", kDataDir);
        bob_.load(path);
        snprintf(path, sizeof(path), "%sline.ttm", kDataDir);
        line_.load(path);
        linesLoaded_ = true;
    }
    if (loadedRod_ != rod) {
        if (rod_.loaded()) {
            C3D_FrameSync();
            rod_.unload();
        }
        snprintf(path, sizeof(path), "%srod_%d.ttm", kDataDir, rod);
        if (!rod_.load(path)) TT_LOG("fishing: no rod model %s (tools/pipeline/bake_fishing.py)", path);
        loadedRod_ = rod;
    }
    if (!sfxTried_) {
        sfxTried_ = true;
        for (int i = 0; i < 2; i++) {
            snprintf(path, sizeof(path), "%ssplash%d.tsnd", kDataDir, i + 1);
            splash_[i] = audio::loadSfx(path);
        }
    }
}

void Fishing::unloadAssets() {
    if (rod_.loaded() || bob_.loaded() || line_.loaded()) C3D_FrameSync();
    rod_.unload();
    bob_.unload();
    line_.unload();
    loadedRod_ = -1;
    linesLoaded_ = false;
}

void Fishing::setZone(u32 zoneId, const char* pointsPath) {
    zoneId_ = zoneId;
    numAnchors_ = 0;
    numSpots_ = 0;
    if (active() || state_ == State::Requesting) {
        state_ = State::Idle;
        bobVisible_ = false;
    }
    if (FILE* f = pointsPath && pointsPath[0] ? fopen(pointsPath, "r") : nullptr) {
        char line[128], name[40];
        while (numAnchors_ < kMaxSpots && fgets(line, sizeof(line), f)) {
            Spot s;
            if (sscanf(line, "%39s %f %f %f %f", name, &s.x, &s.y, &s.z, &s.h) == 5 &&
                !strncmp(name, "fishing_spot", 12)) {
                anchors_[numAnchors_++] = s;
            }
        }
        fclose(f);
    }
    TT_LOG("fishing: zone %lu has %d local spot anchors", static_cast<unsigned long>(zoneId), numAnchors_);
}

// ---------------------------------------------------------------- helpers
bool Fishing::serverMode() const { return numSpots_ > 0; }

int Fishing::numSpots() const { return serverMode() ? numSpots_ : numAnchors_; }

void Fishing::collectSpots() {
    // Server spots, keeping the armed flag of ones we already knew.
    Spot fresh[kMaxSpots];
    int n = 0;
    if (net_ && net_->inZone()) {
        for (int i = 0; i < net::kMaxFishSpots && n < kMaxSpots; i++) {
            const net::FishingSpot& s = net_->fishSpotSlot(i);
            if (!s.active) continue;
            Spot& d = fresh[n++];
            d.doId = s.doId;
            d.x = s.x;
            d.y = s.y;
            d.z = s.z;
            d.h = s.h;
            d.armed = true;
            for (int k = 0; k < numSpots_; k++) {
                if (spots_[k].doId == s.doId) d.armed = spots_[k].armed;
            }
        }
    }
    for (int i = 0; i < n; i++) spots_[i] = fresh[i];
    numSpots_ = n;
}

int Fishing::currentRod() const {
    if (net_ && net_->inZone()) {
        const int r = net_->ownFish().rod;
        return r < fish::kNumRods ? r : fish::kNumRods - 1;
    }
    return 0;
}

int Fishing::currentArea() const {
    if (net_ && net_->fishingPond()) return static_cast<int>(net_->fishingPond()->area);
    const u32 z = zoneId_;
    return static_cast<int>(z % 1000 >= 100 ? z - z % 100 : z); // ZoneUtil.getCanonicalZoneId
}

bool Fishing::nearestSpot(float x, float y, float* sx, float* sy, float* sz, float* sh) const {
    const Spot* list = serverMode() ? spots_ : anchors_;
    const int n = serverMode() ? numSpots_ : numAnchors_;
    float best = 1e30f;
    const Spot* hit = nullptr;
    for (int i = 0; i < n; i++) {
        const float d = (list[i].x - x) * (list[i].x - x) + (list[i].y - y) * (list[i].y - y);
        if (d < best) {
            best = d;
            hit = &list[i];
        }
    }
    if (!hit) return false;
    *sx = hit->x;
    *sy = hit->y;
    *sz = hit->z;
    *sh = hit->h;
    return true;
}

void Fishing::setState(State s) {
    state_ = s;
    stateTime_ = 0.0f;
}

void Fishing::setStatus(const char* text) {
    if (strcmp(view_.status, text) == 0) return;
    snprintf(view_.status, sizeof(view_.status), "%s", text);
    view_.revision++;
}

void Fishing::setResultNone() {
    view_.result = ui::FishingView::kNone;
    view_.itemName[0] = view_.itemWeight[0] = view_.itemValue[0] = 0;
    view_.newEntry = false;
    view_.revision++;
}

// ---------------------------------------------------------------- session
void Fishing::enter(const Spot& s, u32 nowMs) {
    cur_ = s;
    curLocal_ = s.doId == 0;
    if (curLocal_) {
        setState(State::Entering);
    } else {
        // DistributedFishingSpot.__handleEnterSphere -> d_requestEnter; the AI answers with
        // setOccupied(avId) + setMovie(EnterMovie), or rejectEnter.
        if (!net_ || !net_->sendFishRequestEnter(s.doId)) return;
        requestMs_ = nowMs;
        setState(State::Requesting);
        return;
    }
    // Local mode starts the session at once.
    sessionSpot_ = 0;
    camStartEye_ = player_->cameraEye();
    camStartTarget_ = player_->cameraTarget();
    camStarted_ = true;
    camT_ = 0.0f;
    aim_ = 0.0f;
    power_ = 0.0f;
    gotResult_ = false;
    bobVisible_ = false;
    rodPitch_ = 35.0f;
    player_->reset(cur_.x, cur_.y, cur_.z, cur_.h);
    loadAssets(currentRod());
    view_ = ui::FishingView{};
    view_.practice = true;
    view_.aimMax = fish::kFishingAngleMax;
    view_.phase = ui::FishingView::kEntering;
    setStatus("Getting ready to fish...");
    TT_LOG("fishing: local spot at %.1f,%.1f,%.1f h %.0f (area %d)", double(cur_.x), double(cur_.y), double(cur_.z),
           double(cur_.h), currentArea());
}

// Server confirmed our requestEnter (setOccupied(us) or EnterMovie).
void Fishing::handleEvent(const net::Event& ev) {
    using net::EventType;
    if (!net_) return;
    switch (ev.type) {
    case EventType::FishReject:
        if (state_ == State::Requesting && ev.doId == cur_.doId) {
            TT_LOG("fishing: spot %u rejected us", unsigned(ev.doId));
            state_ = State::Idle;
        }
        break;
    case EventType::FishOccupied: {
        const net::FishingSpot* s = net_->findFishSpot(ev.doId);
        if (!s || ev.doId != cur_.doId) break;
        if (s->occupant == net_->avatarId() && state_ == State::Requesting) {
            // Same start as local mode, but the spot is the server's.
            const Spot spot = cur_;
            sessionSpot_ = spot.doId;
            camStartEye_ = player_->cameraEye();
            camStartTarget_ = player_->cameraTarget();
            camStarted_ = true;
            camT_ = 0.0f;
            aim_ = 0.0f;
            power_ = 0.0f;
            gotResult_ = false;
            bobVisible_ = false;
            rodPitch_ = 35.0f;
            player_->reset(cur_.x, cur_.y, cur_.z, cur_.h);
            loadAssets(currentRod());
            view_ = ui::FishingView{};
            view_.practice = false;
            view_.aimMax = fish::kFishingAngleMax;
            view_.phase = ui::FishingView::kEntering;
            setState(State::Entering);
            setStatus("Getting ready to fish...");
            TT_LOG("fishing: sat down at server spot %u", unsigned(spot.doId));
        } else if (s->occupant != net_->avatarId() && active() && sessionSpot_ == ev.doId) {
            beginLeave();
        }
        break;
    }
    case EventType::FishMovie: {
        const net::FishMovieMsg& m = net_->lastFishMovie();
        if (m.spotDoId != sessionSpot_ || !active()) break;
        if (m.mode == net::kFishPullInMovie) onPullIn(m);
        else if (m.mode == net::kFishExitMovie) beginLeave();
        break;
    }
    case EventType::FishSpotGone:
        if (active() && sessionSpot_ == ev.doId) beginLeave();
        else if (state_ == State::Requesting && cur_.doId == ev.doId) state_ = State::Idle;
        break;
    default:
        break;
    }
}

void Fishing::beginLeave() {
    if (state_ == State::Leaving || !active()) return;
    // d_requestExit: the AI frees the spot and sends setMovie(ExitMovie).
    if (!curLocal_ && net_ && net_->inZone() && sessionSpot_) net_->sendFishRequestExit(sessionSpot_);
    bobVisible_ = false;
    setState(State::Leaving);
    view_.phase = ui::FishingView::kLeaving;
    setStatus("Packing up...");
    TT_LOG("fishing: leaving the spot");
}

void Fishing::startCharge() {
    power_ = 0.0f;
    bobVisible_ = false;
    gotResult_ = false;
    setResultNone();
    setState(State::Charging);
    setStatus("Release A to cast!");
}

void Fishing::releaseCast(u32 nowMs) {
    (void)nowMs;
    if (power_ < kMinPower) {
        setState(State::Ready);
        power_ = 0.0f;
        setStatus("Hold A longer, then let go to cast.");
        return;
    }
    const int rod = currentRod();
    if (!curLocal_ && net_ && net_->inZone()) {
        // FishGlobals.getCastCost: jellybeans per cast, charged by the AI.
        if (net_->ownStats().money < fish::kRods[rod].castCost) {
            setState(State::Ready);
            power_ = 0.0f;
            view_.result = ui::FishingView::kBroke;
            snprintf(view_.itemName, sizeof(view_.itemName), "Out of jellybeans");
            snprintf(view_.itemValue, sizeof(view_.itemValue), "You need %d to cast.", fish::kRods[rod].castCost);
            view_.revision++;
            setStatus("You have no jellybeans for bait!");
            return;
        }
    }
    const int maxTank = curLocal_ || !net_ || !net_->inZone() ? 20 : net_->ownFish().maxTank;
    const int tank = curLocal_ || !net_ || !net_->inZone() ? localTank_ : net_->ownFish().tankCount;
    if (tank >= maxTank) {
        setState(State::Ready);
        power_ = 0.0f;
        view_.result = ui::FishingView::kTankFull;
        snprintf(view_.itemName, sizeof(view_.itemName), "Your fish bucket is full");
        snprintf(view_.itemValue, sizeof(view_.itemValue), curLocal_ ? "Leave (B) to empty it." : "Sell your fish first.");
        view_.revision++;
        setStatus("Bucket full!");
        return;
    }
    castPower_ = power_;
    castAim_ = aim_;
    castTime_ = 0.0f;
    landing_ = fish::castLanding(castPower_, fish::waterLevel(currentArea()));
    bobVisible_ = true;
    casts_++;
    if (!curLocal_ && net_) net_->sendFishCast(sessionSpot_, castPower_, castAim_); // d_doCast(power, heading)
    TT_LOG("fishing: cast power %.2f aim %.0f -> %.1f ahead in %.2fs", double(castPower_), double(castAim_),
           double(landing_.y), double(landing_.t));
    setResultNone();
    setState(State::Casting);
    setStatus("Casting...");
}

void Fishing::landBob(u32 nowMs) {
    (void)nowMs;
    setState(State::Waiting);
    waitTime_ = 0.0f;
    bobBob_ = 0.0f;
    gotResult_ = false;
    const int sfx = rng_.below(2);
    if (splash_[sfx] != audio::kInvalidSfx) audio::playSfx(splash_[sfx], 0.8f);
    if (!curLocal_) {
        biteIn_ = -1.0f;
        setStatus("Waiting for a bite...");
        return;
    }
    // Local mode: a bite only if the bob landed over the fish region of the pond
    // (FishingTargetGlobals centre/radius; upstream needs a hit on a swimming target).
    float hx, hy;
    rotH(cur_.h + castAim_, 0.0f, landing_.y, &hx, &hy);
    const float wx = cur_.x + hx, wy = cur_.y + hy;
    float cx, cy, rad;
    bool inPond = true;
    if (fish::pondRegion(currentArea(), &cx, &cy, &rad)) {
        const float d = sqrtf((wx - cx) * (wx - cx) + (wy - cy) * (wy - cy));
        inPond = d <= rad + kPondMargin;
    }
    if (inPond) {
        biteIn_ = 2.0f + rng_.unit() * 4.0f;
        setStatus("Waiting for a bite...");
    } else {
        biteIn_ = -1.0f;
        setStatus("Too far from the fish! Cast again.");
    }
    TT_LOG("fishing: bob at %.1f,%.1f %s", double(wx), double(wy), inPond ? "in the pond" : "off target");
}

void Fishing::onPullIn(const net::FishMovieMsg& m) {
    fish::Catch c;
    pendingNewEntry_ = false;
    switch (m.code) {
    case net::kFishItem:
    case net::kFishItemNewEntry:
    case net::kFishItemNewRecord:
        c.kind = fish::CatchKind::Fish;
        c.genus = static_cast<uint8_t>(m.item1);
        c.species = static_cast<uint8_t>(m.item2);
        c.weight = m.item3;
        c.value = static_cast<uint16_t>(fish::fishValue(c.genus, c.species, c.weight));
        pendingNewEntry_ = m.code != net::kFishItem;
        break;
    case net::kFishJellybean:
        c.kind = fish::CatchKind::Jellybeans;
        c.beans = m.item1;
        break;
    case net::kFishBoot: c.kind = fish::CatchKind::Boot; break;
    default: break; // Nothing, OverTankLimit, quest items: nothing to show
    }
    pending_ = c;
    gotResult_ = true;
    if (state_ == State::Waiting || state_ == State::Casting) {
        reelTime_ = 0.0f;
        setState(State::Reeling);
        setStatus("A bite!");
    }
}

void Fishing::finishCatch(const fish::Catch& c, bool newEntry) {
    view_.newEntry = newEntry;
    view_.itemName[0] = view_.itemWeight[0] = view_.itemValue[0] = 0;
    switch (c.kind) {
    case fish::CatchKind::Fish: {
        const fish::Species* s = fish::findSpecies(c.genus, c.species);
        view_.result = ui::FishingView::kFish;
        snprintf(view_.itemName, sizeof(view_.itemName), "%s", s ? s->name : "A fish");
        char w[32];
        fish::weightText(c.weight, w, sizeof(w));
        snprintf(view_.itemWeight, sizeof(view_.itemWeight), "Weight: %s", w);
        snprintf(view_.itemValue, sizeof(view_.itemValue), "Value: %d jellybean%s", c.value, c.value == 1 ? "" : "s");
        setStatus("You caught a fish!");
        if (curLocal_) localTank_++;
        TT_LOG("fishing: caught %s %d oz value %d%s", s ? s->name : "?", c.weight, c.value, curLocal_ ? " (practice)" : "");
        break;
    }
    case fish::CatchKind::Jellybeans:
        view_.result = ui::FishingView::kBeans;
        snprintf(view_.itemName, sizeof(view_.itemName), "%d jellybeans", c.beans);
        snprintf(view_.itemValue, sizeof(view_.itemValue), curLocal_ ? "Practice: not added to your jar." : "Added to your jar!");
        setStatus("You found jellybeans!");
        TT_LOG("fishing: caught %d jellybeans", c.beans);
        break;
    case fish::CatchKind::Boot:
        view_.result = ui::FishingView::kBoot;
        snprintf(view_.itemName, sizeof(view_.itemName), "An old boot");
        snprintf(view_.itemValue, sizeof(view_.itemValue), "Better luck next time!");
        setStatus("Oops, an old boot.");
        TT_LOG("fishing: caught a boot");
        break;
    default:
        view_.result = ui::FishingView::kNothing;
        snprintf(view_.itemName, sizeof(view_.itemName), "You didn't catch anything!");
        snprintf(view_.itemValue, sizeof(view_.itemValue), "Try again.");
        setStatus("It got away.");
        TT_LOG("fishing: nothing caught");
        break;
    }
    view_.revision++;
}

// ---------------------------------------------------------------- per frame
void Fishing::handleInput(Input& in, float dt, u32 nowMs, bool canEnter) {
    if (!player_) return;
    collectSpots();

    if (state_ == State::Idle || state_ == State::Requesting) {
        Spot* list = serverMode() ? spots_ : anchors_;
        const int n = serverMode() ? numSpots_ : numAnchors_;
        if (state_ == State::Requesting && nowMs - requestMs_ > static_cast<u32>(kRequestTimeoutSec * 1000.0f)) {
            TT_LOG("fishing: no answer to requestEnter");
            state_ = State::Idle;
        }
        for (int i = 0; i < n; i++) {
            const float dx = player_->x() - list[i].x, dy = player_->y() - list[i].y;
            const float d = sqrtf(dx * dx + dy * dy);
            if (d > kRearmRadius) list[i].armed = true;
            if (state_ != State::Idle || !canEnter || !list[i].armed) continue;
            if (d <= kEnterRadius && fabsf(player_->z() - list[i].z) < kEnterZRange && !player_->airborne()) {
                list[i].armed = false;
                enter(list[i], nowMs);
            }
        }
        return;
    }

    // Fishing: the toon stays on the spot; the pad aims and A / B drive the state machine.
    const float aimAxis = padAxis(in.pad.dx);
    const bool aDown = (in.down & KEY_A) != 0, aHeld = (in.held & KEY_A) != 0, bDown = (in.down & KEY_B) != 0;
    in.pad = circlePosition{};
    in.cstick = circlePosition{};
    in.held = 0;
    in.down = 0;
    stateTime_ += dt;

    const bool canAim = state_ == State::Ready || state_ == State::Charging || state_ == State::Waiting ||
                        state_ == State::Result;
    if (canAim) {
        aim_ -= aimAxis * kAimRate * dt; // pad right = clockwise = negative Panda heading
        const float lim = fish::kFishingAngleMax;
        aim_ = aim_ < -lim ? -lim : (aim_ > lim ? lim : aim_);
    }

    switch (state_) {
    case State::Entering:
        camT_ = stateTime_ / kCamLerpSec;
        if (stateTime_ >= kEnterSec) {
            setState(State::Ready);
            view_.phase = ui::FishingView::kReady;
            setStatus("Hold A, then let go to cast!");
        }
        break;
    case State::Ready:
    case State::Result:
        camT_ = (stateTime_ + (state_ == State::Ready ? kEnterSec : 0.0f)) / kCamLerpSec;
        if (aDown) startCharge();
        else if (bDown) beginLeave();
        break;
    case State::Charging:
        if (bDown) {
            power_ = 0.0f;
            setState(State::Ready);
            setStatus("Hold A, then let go to cast!");
        } else if (aHeld) {
            power_ += dt / kChargeSec;
            if (power_ > 1.0f) power_ = 1.0f;
        } else {
            releaseCast(nowMs);
        }
        break;
    case State::Casting:
        castTime_ += dt;
        if (castTime_ >= landing_.t) landBob(nowMs);
        break;
    case State::Waiting:
        waitTime_ += dt;
        bobBob_ += dt;
        if (aDown) {
            startCharge();
        } else if (bDown) {
            beginLeave();
        } else if (curLocal_) {
            if (biteIn_ >= 0.0f && waitTime_ >= biteIn_) {
                pending_ = fish::rollCatch(currentArea(), currentRod(), rng_);
                pendingNewEntry_ = false;
                gotResult_ = true;
                reelTime_ = 0.0f;
                setState(State::Reeling);
                setStatus("A bite!");
            } else if (biteIn_ < 0.0f && waitTime_ >= kMissSec) {
                bobVisible_ = false;
                setState(State::Ready);
            }
        } else if (waitTime_ >= fish::kCastTimeout) {
            // The AI answers every cast; a missing PullIn means we lost it.
            bobVisible_ = false;
            setState(State::Ready);
            setStatus("Nothing bit. Cast again!");
        }
        break;
    case State::Reeling:
        reelTime_ += dt;
        if (reelTime_ >= kReelSec) {
            bobVisible_ = false;
            finishCatch(pending_, pendingNewEntry_);
            setState(State::Result);
        }
        break;
    case State::Leaving:
        camT_ = 1.0f - stateTime_ / kLeaveSec;
        if (stateTime_ >= kLeaveSec) {
            // Step back from the water (the sphere would re-trigger otherwise).
            float bx, by;
            rotH(cur_.h, 0.0f, -(kRearmRadius + 0.5f), &bx, &by);
            player_->reset(cur_.x + bx, cur_.y + by, cur_.z, cur_.h);
            if (curLocal_) localTank_ = 0; // practice bucket empties when we leave
            state_ = State::Idle;
            camStarted_ = false;
            TT_LOG("fishing: left the spot after %d casts", casts_);
            casts_ = 0;
            return;
        }
        break;
    default:
        break;
    }

    player_->setHeading(cur_.h + aim_);
    updateBob(dt);
    updateRodPitch(dt);
    syncView();
}

void Fishing::syncView() {
    using P = ui::FishingView;
    view_.phase = state_ == State::Entering   ? P::kEntering
                  : state_ == State::Ready    ? P::kReady
                  : state_ == State::Charging ? P::kCharging
                  : state_ == State::Casting  ? P::kCasting
                  : state_ == State::Waiting  ? P::kWaiting
                  : state_ == State::Reeling  ? P::kReeling
                  : state_ == State::Result   ? P::kResult
                                              : P::kLeaving;
    view_.power = state_ == State::Charging ? power_ : (state_ == State::Casting || state_ == State::Waiting ||
                                                        state_ == State::Reeling)
                                                          ? castPower_
                                                          : 0.0f;
    view_.aim = aim_;
    view_.rod = currentRod();
    const bool online = net_ && net_->inZone();
    view_.beans = online ? net_->ownStats().money : -1;
    if (curLocal_ || !online) {
        view_.tank = localTank_;
        view_.tankMax = 20;
    } else {
        view_.tank = net_->ownFish().tankCount;
        view_.tankMax = net_->ownFish().maxTank;
    }
    view_.timeoutFrac = state_ == State::Waiting ? waitTime_ / fish::kCastTimeout : 0.0f;
}

void Fishing::updateRodPitch(float dt) {
    float target = 35.0f;
    switch (state_) {
    case State::Charging: target = 35.0f + 40.0f * power_; break; // wind-up
    case State::Casting: target = 12.0f; break;                   // swing forward
    case State::Waiting: target = 28.0f; break;
    case State::Reeling: target = 55.0f; break;
    default: break;
    }
    const float rate = state_ == State::Casting ? 14.0f : 8.0f;
    rodPitch_ += (target - rodPitch_) * (1.0f - expf(-rate * dt));
}

void Fishing::rodMatrix(C3D_Mtx* out) const {
    float scale = 1.0f;
    if (state_ == State::Entering) scale = smooth(stateTime_ / 0.6f);
    else if (state_ == State::Leaving) scale = 1.0f - smooth(stateTime_ / kLeaveSec);
    if (scale < 0.01f) scale = 0.01f;
    Mtx_Identity(out);
    Mtx_Translate(out, cur_.x, cur_.y, cur_.z, true);
    Mtx_RotateZ(out, (cur_.h + aim_) * kDegToRad, true);
    Mtx_Translate(out, kHand[0], kHand[1], kHand[2], true);
    Mtx_RotateX(out, rodPitch_ * kDegToRad, true);
    Mtx_Scale(out, scale, scale, scale);
}

void Fishing::rodTip(float* tip) const {
    C3D_Mtx m;
    rodMatrix(&m);
    applyMtx(m, kRodTip, tip);
}

void Fishing::updateBob(float dt) {
    (void)dt;
    if (!bobVisible_) return;
    const float water = fish::waterLevel(currentArea());
    float tip[3];
    rodTip(tip);
    float lx = 0.0f, ly = 0.0f, lz = water;
    float blend = 1.0f; // 0 = rod tip, 1 = projectile
    switch (state_) {
    case State::Casting: {
        // DistributedFishingSpot.moveBobTask, in the aim frame of the spot.
        const float t = castTime_;
        const float vZero = castPower_ * fish::kVZeroMax;
        const float ang = castPower_ * fish::kAngleMaxDeg * kDegToRad;
        ly = fish::kBobStartY + vZero * cosf(ang) * t;
        lz = fish::kBobStartZ + vZero * sinf(ang) * t - fish::kGravity * t * t * 0.5f;
        blend = smooth(t / 0.15f);
        break;
    }
    case State::Waiting:
        ly = landing_.y;
        lz = water + sinf(bobBob_ * 1.8f) * kBobSink; // __doBobBob
        break;
    case State::Reeling: {
        const float f = smooth(reelTime_ / kReelSec);
        ly = landing_.y;
        lz = water + 1.5f * sinf(f * 3.14159265f); // hop out of the water while being pulled in
        blend = 1.0f - f;
        break;
    }
    default: break;
    }
    float hx, hy;
    rotH(cur_.h + castAim_, 0.0f, ly, &hx, &hy);
    const float px = cur_.x + hx, py = cur_.y + hy, pz = cur_.z + lz;
    bobX_ = tip[0] + (px - tip[0]) * blend;
    bobY_ = tip[1] + (py - tip[1]) * blend;
    bobZ_ = tip[2] + (pz - tip[2]) * blend;
}

void Fishing::applyCamera() {
    if (!active() || !player_ || !camStarted_) return;
    // Final camera: (0, -12, 15) relative to the toon, pitched down 38 degrees, in the spot frame.
    float bx, by;
    rotH(cur_.h, 0.0f, -kCamBack, &bx, &by);
    const C3D_FVec eye = FVec3_New(cur_.x + bx, cur_.y + by, cur_.z + kCamUp);
    float fx, fy;
    rotH(cur_.h, 0.0f, cosf(kCamPitchDeg * kDegToRad) * kCamLook, &fx, &fy);
    const C3D_FVec target = FVec3_New(eye.x + fx, eye.y + fy, eye.z - sinf(kCamPitchDeg * kDegToRad) * kCamLook);
    const float t = smooth(camT_);
    const C3D_FVec fromEye = state_ == State::Leaving ? player_->cameraEye() : camStartEye_;
    const C3D_FVec fromTarget = state_ == State::Leaving ? player_->cameraTarget() : camStartTarget_;
    auto mix = [&](const C3D_FVec& a, const C3D_FVec& b) {
        return FVec3_New(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    };
    player_->overrideCamera(mix(fromEye, eye), mix(fromTarget, target));
}

void Fishing::draw(render::Renderer& renderer) {
    if (!active() || !player_) return;
    if (rod_.loaded()) {
        C3D_Mtx m;
        rodMatrix(&m);
        renderer.drawModel(rod_, m);
    }
    if (!bobVisible_) return;
    if (bob_.loaded()) {
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, bobX_, bobY_, bobZ_, true);
        renderer.drawModel(bob_, m);
    }
    if (line_.loaded()) {
        float tip[3];
        rodTip(tip);
        const float dx = bobX_ - tip[0], dy = bobY_ - tip[1], dz = bobZ_ - tip[2];
        const float horiz = sqrtf(dx * dx + dy * dy);
        const float len = sqrtf(horiz * horiz + dz * dz);
        if (len > 0.05f) {
            C3D_Mtx m;
            Mtx_Identity(&m);
            Mtx_Translate(&m, tip[0], tip[1], tip[2], true);
            Mtx_RotateZ(&m, atan2f(-dx, dy), true);              // heading: +Y toward the bob
            Mtx_RotateX(&m, atan2f(dz, horiz > 1e-4f ? horiz : 1e-4f), true); // pitch up toward it
            Mtx_Scale(&m, 1.0f, len, 1.0f);
            renderer.drawModel(line_, m);
        }
    }
}

} // namespace tt::world
