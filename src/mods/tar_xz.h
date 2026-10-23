#pragma once
#include "progress.h"
#include <filesystem>
namespace shaolin::mods {
// destination must be an empty, private staging folder.
void ExtractTarXz(const std::filesystem::path& file,const std::filesystem::path& destination,const Progress& progress={});
}
