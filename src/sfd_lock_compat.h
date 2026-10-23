#pragma once
#include <cstdint>
#include "gpu/texture.h"

namespace shaolin::sfd {

// The CRI player locks one of two video surfaces before writing its next
// decoded frame. The SDK's generic Lock2DSurface wrapper did not invalidate
// ordinary (flags=0) CPU writes; its sampled texture checksum can therefore
// miss small changed regions. Keep the workaround strictly to this title's
// full-frame, writable SFD surface layouts.
inline bool NeedsWriteInvalidation(uint32_t resource, uint32_t face, uint32_t level,
                                   uint32_t rect, uint32_t flags) {
    if (!resource || face || level || rect || flags) return false;
    gpu::TextureDesc d{};
    if (!gpu::Describe(resource, d)) return false;
    return d.linear && !d.cube && d.depth == 1 && d.levels == 1 &&
           d.width == 640 && (d.height == 480 || d.height == 288) &&
           (d.format == 0x12 || d.format == 0x24);
}

} // namespace shaolin::sfd
