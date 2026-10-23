// Exercise the production kernel file exports with real temporary Windows files.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <windows.h>
#include "kernel/kernel.h"
#include "host.h"
#include "runtime_paths.h"
#include SHAOLIN_FILES_TEST_SOURCE
namespace shaolin { Options options; }

namespace fs = std::filesystem;
namespace shaolin::language {
// The archive builder is covered separately; this fixture controls the DVD lookup boundary.
static std::shared_ptr<const VirtualFile> overlay;
std::shared_ptr<const VirtualFile> OverlayFile(const std::vector<std::string>& parts) {
    return parts.size() == 1 && parts[0] == "asset.bin" ? overlay : nullptr;
}
}
namespace kernel {
uint8_t* g_base = nullptr;
static std::map<uint32_t, ObjectRef>& Handles() { static std::map<uint32_t, ObjectRef> h; return h; }
static std::map<uint32_t, KernelFn>& Exports() { static std::map<uint32_t, KernelFn> e; return e; }
void RegisterExport(uint32_t n, const char*, KernelFn f) { Exports()[n] = f; }
uint32_t NewHandle(ObjectRef o) { static uint32_t next = 0x1000; Handles()[next] = std::move(o); return std::exchange(next, next + 4); }
ObjectRef FromHandle(uint32_t h) { auto i = Handles().find(h); return i == Handles().end() ? nullptr : i->second; }
bool CloseHandle(uint32_t h) { return Handles().erase(h) != 0; }
int32_t SetEvent(Object& o) { o.signal = 1; return 0; }
int32_t ResetEvent(Object& o) { o.signal = 0; return 0; }
Thread* Current() { return nullptr; }
void QueueApc(Thread&, uint32_t, uint32_t, uint32_t, uint32_t) {}
}

static uint32_t Call(uint32_t ordinal, std::initializer_list<uint32_t> args) {
    X86Context ctx{}; ctx.esp = 0x100;
    unsigned i = 0;
    for (auto a : args) kernel::W32(ctx.esp + 4 + 4 * i++, a);
    kernel::Exports().at(ordinal)(ctx, kernel::g_base);
    return ctx.eax;
}
static uint32_t Open(const std::string& name, uint32_t disposition = 1, uint32_t options = 0x40) {
    std::memcpy(kernel::HostPtr(0x500), name.data(), name.size());
    kernel::W16(0x400, uint16_t(name.size())); kernel::W32(0x404, 0x500);
    kernel::W32(0x300, 0); kernel::W32(0x304, 0x400); kernel::W32(0x200, 0);
    uint32_t s = Call(190, {0x200, 0xC0000000, 0x300, 0x250, 0, 0, 0, disposition, options});
    return s ? 0 : kernel::R32(0x200);
}
static std::string Read(const fs::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void Write(const fs::path& p, const std::string& data) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << data; }

