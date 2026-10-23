#include "path_preferences.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <toml++/toml.hpp>
#include "save_storage.h"
namespace fs = std::filesystem;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Put(const fs::path& p, const char* text) { fs::create_directories(p.parent_path()); std::ofstream(p) << text; }
std::string Get(const fs::path& p) { std::ifstream f(p); return {std::istreambuf_iterator<char>(f), {}}; }
int main() {
    const auto base = fs::temp_directory_path() / ("shaolin-path-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto locator = base / "default/paths.toml";
        shaolin::paths::Locations before{base / "old-data", base / "old-saves"}, after{base / "new-data", base / "new-saves"}, loaded;
        Put(before.data / "settings.toml", "volume = 71\n");
        Put(before.data / "games/game/default.xbe", "synthetic game");
        Put(before.data / "game-source.toml", ("disc = '" + (before.data / "games/game").generic_string() + "'\n").c_str());
        Put(before.saves / "hdd/E/UDATA/campaign", "campaign bytes");
        std::string error;
        Require(shaolin::paths::ScheduleLocations(locator, before, after, error), error.c_str());
        Require(fs::exists(locator), "Scheduling must persist a locator before a move");
        Require(fs::exists(before.saves / "hdd/E/UDATA/campaign"), "Scheduling must not move live saves");
        Require(shaolin::paths::LoadLocations(locator, before, loaded, error, false), error.c_str());
        Require(loaded.data == before.data, "Read-only inspection must not apply pending moves");
        Require(shaolin::paths::LoadLocations(locator, before, loaded, error, true), error.c_str());
        Require(error.empty(), error.c_str());
        Require(loaded.data == after.data && loaded.saves == after.saves, "Both roots must change after verified copy");
        Require(Get(after.saves / "hdd/E/UDATA/campaign") == "campaign bytes", "Save bytes must survive migration");
        Require(Get(after.data / "settings.toml") == "volume = 71\n", "Settings must survive migration");
        const auto selection = toml::parse(Get(after.data / "game-source.toml"))["disc"].value_or(std::string{});
        Require(fs::path(selection) == after.data / "games/game", "Imported source must follow data move");
        Require(!fs::exists(before.data / "settings.toml") && !fs::exists(before.saves / "hdd/E/UDATA/campaign"), "Verified source files must be removed after publishing roots");
        Require(shaolin::paths::LoadLocations(locator, before, loaded, error, true) && loaded.data == after.data, "New roots must persist");
        Put(base / "occupied/keep", "keep");
        auto invalid = after; invalid.data = base / "occupied";
        Require(!shaolin::paths::ScheduleLocations(locator, after, invalid, error), "Nonempty destination must be rejected");
        invalid = after; invalid.data = after.data / "nested";
        Require(!shaolin::paths::ScheduleLocations(locator, after, invalid, error), "Nested destinations must be rejected");
        invalid = after; invalid.saves = base / "late-conflict";
        Require(shaolin::paths::ScheduleLocations(locator, after, invalid, error), error.c_str());
        Put(invalid.saves / "keep", "other campaign");
        error.clear();
        Require(shaolin::paths::LoadLocations(locator, before, loaded, error, true), "Failed move must permit startup with old roots");
        Require(loaded.saves == after.saves && !error.empty(), "Failed move must keep old root and explain failure");
        Require(Get(after.saves / "hdd/E/UDATA/campaign") == "campaign bytes" && Get(invalid.saves / "keep") == "other campaign", "Failure must preserve both trees");
        {
            invalid = after; invalid.saves = base / "locked-move";
            Require(shaolin::paths::ScheduleLocations(locator, after, invalid, error), error.c_str());
            shaolin::storage::DirectoryLock lock;
            Require(lock.Acquire(after.saves / "hdd", error), error.c_str());
            error.clear();
            Require(shaolin::paths::LoadLocations(locator, before, loaded, error, true) && loaded.saves == after.saves && !error.empty(), "Active save locks must block relocation");
            Require(!fs::exists(invalid.saves) && Get(after.saves / "hdd/E/UDATA/campaign") == "campaign bytes", "Locked saves must remain intact");
        }
        const shaolin::paths::Locations nested{base / "linux-data", base / "linux-data/saves"}, separate{base / "linux-data-new", nested.saves};
        Put(nested.data / "settings.toml", "volume = 50\n"); Put(nested.saves / "hdd/campaign", "nested campaign");
        const auto nested_locator = base / "linux-default/paths.toml";
        Require(shaolin::paths::ScheduleLocations(nested_locator, nested, separate, error), error.c_str());
        Require(shaolin::paths::LoadLocations(nested_locator, nested, loaded, error, true) && loaded.data == separate.data && loaded.saves == nested.saves, "Moving Linux data must keep unchanged nested saves at their configured root");
        Require(Get(nested.saves / "hdd/campaign") == "nested campaign" && !fs::exists(separate.data / "saves"), "Data movement must not implicitly relocate saves");
        fs::remove_all(base);
        std::cout << "Path preferences checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; fs::remove_all(base); return 1; }
}
