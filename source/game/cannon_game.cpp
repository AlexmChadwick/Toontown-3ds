#include "game/cannon_game.h"
#include "world/toon_avatar.h"

#include <cmath>
#include <cstdio>
#include "core/log.h"

namespace tt::game {

namespace {
constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";
constexpr float kPi = 3.14159265358979323846f;
// DistributedCannonGame.py WORLD_SCALE: TT_sky is set to 2.0. The bake stores it at 2.8 (the
// ground's GROUND_SCALE), so the draw undoes that factor.
constexpr float kWorldScale = 2.0f;
constexpr float kSkyBakedScale = 2.8f;
} // namespace

void CannonGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    char path[128];
    snprintf(path, sizeof(path), "%scannon_gameground.ttm", kDir);
    assetBatch_.load(ground_, path);
    snprintf(path, sizeof(path), "%scannon_hill.ttm", kDir);
    assetBatch_.load(hill_, path);
    snprintf(path, sizeof(path), "%scannon_tower.ttm", kDir);
    assetBatch_.load(tower_, path);
    snprintf(path, sizeof(path), "%scannon_base.ttm", kDir);
    assetBatch_.load(cannonBase_, path);
    snprintf(path, sizeof(path), "%scannon_barrel.ttm", kDir);
    assetBatch_.load(cannonBarrel_, path);
    snprintf(path, sizeof(path), "%sminigame_sky.ttm", kDir);
    assetBatch_.load(sky_, path);

    const net::Minigame* mg = client.minigame();
    slot_ = 0;
    numPlayers_ = mg ? mg->numParticipants : 1;
    if (numPlayers_ < 1) numPlayers_ = 1;
    if (mg) {
        for (int i = 0; i < numPlayers_; ++i) {
            if (mg->participants[i] == client.avatarId()) slot_ = i;
        }
    }

    float diff = 0.0f;
    if (mg && mg->difficultyOverride != net::kNoDifficultyOverride) {
        diff = mg->difficultyOverride / 65536.0f;
    }

    uint32_t seed = mg ? mg->doId : 1001;
    RandomNumGen rng(seed);
    towerPos_ = cannongame::getTowerPosition(rng, diff);

    state_ = State::Aiming;
    done_ = false;
    autoPlay_ = autoPlay;
    startMs_ = nowMs;
    stateMs_ = nowMs;
    lastBroadcastMs_ = nowMs;

    zRot_ = 0.0f;
    angle_ = cannongame::kCannonAngleMin; // upstream cannonPositionDict = [0, ANGLE_MIN]

    // Position toon in cannon
    const float cx = cannongame::cannonX(slot_, numPlayers_);
    toonX_ = cx;
    toonY_ = cannongame::kCannonY;
    toonZ_ = cannongame::kCannonZ;
    toonH_ = 0.0f;

    camEyeX_ = cx;
    camEyeY_ = cannongame::kCannonY - 25.0f;
    camEyeZ_ = cannongame::kCannonZ + 7.0f;
    camTgtX_ = cx;
    camTgtY_ = cannongame::kCannonY + 40.0f;
    camTgtZ_ = cannongame::kCannonZ + 7.0f;

    snprintf(lines_[0], sizeof(lines_[0]), "Aim: Circle Pad  |  Fire: A");
    snprintf(lines_[1], sizeof(lines_[1]), "Angle: %.1f  Heading: %.1f", angle_, zRot_);
    snprintf(lines_[2], sizeof(lines_[2]), "Time: 90");

    TT_LOG("cannon_game: init slot %d/%d, tower at (%.1f, %.1f)", slot_, numPlayers_, towerPos_.x, towerPos_.y);
}

void CannonGame::shutdown() {
    assetBatch_.clear();
    C3D_FrameSync();
    ground_.unload();
    hill_.unload();
    tower_.unload();
    cannonBase_.unload();
    cannonBarrel_.unload();
    sky_.unload();
}

