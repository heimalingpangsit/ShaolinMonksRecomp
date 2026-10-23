#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace shaolin::mods {
std::vector<uint8_t> ApplyDelta(std::span<const uint8_t> original, std::span<const uint8_t> delta);
}
