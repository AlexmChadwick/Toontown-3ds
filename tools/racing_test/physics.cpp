#include "world/racing_logic.h"
#include <cstdio>
int main() {
    for (int scenario = 0; scenario < 6; ++scenario) {
        tt::racing::Vehicle kart;
        kart.stopped = scenario == 5;
        kart.surface = scenario == 2 ? tt::racing::Surface::Ice : tt::racing::Surface::Gravel;
        kart.turbo = scenario == 3;
        for (int frame = 0; frame < 300; ++frame) {
            kart.update(1.0f / 30, scenario == 1 || scenario == 2 ? .6f : 0,
                        scenario == 4 ? -1 : 1);
        }
        std::printf("%d %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n", scenario, kart.position.x,
                    kart.position.y, kart.position.z, kart.velocity.x, kart.velocity.y,
                    kart.velocity.z, kart.heading, kart.lean);
    }
}
