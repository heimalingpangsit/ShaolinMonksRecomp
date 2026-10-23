#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "save_storage.h"
namespace fs = std::filesystem;
namespace storage = shaolin::storage;
static std::string Read(const fs::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void Write(const fs::path& p, const std::string& b) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << b; }
int main() {
    const auto root = fs::temp_directory_path() / (L"shaolin-migration-test-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directory(root);
    const auto legacy = root / L"old build/hdd", stable = root / L"Jogos Salvos/少林/hdd";
    const fs::path slot = "E/UDATA/4d570029/slot/save.dat";
    Write(legacy / slot, "original-campaign");
    Write(legacy / "E/UDATA/4d570029/TitleMeta.xbx", "metadata");
    int failures = 0;
    auto check = [&](bool ok, const char* m) { if (!ok) { ++failures; std::cerr << "FAIL: " << m << '\n'; } };
    std::string error;
    check(storage::PrepareDirectory(legacy, stable, error), "migration must publish a verified copy");
    check(Read(legacy / slot) == "original-campaign" && Read(stable / slot) == "original-campaign", "migration must preserve both copies byte for byte");
    check(Read(stable / "E/UDATA/4d570029/TitleMeta.xbx") == "metadata", "migration must preserve metadata");
    const auto empty_destination = root / L"precreated-empty/hdd";
    fs::create_directories(empty_destination);
    check(storage::PrepareDirectory(legacy, empty_destination, error) &&
          Read(empty_destination / slot) == "original-campaign",
          "an existing empty save directory must not hide legacy progress");
    Write(stable / slot, "newer-campaign");
    check(storage::PrepareDirectory(legacy, stable, error) && Read(stable / slot) == "newer-campaign", "repeat migration must never replace newer saves");
    const auto broken = root / "broken";
    check(!storage::PrepareDirectory(legacy / slot, broken, error) && !fs::exists(broken), "failed migration must not publish a partial destination");
    for (const auto& e : fs::directory_iterator(root)) check(e.path().filename().string().find(".migrating-") == std::string::npos, "failed migration must clean its own staging directory");
    {
        storage::DirectoryLock first, second;
        check(first.Acquire(stable, error) && !second.Acquire(stable, error), "two processes/instances must not share a save directory for writing");
    }
    { storage::DirectoryLock next; check(next.Acquire(stable, error), "save lock must release on shutdown"); }
    storage::SetDataRoot(stable);
    check(!storage::IsSave(stable / "E/TDATA/cache.dat") && !storage::IsSave(root / slot), "backups must only cover UDATA under the selected root");
    for (int i = 0; i < 5; ++i) {
        Write(stable / slot, "progress-" + std::to_string(i));
        check(storage::BeforeWrite(stable / slot) == 0, "save snapshot must succeed");
    }
    std::vector<std::string> snapshots;
    for (const auto& e : fs::recursive_directory_iterator(stable / ".backups")) if (e.is_regular_file()) snapshots.push_back(Read(e.path()));
    std::sort(snapshots.begin(), snapshots.end());
    check(snapshots == std::vector<std::string>{"progress-2", "progress-3", "progress-4"}, "backup rotation must retain the three newest complete versions");
    const auto output = root / "flush.dat";
    FILE* f = _wfopen(output.c_str(), L"w+b");
    std::fwrite("durable", 1, 7, f);
    check(storage::Flush(f) == 0 && Read(output) == "durable", "flush must persist actual buffered file bytes");
    // Invalidate the real OS handle to prove commit failures cannot be reported as success.
    CloseHandle(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(f))));
    check(storage::Flush(f) != 0, "flush must propagate a real Windows commit failure");
    std::fclose(f);
    fs::remove_all(root);
    if (!failures) std::cout << "PASS: verified migration, destination preservation, exclusive access, rotation and durable flush failures\n";
    return failures ? 1 : 0;
}