void CannonGame::update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs) {
    const float cx = cannongame::cannonX(slot_, numPlayers_);
    const float inGameSec = static_cast<int>(nowMs - startMs_) / 1000.0f;
    const int timeLeft = static_cast<int>(cannongame::kGameTime - inGameSec);

    switch (state_) {
    case State::Aiming: {
        float ax = pad.dx / 156.0f;
        float ay = pad.dy / 156.0f;
        bool changed = false;

        if (std::abs(ax) > 0.15f) {
            zRot_ -= ax * cannongame::kCannonRotVel * dt;
            if (zRot_ < cannongame::kCannonRotMin) zRot_ = cannongame::kCannonRotMin;
            if (zRot_ > cannongame::kCannonRotMax) zRot_ = cannongame::kCannonRotMax;
            changed = true;
        }
        if (std::abs(ay) > 0.15f) {
            angle_ += ay * cannongame::kCannonAngleVel * dt;
            if (angle_ < cannongame::kCannonAngleMin) angle_ = cannongame::kCannonAngleMin;
            if (angle_ > cannongame::kCannonAngleMax) angle_ = cannongame::kCannonAngleMax;
            changed = true;
        }

        if (autoPlay_ && (nowMs - stateMs_) > 1500) {
            // Compute aim to hit water tower
            const float dx = towerPos_.x - cx;
            const float dy = towerPos_.y - cannongame::kCannonY;
            const float dist = std::sqrt(dx * dx + dy * dy);
            zRot_ = -std::atan2(dx, dy) * 180.0f / kPi;
            if (zRot_ < cannongame::kCannonRotMin) zRot_ = cannongame::kCannonRotMin;
            if (zRot_ > cannongame::kCannonRotMax) zRot_ = cannongame::kCannonRotMax;

            // Target water bucket height relative to cannon
            const float dz = (towerPos_.z + cannongame::kTowerHeight) - (cannongame::kCannonZ + cannongame::kBarrelPivotZ);
            const float v = cannongame::kInitialVelocity;
            const float g = cannongame::kGravity;
            const float v2 = v * v;
            const float disc = v2 * v2 - g * (g * dist * dist + 2.0f * dz * v2);
            if (disc >= 0.0f) {
                // High arc root
                const float tanTheta = (v2 + std::sqrt(disc)) / (g * dist);
                angle_ = std::atan(tanTheta) * 180.0f / kPi;
                if (angle_ < cannongame::kCannonAngleMin) angle_ = cannongame::kCannonAngleMin;
                if (angle_ > cannongame::kCannonAngleMax) angle_ = cannongame::kCannonAngleMax;
            }
            kDown |= KEY_A;
        }

        if (changed && (nowMs - lastBroadcastMs_) > 500) {
            client.sendCannonPosition(zRot_, angle_);
            lastBroadcastMs_ = nowMs;
        }

        snprintf(lines_[0], sizeof(lines_[0]), "Aim: Circle Pad  |  Fire: A");
        snprintf(lines_[1], sizeof(lines_[1]), "Angle: %.1f  Heading: %.1f", angle_, zRot_);
        snprintf(lines_[2], sizeof(lines_[2]), "Time: %d", timeLeft > 0 ? timeLeft : 0);

        // Camera stays behind cannon
        camEyeX_ = cx;
        camEyeY_ = cannongame::kCannonY - 25.0f;
        camEyeZ_ = cannongame::kCannonZ + 7.0f;
        camTgtX_ = cx;
        camTgtY_ = cannongame::kCannonY + 40.0f;
        camTgtZ_ = cannongame::kCannonZ + 7.0f;

        toonX_ = cx;
        toonY_ = cannongame::kCannonY;
        toonZ_ = cannongame::kCannonZ;
        toonH_ = zRot_;

        if (kDown & KEY_A) {
            client.sendCannonLit(zRot_, angle_);
            flight_ = cannongame::calcFlight(cx, zRot_, angle_, towerPos_);
            if (flight_.hitWhat == cannongame::HitType::Water) {
                client.sendCannonToonWillLandInWater(flight_.timeOfImpact);
            }
            state_ = State::Flying;
            stateMs_ = nowMs;
            TT_LOG("cannon_game: fired (rot=%.1f, ang=%.1f), impact in %.2fs (hit=%d)", zRot_, angle_, flight_.timeOfImpact, int(flight_.hitWhat));
        }
        break;
    }

    case State::Flying: {
        const float t = static_cast<int>(nowMs - stateMs_) / 1000.0f;
        const cannongame::Vec3 pos = cannongame::trajectoryPos(flight_.startPos, flight_.startVel, t);
        const cannongame::Vec3 vel = cannongame::trajectoryVel(flight_.startVel, t);

        toonX_ = pos.x;
        toonY_ = pos.y;
        toonZ_ = pos.z;
        toonH_ = std::atan2(-vel.x, vel.y) * 180.0f / kPi;
        toonPitch_=-90+std::atan2(vel.z,std::sqrt(vel.x*vel.x+vel.y*vel.y))*180/kPi;

        // Camera follows toon from behind
        camEyeX_ = toonX_;
        camEyeY_ = toonY_ - 24.0f;
        camEyeZ_ = toonZ_ + 10.0f;
        camTgtX_ = toonX_;
        camTgtY_ = toonY_ + 10.0f;
        camTgtZ_ = toonZ_;

        snprintf(lines_[0], sizeof(lines_[0]), "Weeee!");
        lines_[1][0] = 0;
        snprintf(lines_[2], sizeof(lines_[2]), "Time: %d", timeLeft > 0 ? timeLeft : 0);

        if (t >= flight_.timeOfImpact) {
            if (flight_.hitWhat == cannongame::HitType::Water) {
                state_ = State::Won;
                done_ = true;
                toonZ_ = towerPos_.z + cannongame::kTowerHeight;
                snprintf(lines_[0], sizeof(lines_[0]), "SPLASH! Direct hit on the tower!");
                snprintf(lines_[1], sizeof(lines_[1]), "You earned %d jellybeans!", flight_.score);
                TT_LOG("cannon_game: landed in water, score %d", flight_.score);
            } else {
                state_ = State::Landed;
                stateMs_ = nowMs;
                if (flight_.hitWhat == cannongame::HitType::Tower) {
                    snprintf(lines_[0], sizeof(lines_[0]), "CLANG! Hit the tower.");
                } else {
                    snprintf(lines_[0], sizeof(lines_[0]), "OOF! Hit the ground.");
                    toonZ_ = 0.0f;
                }
                lines_[1][0] = 0;
            }
        }
        break;
    }

    case State::Landed: {
        const float inState = static_cast<int>(nowMs - stateMs_) / 1000.0f;
        if (inState > 2.0f) {
            state_ = State::Aiming;
            stateMs_ = nowMs;
        }
        break;
    }

    case State::Won:
        // Wait for server gameOver / PurchaseManager
        break;
    }
}

