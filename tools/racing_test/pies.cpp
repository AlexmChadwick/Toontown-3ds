#include "world/racing_logic.h"
#include <cstdio>
int main() {
    for (int scenario = 0; scenario < 2; ++scenario) {
        tt::racing::Pie pie;
        pie.launch({0, 0, 0}, {10, 30, 0}, 25, scenario == 1);
        for (int frame = 0; frame < 90; ++frame)
            pie.update(1.0f / 30, {0, 5000, 30}, 20);
        std::printf("%d %.6f %.6f %.6f %.6f %.6f %.6f\n", scenario, pie.position.x, pie.position.y,
                    pie.position.z, pie.velocity.x, pie.velocity.y, pie.velocity.z);
    }
}
