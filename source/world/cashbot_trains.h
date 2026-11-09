// Cashbot HQ's freight trains (safezone/Train.py, run by CashbotHQExterior on four tracks; not
// part of the zone bake). Each train's run is a pure function of the network clock: run r of
// track i starts at 3.75 i + 15 r seconds and random.seed(i + r) picks its cars, whether it
// stops halfway and how long it takes, so every client sees the same trains. A train that
// meets our Toon squishes it (b_squish(10)) and its sides push the Toon off the track.
// Models: tools/pipeline/bake_cashbot_train.py.
#pragma once

#include <cstdint>

#include "render/model.h"
#include "world/player.h"

namespace tt::net {
class Client;
}
namespace tt::render {
class Renderer;
}

namespace tt::world {

class CashbotTrains {
public:
    // Only Cashbot HQ's shipping station (12000) has trains.
    void setZone(uint32_t zoneId);
    // Once per frame with our Toon's feet: places the trains; true when one just hit us
    // (the squish was sent; main.cpp plays it like a stomper's).
    bool update(net::Client& client, float x, float y, float z, uint32_t nowMs);
    void draw(render::Renderer& renderer) const;
    const DynamicSolids* solids() const { return present_ ? &solids_ : nullptr; }
    void shutdown();

private:
    static constexpr int kTracks = 4, kMaxCars = 9;
    struct Run {
        int number = INT32_MIN;
        int cars = 0;
        uint8_t type[kMaxCars] = {};
        bool stop = false;
        int totalTime = 0;
    };
    struct Train {
        Run run;
        bool shown = false;
        float x = 0.0f, y = 0.0f; // the locomotive's origin
    };
    static void push(void* user, float* x, float* y, float z, float radius);
    void place(int track, double t);
    render::Model loco_, cars_[3];
    Train trains_[kTracks];
    DynamicSolids solids_;
    bool present_ = false, loaded_ = false;
    double stunnedUntil_ = -1e9; // network seconds; b_squish is refused while stunned
};

} // namespace tt::world
