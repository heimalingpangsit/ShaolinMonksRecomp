#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fstream>
#include <iostream>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>
#include "game_import.h"
#include "runtime_paths.h"
#include "host.h"

namespace shaolin { Options options; }
extern const char kImageSha1[] = "unused-in-cancelled-import";
namespace fs = std::filesystem;

int main() {
    const auto root = fs::temp_directory_path() / ("shaolin-import-test-" + std::to_string(GetCurrentProcessId()));
    fs::create_directory(root);
    std::string error;
    if (!shaolin::paths::Initialize(root, true, root / "data", error)) return 2;
    fs::create_directories(root / "data/game/previous");
    std::ofstream(root / "data/game/previous/keep.dat") << "keep";
    const auto zip_path = root / "game.zip";
    mz_zip_archive zip{};
    const auto narrow = zip_path.string();
    bool ok = mz_zip_writer_init_file(&zip, narrow.c_str(), 0);
    std::vector<uint8_t> payload(1024 * 1024, 42);
    ok = ok && mz_zip_writer_add_mem(&zip, "default.xbe", payload.data(), payload.size(), MZ_BEST_SPEED);
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    if (!ok) return 2;
    shaolin::setup::Source source;
    std::vector<uint8_t> bytes;
    bool saw_data = false;
    const bool accepted = shaolin::setup::PrepareSource(zip_path, source, bytes, error,
        [&](uint64_t done, uint64_t) { if (done) saw_data = true; return done == 0; });
    ok = !accepted && saw_data && error.find("cancelled") != std::string::npos;
    ok = ok && fs::is_regular_file(root / "data/game/previous/keep.dat") && fs::is_regular_file(zip_path);
    size_t imports = 0;
    for (const auto& entry : fs::directory_iterator(root / "data/game"))
        if (entry.path().filename() != "previous") ++imports;
    ok = ok && imports == 0;
    if (!ok) std::cerr << "FAIL: cancellation must remove only the partial import and preserve the previous installation\n";
    fs::remove_all(root);
    return ok ? 0 : 1;
}
