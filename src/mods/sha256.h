#pragma once
#include <filesystem>
#include <span>
#include <string>
#include <cstdint>
namespace shaolin::mods {
std::string Sha256(std::span<const uint8_t> data);
std::string Sha256File(const std::filesystem::path& path);
}
