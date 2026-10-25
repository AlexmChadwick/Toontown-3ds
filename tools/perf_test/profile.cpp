#include "core/profile.h"
#include <cassert>
#include <cmath>
#include <cstdio>
static uint64_t ticks=1;
static uint64_t clockTick() { return ticks; }
int main() {
    using namespace tt::prof;
    init();
    setClockForTest(clockTick);
    frameBegin();
    for (int frame=0;frame<31;++frame) {
        ticks+=268112;
        lap(kFrontUpdate);
        ticks+=804336;
        lap(kBossDraw);
        ticks+=8043356;
        lap(kWait);
        ticks+=268112;
        frameBegin();
    }
    assert(avgMs(kAudio)==0);
    assert(std::fabs(avgMs(kFrontUpdate)-1)<.01f);
    assert(std::fabs(avgMs(kBossDraw)-3)<.01f);
    assert(std::fabs(avgMs(kOther)-1)<.01f);
    assert(std::fabs(busyAvgMs()-5)<.01f);
    for(int i=0;i<kCount;++i) assert(name(Section(i))[0]);
    std::puts("Exclusive profiler, early-owner tail, names and busy/wait accounting PASS");
}
