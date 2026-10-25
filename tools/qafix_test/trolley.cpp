#include "core/trolley_progress.h"
#include <cassert>
int main() {
    using tt::autotest::TrolleyProgress;
    TrolleyProgress p;
    assert(!p.mayAdvance("board trolley") && !p.mayAdvance("trolley: ride"));
    p.seated = true;
    assert(p.mayAdvance("board trolley") && !p.mayAdvance("trolley: game"));
    p.started = true;
    assert(p.mayAdvance("trolley: ride") && !p.mayAdvance("trolley: game"));
    p.ended = true;
    assert(p.mayAdvance("trolley: game") && !p.mayAdvance("trolley: purchase"));
    // Sending requestExit still does not acknowledge the server or close a session.
    assert(!p.mayAdvance("trolley: purchase"));
    p.returned = true;
    assert(p.mayAdvance("trolley: purchase") && p.mayAdvance("run forward"));
}
