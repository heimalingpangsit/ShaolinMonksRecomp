#include "file_ops.h"
#include "save_storage.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace fs = std::filesystem;
namespace shaolin::files {
std::string Utf8(const fs::path& path) { const auto s = path.u8string(); return {s.begin(), s.end()}; }
fs::path FromUtf8(std::string_view path) { return fs::path(std::u8string(path.begin(), path.end())); }
fs::path UniqueSibling(const fs::path& path, std::string_view suffix) {
    static std::atomic<unsigned> sequence{0};
    return path.parent_path() / (path.filename().string() + std::string(suffix) +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++));
}
void AtomicWrite(const fs::path& path, std::string_view contents) {
    fs::create_directories(path.parent_path());
    const auto temp = UniqueSibling(path, ".writing-");
    try {
        FILE* file = nullptr;
#ifdef _WIN32
        file = _wfopen(temp.c_str(), L"wb");
#else
        file = std::fopen(temp.c_str(), "wb");
#endif
        if (!file) throw std::runtime_error("Cannot create " + Utf8(temp));
        const bool written = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
        const auto flushed = storage::Flush(file);
        const auto closed = std::fclose(file);
        if (!written || flushed || closed) throw std::runtime_error("Cannot flush " + Utf8(temp) + "; check free space and permissions");
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace " + Utf8(path) + " (Windows error " + std::to_string(GetLastError()) + ")");
#else
        fs::rename(temp, path);
#endif
    } catch (...) { std::error_code ec; fs::remove(temp, ec); throw; }
}
bool Within(const fs::path& child, const fs::path& parent) {
    auto a = fs::weakly_canonical(child), b = fs::weakly_canonical(parent);
#ifdef _WIN32
    auto lower = [](fs::path p) { auto s = p.wstring(); for (auto& c : s) c = wchar_t(std::towlower(c)); return fs::path(s); };
    a = lower(a); b = lower(b);
#endif
    auto i = a.begin();
    for (auto j = b.begin(); j != b.end(); ++j, ++i) if (i == a.end() || *i != *j) return false;
    return true;
}
bool SafeRelative(std::string_view path) {
    if (path.empty() || path.size() > 220 || path.front() == '/' || path.back() == '/') return false;
    size_t start = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == path.npos ? path.size() - start : end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        for (unsigned char c : part) if (c < 32 || c == ':' || c == '\\' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
        std::string stem(part.substr(0, part.find('.')));
        for (auto& c : stem) if (c >= 'a' && c <= 'z') c -= 32;
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
            (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '0' && stem[3] <= '9')) return false;
        if (end == path.npos) break;
        start = end + 1;
    }
    return true;
}
bool SameBytes(const fs::path& a, const fs::path& b) {
    std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
    char xb[65536], yb[65536];
    do { x.read(xb, sizeof xb); y.read(yb, sizeof yb);
        if (x.gcount() != y.gcount() || !std::equal(xb, xb + x.gcount(), yb)) return false;
    } while (x && y);
    return x.eof() && y.eof();
}
void FlushFile(const fs::path& path) {
#ifdef _WIN32
    FILE* file = _wfopen(path.c_str(), L"rb+");
#else
    FILE* file = std::fopen(path.c_str(), "rb+");
#endif
    if (!file) throw std::runtime_error("Cannot open copied file for flushing: " + Utf8(path));
    const auto status = storage::Flush(file); const auto closed = std::fclose(file);
    if (status || closed) throw std::runtime_error("Cannot flush copied file; check disk space and permissions: " + Utf8(path));
}
}
