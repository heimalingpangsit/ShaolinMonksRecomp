#pragma once
#include <filesystem>
#include <string>
#include <string_view>
namespace shaolin::files {
std::string Utf8(const std::filesystem::path& path);
std::filesystem::path FromUtf8(std::string_view path);
std::filesystem::path UniqueSibling(const std::filesystem::path& path, std::string_view suffix);
void AtomicWrite(const std::filesystem::path& path, std::string_view contents);
bool Within(const std::filesystem::path& child, const std::filesystem::path& parent);
bool SafeRelative(std::string_view path);
bool SameBytes(const std::filesystem::path& a, const std::filesystem::path& b);
void FlushFile(const std::filesystem::path& path);
}
