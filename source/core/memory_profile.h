#pragma once
#include <cstddef>

namespace tt::mem {
constexpr size_t kMiB = 1024u * 1024u;
struct MemoryProfile { size_t heap, linear, retainGeometry, retainTextures; };
// Select by granted application region, including HBL/older-launcher fallback.
// Reserves 16 MiB of the 80 MiB mode for code, stacks and system headroom.
constexpr MemoryProfile memoryProfile(size_t region, bool new3ds = false) {
    return region >= 124u * kMiB ? MemoryProfile{40u*kMiB, 64u*kMiB, 16u*kMiB, 16u*kMiB} :
           region >= 80u * kMiB ? MemoryProfile{28u*kMiB, 36u*kMiB, 10u*kMiB, 10u*kMiB} :
                                 MemoryProfile{24u*kMiB, 24u*kMiB, (new3ds ? 5u : 4u)*kMiB, (new3ds ? 5u : 4u)*kMiB};
}
}
