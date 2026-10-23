#pragma once
#include <filesystem>
#include <string>
#include "game_import.h"

namespace shaolin::setup {
std::filesystem::path RememberedDisc();
bool RememberDisc(const std::filesystem::path& disc, std::string& error);
bool ChooseDisc(std::filesystem::path& disc);
bool PrepareGame(const std::filesystem::path& input, Source& source,
                 std::vector<uint8_t>& executable, std::string& error, bool show_progress);
}
