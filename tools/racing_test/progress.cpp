#include "world/racing_logic.h"
#include <cassert>
#include <cmath>
int main() {
    using namespace tt::racing;
    constexpr int count = 4000;
    static CurvePoint curve[count];
    for (int i = 0; i < count; ++i) {
        const float angle = float(i) * 6.28318530718f / count;
        curve[i] = {100 * std::cos(angle), 100 * std::sin(angle), 0, 0};
    }
    Progress p;
    p.reset(curve, count, {100, 0, 0}, 0);
    for (int step = 1; step <= count * 2; ++step) {
        const auto &point = curve[step % count];
        const float heading = float(step % count) * 360 / count;
        p.update(curve, count, {point.x, point.y, 0}, heading, 0);
    }
    assert(p.laps == 2 && !p.wrongWay);
    for (int step = count - 1; step >= 0; --step) {
        const auto &point = curve[step];
        p.update(curve, count, {point.x, point.y, 0}, float(step) * 360 / count + 180, 0);
    }
    assert(p.laps == 1 && p.wrongWay);
    p.reset(curve, count, {curve[count - 1].x, curve[count - 1].y, 0}, 0);
    assert(p.laps == -1);
    p.update(curve, count, {curve[0].x, curve[0].y, 0}, 0, 0);
    assert(p.laps == 0); // crossing from the grid must not complete a whole lap.
    assert(trackIndex(0) == 0 && trackIndex(21) == 3 && trackIndex(65) == 11);
    assert(trackIndex(2) == -1 && !trackName(65535));
}
