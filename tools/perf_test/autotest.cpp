#include <cassert>
#include <cstdio>
#include <filesystem>
#include <unistd.h>
#define private public
#include "core/autotest.h"
#undef private
int main() {
    std::filesystem::create_directories("sdmc-out/perf/autotest-fixture/sdmc:/3ds/toontown3ds");
    assert(chdir("sdmc-out/perf/autotest-fixture") == 0);
    const char* path = "sdmc:/3ds/toontown3ds/autotest";
    auto config = [&](const char* text) {
        FILE* file = std::fopen(path, "w");
        assert(file);
        std::fputs(text, file);
        std::fclose(file);
    };
    config("shots off\nonly run forward\n");
    tt::autotest::Runner runner;
    assert(runner.begin());
    runner.pendingShot_ = "test";
    assert(!runner.shotsEnabled_ && !runner.pendingShot());
    config("only run forward\n");
    assert(runner.begin());
    runner.pendingShot_ = "test";
    assert(runner.shotsEnabled_ && runner.pendingShot());
    tt::autotest::Runner enabled, disabled;
    config("only run forward\n");
    assert(enabled.begin());
    config("shots off\nonly run forward\n");
    assert(disabled.begin());
    int enabledShots = 0;
    for (int frame = 0; frame < 200; ++frame) {
        circlePosition a{}, b{};
        touchPosition ta{}, tb{};
        u32 ha = 0, hb = 0, da = 0, db = 0;
        int sa = 0, sb = 0;
        enabled.apply(1.0f / 30, a, ha, da, ta, sa);
        disabled.apply(1.0f / 30, b, hb, db, tb, sb);
        assert(a.dx == b.dx && a.dy == b.dy && ha == hb && da == db && ta.px == tb.px && ta.py == tb.py &&
               sa == sb);
        assert(enabled.active() == disabled.active() && enabled.stepTime() == disabled.stepTime());
        enabledShots += enabled.pendingShot() != nullptr;
        assert(!disabled.pendingShot());
    }
    assert(enabledShots > 0);
    std::puts("Actual autotest parser: capture-off getter, default capture and repeated begin reset PASS");
}
