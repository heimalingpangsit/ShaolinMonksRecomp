#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace shaolin::setup {
struct Source {
    std::filesystem::path path;
    bool folder = false;
};
using Progress = std::function<bool(uint64_t done, uint64_t total)>;
bool PrepareSource(const std::filesystem::path& input, Source& source,
                   std::vector<uint8_t>& executable, std::string& error, const Progress& progress = {});
}
