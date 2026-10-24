#include "world/racing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tt::world {

void Racing::loadAssets(uint16_t track) {
    if (loadedTrack_ == track && track_.loaded()) return;
    unloadAssets();
    loadedTrack_ = track;
    const char* names[] = {"screwball_stadium", "screwball_stadium_rev", "rustic_raceway",
                           "rustic_raceway_rev", "city_circuit", "city_circuit_rev",
                           "corkscrew_coliseum", "corkscrew_coliseum_rev", "airborne_acres",
                           "airborne_acres_rev", "blizzard_boulevard", "blizzard_boulevard_rev"};
    int index = track < 2 ? int(track) : track < 20 ? 0 : track < 40 ? int(track - 18) :
                track < 60 ? int(track - 36) : track < 62 ? int(track - 52) :
                track < 64 ? int(track - 54) : int(track - 54);
    if (index < 0 || index >= 12) index = 0;
    const char* name = names[index];
    char path[128];
    snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/racing/%s.ttm", name);
    track_.load(path);
    kart_.load("sdmc:/3ds/toontown3ds/data/racing/kart_default.ttm");
    pad_.load("sdmc:/3ds/toontown3ds/data/racing/kart_pad.ttm");
    snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/racing/%s.ttc", name);
    collision_.load(path);
}

void Racing::unloadAssets() {
    track_.unload(); kart_.unload(); pad_.unload(); collision_.unload(); loadedTrack_ = 0xffff;
}

void Racing::handleEvent(const net::Event& ev, const net::Client& client) {
    if (ev.type == net::EventType::RacingRaceUp || ev.type == net::EventType::RacingRaceState) {
        const net::RacingRace* race = client.racingRace();
        if (!race) return;
        active_ = true; race_ = race->doId; loadAssets(race->track);
        lap_ = race->localLap; place_ = race->localPlace ? race->localPlace : 1;
        finished_ = race->finished;
        snprintf(hudTitle_, sizeof(hudTitle_), "Goofy Speedway");
    } else if (ev.type == net::EventType::RacingPadUp) {
        active_ = true;
        pad_.load("sdmc:/3ds/toontown3ds/data/racing/kart_pad.ttm");
    } else if (ev.type == net::EventType::RacingGone) {
        if (race_ == ev.doId) { race_ = 0; finished_ = false; }
    }
}

void Racing::update(net::Client& client, const Input& input, float dt, float x, float y, float z, uint32_t nowMs) {
    const net::RacingRace* race = client.racingRace();
    if (race) {
        active_ = true; race_ = race->doId; loadAssets(race->track);
        if (!spawned_) { x_ = x; y_ = y; z_ = z; spawned_ = true; }
        if (race->started && !finished_) {
            const float steer = std::max(-1.0f, std::min(1.0f, input.pad.dx / 156.0f));
            if (input.held & KEY_A) speed_ = std::min(24.0f, speed_ + 18.0f * dt);
            else speed_ = std::max(0.0f, speed_ - 7.0f * dt);
            if (input.held & KEY_B) speed_ = std::max(0.0f, speed_ - 25.0f * dt);
            h_ -= steer * (35.0f + speed_ * 1.5f) * dt;
            const float rad = h_ * 3.14159265f / 180.0f;
            x_ += std::sin(rad) * speed_ * dt;
            y_ += std::cos(rad) * speed_ * dt;
            if ((nowMs / 200) != ((nowMs - uint32_t(dt * 1000.0f)) / 200))
                client.sendPosHpr(x_, y_, z_, h_, 0.0f, 0.0f);
            if ((input.down & KEY_R) != 0) client.sendRacingGag(race_, 0, 0, 1);
            lap_ = race->localLap; place_ = race->localPlace ? race->localPlace : 1;
            if (lap_ >= race->laps && race->laps != 0) finished_ = true;
        }
        snprintf(hudLines_[0], sizeof(hudLines_[0]), "Speed %02d", int(speed_ + 0.5f));
        snprintf(hudLines_[1], sizeof(hudLines_[1]), "Lap %u/%u", unsigned(lap_), unsigned(race->laps));
        snprintf(hudLines_[2], sizeof(hudLines_[2]), "Place %u", unsigned(place_));
        if (finished_) { snprintf(hudLines_[0], sizeof(hudLines_[0]), "Finished!"); destination_ = race->zone; }
        (void)nowMs;
        return;
    }
    // Starting blocks are deliberately proximity based: the server remains authoritative.
    for (int i = 0; i < net::kMaxRacingBlocks; ++i) {
        const net::RacingBlock& b = client.racingBlockSlot(i);
        if (!b.active || b.occupant != 0) continue;
        const float dx=x-b.x, dy=y-b.y, dz=z-b.z;
        if (dx*dx + dy*dy + dz*dz < 16.0f && (input.down & KEY_A)) {
            client.sendRacingEnter(b.doId, false);
            break;
        }
    }
}

bool Racing::spawnPose(float* x, float* y, float* z, float* h) {
    if (!active_ || spawned_) return false;
    spawned_ = true; *x = 0; *y = -8; *z = 0; *h = 0; return true;
}

void Racing::draw(render::Renderer& renderer, const net::Client& client) const {
    if (!active_) return;
    C3D_Mtx m;
    Mtx_Identity(&m);
    if (track_.loaded()) renderer.drawModel(const_cast<render::Model&>(track_), m);
    for (int i=0;i<net::kMaxRacingBlocks;++i) {
        const net::RacingBlock& b=client.racingBlockSlot(i);
        if (!b.active || !pad_.loaded()) continue;
        Mtx_Identity(&m); Mtx_Translate(&m,b.x,b.y,b.z,true); renderer.drawModel(const_cast<render::Model&>(pad_),m);
    }
    if (race_ && kart_.loaded()) {
        Mtx_Identity(&m); Mtx_Translate(&m,x_,y_,z_,true); Mtx_RotateZ(&m,h_ * 3.14159265f / 180.0f,true);
        renderer.drawModel(const_cast<render::Model&>(kart_),m);
    }
}

void Racing::shutdown() { active_=false; race_=0; spawned_=false; unloadAssets(); }

} // namespace tt::world