void CannonGame::toonPose(float* x, float* y, float* z, float* h) const {
    *x = toonX_;
    *y = toonY_;
    *z = toonZ_;
    *h = toonH_;
    if(state_==State::Aiming) {
        C3D_Mtx barrel;Mtx_Identity(&barrel);
        Mtx_Translate(&barrel,toonX_,toonY_,toonZ_+cannongame::kBarrelPivotZ,true);
        Mtx_RotateZ(&barrel,zRot_*kPi/180,true);Mtx_RotateX(&barrel,angle_*kPi/180,true);
        *x=barrel.r[0].y*cannongame::kBarrelOffset+barrel.r[0].w;
        *y=barrel.r[1].y*cannongame::kBarrelOffset+barrel.r[1].w;
        *z=barrel.r[2].y*cannongame::kBarrelOffset+barrel.r[2].w;
    }
}

void CannonGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    *eye = FVec3_New(camEyeX_, camEyeY_, camEyeZ_);
    *target = FVec3_New(camTgtX_, camTgtY_, camTgtZ_);
}

void CannonGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    Mtx_Identity(&m);

    if (sky_.loaded()) {
        Mtx_Identity(&m);
        const float s = kWorldScale / kSkyBakedScale;
        Mtx_Scale(&m, s, s, s);
        renderer.drawModel(sky_, m);
    }

    // Ground
    if (ground_.loaded()) {
        Mtx_Identity(&m);
        renderer.drawModel(ground_, m);
    }

    // Hill
    if (hill_.loaded()) {
        Mtx_Identity(&m);
        renderer.drawModel(hill_, m);
    }

    // Water tower
    if (tower_.loaded()) {
        Mtx_Identity(&m);
        Mtx_Translate(&m, towerPos_.x, towerPos_.y, towerPos_.z, true);
        renderer.drawModel(tower_, m);
    }

    // Cannons
    for (int i = 0; i < numPlayers_; ++i) {
        const float cx = cannongame::cannonX(i, numPlayers_);
        const float rot = (i == slot_) ? zRot_ : 0.0f;
        const float ang = (i == slot_) ? angle_ : cannongame::kCannonAngleMin;

        // Base
        if (cannonBase_.loaded()) {
            Mtx_Identity(&m);
            Mtx_Translate(&m, cx, cannongame::kCannonY, cannongame::kCannonZ, true);
            Mtx_RotateZ(&m, rot * kPi / 180.0f, true);
            renderer.drawModel(cannonBase_, m);
        }

        // Barrel: the cannon node takes the heading; the barrel node sits at its bam origin
        // (0, 0, 1.89703) and pitches by +angle (Panda P, nose up).
        if (cannonBarrel_.loaded()) {
            Mtx_Identity(&m);
            Mtx_Translate(&m, cx, cannongame::kCannonY, cannongame::kCannonZ + cannongame::kBarrelPivotZ, true);
            Mtx_RotateZ(&m, rot * kPi / 180.0f, true);
            Mtx_RotateX(&m, ang * kPi / 180.0f, true);
            renderer.drawModel(cannonBarrel_, m);
        }
    }
}

void CannonGame::applyAnimation(world::ToonAvatar& avatar) const {
    using namespace world;
    avatar.setHeadOnly(state_==State::Aiming);
    // DistributedCannonGame.__loadToonInCannon: the head sits in the barrel at (0, 6, 0) with
    // hpr (0, -45, 0), so its world pitch is the barrel pitch minus 45.
    avatar.setHeadPitch(angle_-45);
    const float height=avatar.look()?avatar.look()->nametagHeight-.5f:4;
    avatar.setBodyPose(state_==State::Flying?toonPitch_:
        state_==State::Landed && flight_.hitWhat!=cannongame::HitType::Tower?-150:0,
        state_==State::Flying?-height/2:0);
    if (state_==State::Flying) avatar.playAction(kAnimSwim,ClipMode::Loop);
    else if(state_==State::Landed && flight_.hitWhat==cannongame::HitType::Tower) avatar.hold(kAnimSlipForward,25/24.0f);
    else if(state_==State::Landed) avatar.playAction(kAnimRun,ClipMode::Loop,2);
    else avatar.playAction(kAnimNeutral,ClipMode::Loop);
}

} // namespace tt::game
