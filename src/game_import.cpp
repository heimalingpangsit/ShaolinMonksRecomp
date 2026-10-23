#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "platform_compat.h"
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>
#include "game_import.h"
#include "runtime_paths.h"
#include "kernel/disc.h"
#include "kernel/crypto.h"

extern const char kImageSha1[];
namespace fs = std::filesystem;
namespace shaolin::setup {
namespace {
std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return std::towlower(c); });
    return s;
}
// An Xbox path (backslashes, any case) under a host folder. Linux file
// systems are case-sensitive, so each component is matched like Windows does.
fs::path HostPath(const fs::path& root, std::string_view name) {
#ifdef _WIN32
    return root / std::string(name);
#else
    fs::path current = root;
    size_t start = 0;
    while (start <= name.size()) {
        const size_t end = std::min(name.find('\\', start), name.size());
        const std::string part(name.substr(start, end - start));
        start = end + 1;
        if (part.empty()) continue;
        fs::path next = current / part;
        std::error_code error;
        if (!fs::exists(next, error)) {
            const auto wanted = Lower(fs::path(part).wstring());
            for (fs::directory_iterator it(current, error), last; !error && it != last; it.increment(error))
                if (Lower(it->path().filename().wstring()) == wanted) { next = it->path(); break; }
        }
        current = next;
        if (end == name.size()) break;
    }
    return current;
#endif
}
bool Image(const fs::path& path) {
    const auto extension = Lower(path.extension().wstring());
    return extension == L".iso" || extension == L".xiso";
}
void VerifyRequiredFiles(const Source& source, const kernel::Disc& disc) {
    struct RequiredFile { const char* name; uint32_t minimum = 4; std::string_view signature = {}; };
    constexpr std::string_view movie_header{"\0\0\1\xba", 4};
    static constexpr RequiredFile files[] = {
        {"WADCRC.BIN"}, {"GAMEDATA.WAD", 32, "PWF "},
        {"GAMEDATA.WAE"}, {"GAMEDATA.WAF"}, {"GAMEDATA.WAG"}, {"GAMEDATA.WAH"},
        {"GAMEDATA.WAI"}, {"GAMEDATA.WAJ"}, {"GAMEDATA.WAK"},
        {"FRONT\\MOVIES\\MIDWAY.SFD", 4, movie_header},
        {"FRONT\\MOVIES\\OPENING.SFD", 4, movie_header},
        {"FRONT\\MOVIES\\END006.SFD", 4, movie_header},
        {"FRONT\\MOVIES\\END011.SFD", 4, movie_header},
        {"FRONT\\MOVIES\\FINAL.SFD", 4, movie_header}
    };
    const uint64_t image_size = source.folder ? 0 : fs::file_size(source.path);
    for (const auto& file : files) {
        const auto fail = [&](const char* reason) {
            throw std::runtime_error(std::string(reason) + file.name + ". Select a complete, readable copy of the game.");
        };
        uint8_t header[4]{}, tail[4]{};
        if (source.folder) {
            const auto path = HostPath(source.path, file.name);
            std::error_code error;
            if (!fs::is_regular_file(path, error)) {
                if (error && error != std::errc::no_such_file_or_directory) fail("Cannot read required game file: ");
                fail("Required game file is missing: ");
            }
            const auto size = fs::file_size(path, error);
            if (error) fail("Cannot read required game file: ");
            if (size < file.minimum) fail("Required game file is incomplete: ");
            std::ifstream stream(path, std::ios::binary);
            stream.read(reinterpret_cast<char*>(header), sizeof(header));
            stream.seekg(std::streamoff(size - sizeof(tail)));
            stream.read(reinterpret_cast<char*>(tail), sizeof(tail));
            if (!stream) fail("Cannot read required game file: ");
        } else {
            const auto* entry = disc.Find(file.name);
            if (!entry || entry->dir) fail("Required game file is missing: ");
            if (entry->size < file.minimum || entry->offset > image_size || entry->size > image_size - entry->offset)
                fail("Required game file is incomplete: ");
            if (disc.Read(*entry, 0, header, sizeof(header)) != sizeof(header) ||
                disc.Read(*entry, entry->size - sizeof(tail), tail, sizeof(tail)) != sizeof(tail))
                fail("Cannot read required game file: ");
        }
        if (!file.signature.empty() && std::memcmp(header, file.signature.data(), file.signature.size()) != 0)
            fail("Required game file has an invalid header: ");
    }
}
void Verify(const Source& source, std::vector<uint8_t>& bytes) {
    kernel::Disc disc;
    if (source.folder) {
        const auto file = source.path / "default.xbe";
        if (!fs::is_regular_file(file) || fs::file_size(file) > 64 * 1024 * 1024)
            throw std::runtime_error("Select default.xbe inside the complete game folder.");
        std::ifstream stream(file, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(stream), {});
    } else {
        std::string error;
        if (!disc.Open(source.path, error))
            throw std::runtime_error("Cannot read an Xbox game from this image. " + error);
        const auto* entry = disc.Find("default.xbe");
        if (!entry || entry->dir || entry->size > 64 * 1024 * 1024 || !disc.Load("default.xbe", bytes))
            throw std::runtime_error("Cannot read default.xbe from this Xbox disc image.");
    }
    alignas(8) uint8_t context[128], digest[20];
    crypto::ShaInit(context);
    crypto::ShaUpdate(context, bytes.data(), bytes.size());
    crypto::ShaFinal(context, digest);
    std::string hash;
    for (uint8_t byte : digest) { hash += "0123456789abcdef"[byte >> 4]; hash += "0123456789abcdef"[byte & 15]; }
    if (hash != kImageSha1)
        throw std::runtime_error("This game version is not supported. Use the original USA/Europe Xbox release of Mortal Kombat: Shaolin Monks.");
    VerifyRequiredFiles(source, disc);
}
fs::path ArchivePath(const std::string& name) {
    if (name.empty() || name.size() > 1024 || name.find('\0') != std::string::npos)
        throw std::runtime_error("The ZIP contains an invalid file name.");
    std::string normalized = name;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.front() == '/') throw std::runtime_error("The ZIP contains an absolute path.");
    fs::path relative;
    size_t begin = 0;
    while (begin < normalized.size()) {
        const auto end = normalized.find('/', begin);
        const auto part = normalized.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ' ||
            part.find_first_of(":*?\"<>|") != std::string::npos ||
            std::any_of(part.begin(), part.end(), [](unsigned char c) { return c < 32; }))
            throw std::runtime_error("The ZIP contains an unsafe path.");
        fs::path component(std::u8string(part.begin(), part.end()));
        auto base = Lower(component.wstring());
        base.resize(base.find(L'.') == std::wstring::npos ? base.size() : base.find(L'.'));
        while (!base.empty() && base.back() == L' ') base.pop_back();
        if (base == L"con" || base == L"prn" || base == L"aux" || base == L"nul" ||
            (base.size() == 4 && (base.starts_with(L"com") || base.starts_with(L"lpt")) && base[3] >= L'0' && base[3] <= L'9'))
            throw std::runtime_error("The ZIP contains a reserved Windows file name.");
        relative /= component;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return relative;
}
struct Zip {
    mz_zip_archive reader{};
    FILE* file = nullptr;
    bool open = false;
    ~Zip() { if (open) mz_zip_reader_end(&reader); if (file) std::fclose(file); }
};
struct Output {
    std::ofstream file;
    const Progress& progress;
    uint64_t done, total, written = 0;
    bool cancelled = false;
};
size_t Extract(void* opaque, mz_uint64 offset, const void* data, size_t size) {
    auto& output = *static_cast<Output*>(opaque);
    if (offset != output.written) return 0;
    output.file.write(static_cast<const char*>(data), size);
    if (!output.file) return 0;
    output.written += size;
    if (output.progress && !output.progress(output.done + output.written, output.total)) {
        output.cancelled = true;
        return 0;
    }
    return size;
}
Source Import(const fs::path& input, fs::path& staging, const Progress& progress) {
    Zip zip;
#ifdef _WIN32
    zip.file = _wfopen(input.c_str(), L"rb");
#else
    zip.file = std::fopen(input.c_str(), "rb");
#endif
    if (!zip.file || !mz_zip_reader_init_cfile(&zip.reader, zip.file, 0, 0))
        throw std::runtime_error("Cannot open this ZIP. Use a ZIP without encryption or select the extracted game files.");
    zip.open = true;
    const auto count = mz_zip_reader_get_num_files(&zip.reader);
    if (!count || count > 100000) throw std::runtime_error("The ZIP contains too many files or no files.");
    struct Entry { fs::path path; mz_zip_archive_file_stat info; };
    std::vector<Entry> entries;
    std::set<std::wstring> names;
    uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat info{};
        if (!mz_zip_reader_file_stat(&zip.reader, i, &info) || !mz_zip_reader_is_file_supported(&zip.reader, i))
            throw std::runtime_error("The ZIP uses encryption or an unsupported compression method.");
        if (((info.m_external_attr >> 16) & 0170000) == 0120000)
            throw std::runtime_error("The ZIP contains a symbolic link.");
        const auto length = mz_zip_reader_get_filename(&zip.reader, i, nullptr, 0);
        std::string name(length, '\0');
        if (!length || mz_zip_reader_get_filename(&zip.reader, i, name.data(), length) != length)
            throw std::runtime_error("Cannot read a ZIP file name.");
        name.pop_back();
        auto relative = ArchivePath(name);
        if (!names.insert(Lower(relative.wstring())).second)
            throw std::runtime_error("The ZIP contains duplicate file names.");
        total += info.m_uncomp_size;
        if (info.m_uncomp_size > (32ull << 30) || total > (64ull << 30))
            throw std::runtime_error("The ZIP exceeds the game import size limit.");
        entries.push_back({relative, info});
    }
    fs::create_directories(paths::Root() / "game");
    const auto space = fs::space(paths::Root() / "game");
    if (space.available < total + (64ull << 20)) throw std::runtime_error("There is not enough free space to import this ZIP.");
    const auto id = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto temporary = paths::Root() / "game" / (L"import-" + id + L".partial");
    if (!fs::create_directory(temporary)) throw std::runtime_error("Cannot create the game import directory.");
    staging = temporary;
    uint64_t done = 0;
    for (const auto& entry : entries) {
        if (progress && !progress(done, total)) throw std::runtime_error("Game import cancelled.");
        const auto destination = staging / entry.path;
        if (entry.info.m_is_directory) { fs::create_directories(destination); continue; }
        fs::create_directories(destination.parent_path());
        if (fs::exists(destination)) throw std::runtime_error("The ZIP contains conflicting file names.");
        Output output{std::ofstream(destination, std::ios::binary), progress, done, total};
        if (!output.file || !mz_zip_reader_extract_to_callback(&zip.reader, entry.info.m_file_index, Extract, &output, 0))
            throw std::runtime_error(output.cancelled ? "Game import cancelled." : "Cannot extract the ZIP. The file may be corrupt, or the disk may be full.");
        output.file.flush();
        if (!output.file) throw std::runtime_error("Cannot write the imported game files.");
        done += entry.info.m_uncomp_size;
    }
    std::vector<Source> candidates;
    for (const auto& entry : entries) {
        if (entry.info.m_is_directory) continue;
        if (Lower(entry.path.filename().wstring()) == L"default.xbe") candidates.push_back({staging / entry.path.parent_path(), true});
        else if (Image(entry.path)) candidates.push_back({staging / entry.path, false});
    }
    if (candidates.size() != 1) throw std::runtime_error("The ZIP must contain one game image or one complete game folder.");
    return candidates.front();
}
}
bool PrepareSource(const fs::path& input, Source& source, std::vector<uint8_t>& executable,
                   std::string& error, const Progress& progress) {
    fs::path staging;
    try {
        const auto path = fs::absolute(input);
        Source selected;
        if (Lower(path.extension().wstring()) == L".zip") selected = Import(path, staging, progress);
        else if (fs::is_directory(path)) selected = {path, true};
        else if (Lower(path.extension().wstring()) == L".xbe") {
            if (Lower(path.filename().wstring()) != L"default.xbe" || !fs::is_regular_file(path))
                throw std::runtime_error("Select default.xbe inside the complete game folder.");
            selected = {path.parent_path(), true};
        }
        else selected = {path, false};
        Verify(selected, executable);
        if (progress && !progress(1, 1)) throw std::runtime_error("Game import cancelled.");
        if (!staging.empty()) {
            const auto relative = fs::relative(selected.path, staging);
            auto installed = staging;
            installed.replace_extension();
            fs::rename(staging, installed);
            selected.path = installed / relative;
            staging.clear();
        }
        source = std::move(selected);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        if (!staging.empty()) { std::error_code ignored; fs::remove_all(staging, ignored); }
        return false;
    }
}
}
