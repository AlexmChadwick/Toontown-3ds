#pragma once
#include <cmath>
namespace tt::world {
struct SkyHeading { float first, second; };
inline SkyHeading skyHeading(float seconds) {
    // Wrap rings independently: wrapping the first before multiplying by -0.8
    // jumps the second ring by 72 degrees every 24 minutes.
    return {fmodf(seconds*.25f, 360.0f), fmodf(seconds*-.20f, 360.0f)};
}
}
