#pragma once
#include <filesystem>
#include <string>

namespace shaolin::paths {
// The locator stays in the OS default data folder, even after data is moved.
struct Locations {
    std::filesystem::path data, saves;
};
bool LoadLocations(const std::filesystem::path& locator, const Locations& defaults,
                   Locations& result, std::string& warning, bool apply_pending);
bool ScheduleLocations(const std::filesystem::path& locator, const Locations& current,
                       const Locations& requested, std::string& error);
}
