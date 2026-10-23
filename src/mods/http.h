#pragma once
#include "progress.h"
#include <filesystem>
#include <string>
namespace shaolin::mods {
void Download(std::string_view url,const std::filesystem::path& file,uint64_t maximum,const Progress& progress={});
}
