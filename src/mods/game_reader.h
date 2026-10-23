#pragma once
#include "catalog.h"
#include "kernel/disc.h"
#include <filesystem>
namespace shaolin::mods {
class GameReader {
    std::filesystem::path folder_;
    kernel::Disc disc_;
    bool image_=false;
    std::vector<uint8_t> Read(std::string name,uint64_t offset=0,uint64_t size=UINT64_MAX);
public:
    explicit GameReader(const std::filesystem::path& source);
    std::vector<uint8_t> Original(const Patch& patch);
};
}
