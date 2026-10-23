#pragma once
#include <atomic>
#include <cstdint>

namespace shaolin::sfd {

// The original CRI movie path enables the Xbox's analog soft display filter
// for the duration of an SFD, then disables it on exit. On a digital PC window
// use a small, title-controlled reconstruction filter only during that window.
inline std::atomic<bool> soft_display_filter{false};

inline void SetSoftDisplayFilter(uint32_t enable) {
    soft_display_filter.store(enable != 0, std::memory_order_relaxed);
}

inline uint32_t PresentationTaps() {
    return soft_display_filter.load(std::memory_order_relaxed) ? 2u : 1u;
}

}  // namespace shaolin::sfd
