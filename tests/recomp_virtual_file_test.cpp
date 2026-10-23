#include "virtual_file.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <fstream>

int main() {
    using namespace shaolin::language;
    unsigned failures = 0;
    const auto check = [&](bool pass, const char* message) { if (!pass) { ++failures; std::cerr << message << '\n'; } };
    VirtualFile file;
    file.AddBytes(8, {'m', 'o', 'd'});
    file.AddBytes(0, {'b', 'a', 's', 'e'});
    file.SetSize(13);
    file.Seal();
    const std::array<uint8_t, 13> expected{'b','a','s','e',0,0,0,0,'m','o','d',0,0};
    for (unsigned start = 0; start <= 15; ++start) {
        std::array<uint8_t, 6> bytes; bytes.fill(0xCC);
        size_t count = 99;
        check(file.Read(start, bytes, count), "A valid virtual read must succeed");
        const auto wanted = start >= expected.size() ? 0 : std::min(bytes.size(), expected.size() - start);
        check(count == wanted, "Virtual reads must stop at EOF");
        for (size_t i = 0; i < wanted; ++i) check(bytes[i] == expected[start + i], "Reads crossing segments or padding differ");
        for (size_t i = wanted; i < bytes.size(); ++i) check(bytes[i] == 0xCC, "A short read must preserve the rest of the buffer");
    }
    VirtualFile invalid;
    invalid.AddBytes(1, {1,2,3}); invalid.AddBytes(3, {4}); invalid.SetSize(8);
    try { invalid.Seal(); check(false, "Overlapping segments must fail"); } catch (const std::exception&) {}
    VirtualFile broken;
    broken.Add(0, 4, 0, [](uint64_t, std::span<uint8_t>) { return false; });
    broken.SetSize(4); broken.Seal();
    std::array<uint8_t, 4> bytes{}; size_t count = 99;
    check(!broken.Read(0, bytes, count) && count == 0, "Source failures must not report a successful transfer");
    const auto path = std::filesystem::temp_directory_path() /
        ("shaolin-reader-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    { std::ofstream stream(path, std::ios::binary); stream << "voice"; }
    std::vector<ReadAt> voices;
    bool all_read = true;
    for (unsigned i = 0; i < 700; ++i) {
        voices.push_back(PackFileReader(path));
        std::array<uint8_t, 5> data{};
        all_read &= voices.back()(0, data) && data == std::array<uint8_t, 5>{'v','o','i','c','e'};
    }
    check(all_read, "Hundreds of voice readers must not exhaust the CRT stream limit");
    voices.clear(); std::filesystem::remove(path);
    return failures ? 1 : 0;
}
