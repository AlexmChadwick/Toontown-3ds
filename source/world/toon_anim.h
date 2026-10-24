// Stable baked animation identity and source timing. No platform dependencies.
#pragma once
#include <cstring>
#include <cstdint>
namespace tt::world {
enum ToonAnim : int {
#define TT_TOON_CLIP(id, name) id,
#include "world/toon_clips.inc"
#undef TT_TOON_CLIP
    kAnimCount
};
inline constexpr const char* kToonAnimNames[kAnimCount] = {
#define TT_TOON_CLIP(id, name) name,
#include "world/toon_clips.inc"
#undef TT_TOON_CLIP
};
static_assert(kAnimCount==51 && kAnimWave==4,"stable Toon clip IDs");
inline ToonAnim toonAnimByName(const char* name) {
    if (name) for (int i=0;i<kAnimCount;i++) if (!std::strcmp(name,kToonAnimNames[i])) return static_cast<ToonAnim>(i);
    return kAnimNeutral;
}
struct ToonSourceTiming { const char* prefix; uint16_t frames[kAnimCount]; };
inline constexpr ToonSourceTiming kToonSourceTiming[] = {
#include "world/toon_timing.inc"
};
inline float toonDuration(ToonAnim clip,const char* key=nullptr) {
    if (clip<0 || clip>=kAnimCount) return 0;
    if (key) for (const auto& row : kToonSourceTiming)
        if (!std::strncmp(key,row.prefix,std::strlen(row.prefix)) && row.frames[clip]) return row.frames[clip]/24.0f;
    uint16_t frames=0;
    for (const auto& row : kToonSourceTiming) if (row.frames[clip]>frames) frames=row.frames[clip];
    return frames/24.0f;
}
} // namespace tt::world
