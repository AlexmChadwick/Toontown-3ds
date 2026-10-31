#include "core/memory_profile.h"
#include <cassert>
#include <initializer_list>
#include "render/upload_quota.h"
#include "render/quality.h"
#include "world/stream_card.h"
int main() {
    auto phase = tt::world::StreamCardPhase::Ready;
    assert(!tt::world::retireStreamCard(phase)); // arrival frame stays covered
    assert(phase == tt::world::StreamCardPhase::Retire);
    assert(tt::world::retireStreamCard(phase));  // next existing GPU fence releases once
    assert(!tt::world::retireStreamCard(phase));
    phase = tt::world::StreamCardPhase::Streaming;
    assert(!tt::world::retireStreamCard(phase));
    const auto oldQuality = tt::render::qualityFor(false);
    const auto newQuality = tt::render::qualityFor(true);
    assert(oldQuality.drawDistance == newQuality.drawDistance);
    assert(oldQuality.lodNear == newQuality.lodNear && oldQuality.fogEnd == newQuality.fogEnd);
    tt::render::UploadQuota quota{16u*1024u};
    assert(!quota.consume(8u*1024u));
    assert(quota.consume(8u*1024u));
    using namespace tt::mem;
    constexpr auto fallback = memoryProfile(64*kMiB);
    static_assert(memoryProfile(64*kMiB, true).retainTextures == 5*kMiB);
    static_assert(fallback.heap == 24*kMiB && fallback.linear == 24*kMiB);
    for (size_t region : {0u, 64u, 80u, 124u}) {
        const auto p = memoryProfile(region*kMiB);
        assert(p.heap+p.linear <= (region ? region : 64)*kMiB);
        assert(p.linear >= p.retainGeometry+p.retainTextures+12*kMiB);
    }
    static_assert(memoryProfile(80*kMiB).linear == 36*kMiB);
    static_assert(memoryProfile(124*kMiB).retainGeometry == 16*kMiB);
}