int main() {
    std::vector<uint8_t> memory(65536); kernel::g_base = memory.data();
    const auto root = fs::temp_directory_path() / ("shaolin-files-test-" + std::to_string(GetCurrentProcessId()));
    fs::create_directory(root);
    std::string path_error;
    if (!shaolin::paths::Initialize(root, true, root / "app-data", path_error)) return 2;
    const auto data = root / "hdd", save = data / "E/UDATA/4d570029/slot/save.dat";
    Write(save, "old-progress");
    kernel::MountDefaults((root / "dvd").string(), data.string());
    kernel::CreateSymbolicLink("\\??\\U:", "\\Device\\Harddisk0\\Partition1\\UDATA\\4d570029");
    int failures = 0;
    auto check = [&](bool pass, const char* message) { if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; } };
    check(fs::is_directory(root / "app-data/cache/xbox/X") && !fs::exists(data / "X"),
          "temporary Xbox cache partitions must use AppData instead of the save directory");
    check(Call(198, {0xDEADBEEF, 0x250}) == kernel::kInvalidHandle && kernel::R32(0x250) == kernel::kInvalidHandle,
          "flush must reject an invalid handle and populate IO status");
    const auto dir = Open("U:\\", 1, 1);
    const auto expected = fs::space(save.parent_path());
    check(Call(218, {dir, 0x250, 0x800, 0x20, 7}) == 0 &&
          kernel::R64(0x800) == expected.capacity / 16384 &&
          kernel::R64(0x808) > expected.available / 16384 - 1024 &&
          kernel::R64(0x808) < expected.available / 16384 + 1024,
          "volume query must report real capacity and available space");
    check(Call(218, {dir, 0x250, 0x800, 4, 3}) == kernel::kBufferTooSmall,
          "volume query must reject a short output buffer");
    check(Call(218, {0xDEADBEEF, 0x250, 0x800, 0x18, 3}) == kernel::kInvalidHandle,
          "volume query must reject an invalid handle");
    kernel::CloseHandle(dir);
    const auto dvd_file = root / "dvd/asset.bin";
    Write(dvd_file, "original-asset");
    const auto media = Open("D:\\asset.bin");
    check(media && Call(219, {media, 0, 0, 0, 0x250, 0x1000, 14, 0}) == 0,
          "extracted DVD files must remain readable");
    check(media && Call(236, {media, 0, 0, 0, 0x250, 0x1000, 14, 0}) == kernel::kAccessDenied,
          "extracted DVD handles must reject writes");
    kernel::W64(0x1800, 1);
    check(Call(226, {media, 0x250, 0x1800, 8, 20}) == kernel::kAccessDenied,
          "extracted DVD files must reject truncation");
    kernel::CloseHandle(media);
    check(!Open("D:\\asset.bin", 5) && !Open("D:\\asset.bin", 1, 0x1040),
          "DVD opens must reject overwrite and delete-on-close");
    const auto delete_probe = Open("D:\\asset.bin");
    if (delete_probe) kernel::CloseHandle(delete_probe);
    check(Call(195, {0x300}) == kernel::kAccessDenied && Read(dvd_file) == "original-asset",
          "DVD delete must preserve the source");
    Write(root / "outside.bin", "private");
    check(!Open("D:\\..\\outside.bin"), "DVD paths must not escape the selected game directory");
    const auto dvd_directory = Open("D:\\", 1, 1);
    kernel::G<uint8_t>(0x1800) = 1;
    check(dvd_directory && Call(226, {dvd_directory, 0x250, 0x1800, 1, 13}) == kernel::kAccessDenied,
          "extracted DVD directories must reject deletion");
    kernel::CloseHandle(dvd_directory);
    const auto translated_file = root / "language-cache/asset.bin";
    Write(translated_file, "translated-asset-with-a-different-size");
    const auto translated_size = static_cast<uint32_t>(fs::file_size(translated_file));
    auto virtual_file = std::make_shared<shaolin::language::VirtualFile>();
    virtual_file->Add(0, translated_size, 0, shaolin::language::PackFileReader(translated_file));
    virtual_file->SetSize(translated_size); virtual_file->Seal();
    shaolin::language::overlay = virtual_file;
    const auto translated = Open("D:\\asset.bin");
    check(translated && Call(219, {translated, 0, 0, 0, 0x250, 0x1000, translated_size, 0}) == 0 &&
          std::string(reinterpret_cast<char*>(kernel::HostPtr(0x1000)), translated_size) == Read(translated_file),
          "DVD reads must use the virtual language file");
    check(translated && Call(211, {translated, 0x250, 0x1000, 0x18, 5}) == 0 && kernel::R64(0x1008) == translated_size,
          "Opened DVD file metadata must describe the translated file");
    check(translated && Call(236, {translated, 0, 0, 0, 0x250, 0x1000, 1, 0}) == kernel::kAccessDenied,
          "Virtual language files must remain read-only");
    check(Call(210, {0x300, 0x1000}) == 0 && kernel::R64(0x1028) == translated_size,
          "Path metadata must report the virtual file size");
    check(Call(219, {translated, 0, 0, 0, 0x250, 0x1000, 1, 0}) == kernel::kEndOfFile,
          "Virtual reads at EOF must return the normal guest EOF status");
    kernel::W64(0x1800, 7);
    check(Call(226, {translated, 0x250, 0x1800, 8, 14}) == 0 &&
          Call(219, {translated, 0, 0, 0, 0x250, 0x1000, 10, 0}) == 0 &&
          std::string(reinterpret_cast<char*>(kernel::HostPtr(0x1000)), 10) == Read(translated_file).substr(7, 10),
          "Seeking a virtual handle must change subsequent implicit-offset reads");
    std::vector<uint8_t> whole;
    check(kernel::ReadGameFile("D:\\asset.bin", whole) &&
          std::string(whole.begin(), whole.end()) == Read(translated_file),
          "Whole-file reads must use the virtual source");
    kernel::CloseHandle(translated);
    const auto translated_directory = Open("D:\\", 1, 1);
    check(translated_directory && Call(207, {translated_directory, 0, 0, 0, 0x250, 0x1000, 0x100, 1, 0, 1}) == 0 &&
          kernel::R64(0x1028) == translated_size,
          "DVD directory metadata must report the translated file size");
    kernel::CloseHandle(translated_directory);
    std::string image(35 * 2048, '\0');
    const auto put32 = [&](size_t at, uint32_t value) { std::memcpy(image.data() + at, &value, 4); };
    std::memcpy(image.data() + 32*2048, "MICROSOFT*XBOX*MEDIA", 20);
    std::memcpy(image.data() + 32*2048 + 0x7EC, "MICROSOFT*XBOX*MEDIA", 20);
    put32(32*2048 + 20, 33); put32(32*2048 + 24, 2048);
    put32(33*2048 + 4, 34); put32(33*2048 + 8, 14);
    image[33*2048 + 12] = 0x20; image[33*2048 + 13] = 9;
    std::memcpy(image.data() + 33*2048 + 14, "asset.bin", 9);
    std::memcpy(image.data() + 34*2048, "original-asset", 14);
    Write(root / "fixture.iso", image);
    auto disc = std::make_unique<kernel::Disc>();
    check(disc->Open(root / "fixture.iso", path_error), "The synthetic Xbox disc must open");
    kernel::MountDisc(std::move(disc));
    const auto disc_directory = Open("D:\\", 1, 1);
    check(disc_directory && Call(207, {disc_directory, 0, 0, 0, 0x250, 0x1000, 0x100, 1, 0, 1}) == 0 &&
          kernel::R64(0x1028) == translated_size,
          "Mounted-disc directory metadata must report the translated file size");
    kernel::CloseHandle(disc_directory);
    kernel::MountDisc(nullptr);
    auto scattered = std::make_shared<shaolin::language::VirtualFile>();
    std::vector<uint8_t> pattern(8192);
    for (size_t i = 0; i < pattern.size(); ++i) pattern[i] = uint8_t(i % 251);
    scattered->AddBytes(0, {pattern.begin(), pattern.begin() + 4090});
    scattered->AddBytes(4090, {pattern.begin() + 4090, pattern.end()});
    scattered->SetSize(pattern.size()); scattered->Seal();
    shaolin::language::overlay = scattered;
    const auto scattered_handle = Open("D:\\asset.bin");
    kernel::W32(0x1800, 0x4000); kernel::W32(0x1808, 0x6000); kernel::W64(0x1A00, 0);
    check(scattered_handle && Call(220, {scattered_handle, 0, 0, 0, 0x250, 0x1800, 8192, 0x1A00}) == 0 &&
          kernel::R32(0x254) == 8192 &&
          std::equal(pattern.begin(), pattern.begin() + 4096, static_cast<uint8_t*>(kernel::HostPtr(0x4000))) &&
          std::equal(pattern.begin() + 4096, pattern.end(), static_cast<uint8_t*>(kernel::HostPtr(0x6000))),
          "Scatter reads must cross virtual segments and guest pages without changing bytes");
    kernel::CloseHandle(scattered_handle);
    shaolin::language::overlay.reset();
    virtual_file.reset();
    check(Read(dvd_file) == "original-asset", "Virtual language files must preserve original DVD bytes");
    const auto file = Open("U:\\slot\\save.dat", 5);
    std::memcpy(kernel::HostPtr(0x1000), "new-progress", 12);
    check(file && Call(236, {file, 0, 0, 0, 0x250, 0x1000, 12, 0}) == 0 && Call(198, {file, 0x250}) == 0,
          "save write and durable flush must succeed");
    kernel::CloseHandle(file);
    check(Read(save) == "new-progress", "save bytes must survive close and reopen");
    bool old_saved = false;
    if (fs::exists(data / ".backups")) for (auto& e : fs::recursive_directory_iterator(data / ".backups"))
        if (e.is_regular_file() && Read(e.path()) == "old-progress") old_saved = true;
    check(old_saved, "backup must contain the old save before a truncating open");
    // The production code must refuse destruction when it cannot create its backup.
    const auto blocked = root / "blocked", blocked_save = blocked / "E/UDATA/4d570029/slot/save.dat";
    Write(blocked_save, "keep-me"); Write(blocked / ".backups", "blocked-by-file");
    kernel::MountDefaults((root / "dvd").string(), blocked.string());
    const auto denied = Open("U:\\slot\\save.dat", 5);
    check(!denied && Read(blocked_save) == "keep-me", "failed backup must preserve the original save and reject overwrite");
    if (denied) kernel::CloseHandle(denied);
    const auto deleted = Call(195, {0x300}); // attributes still name the blocked save
    check(deleted != 0 && Read(blocked_save) == "keep-me", "failed backup must also reject deletion of a save");
    fs::remove_all(root);
    return failures ? 1 : 0;
}
