#pragma once
#include <cstdio>
#include <filesystem>
#include <string>
#include <cstdint>

namespace shaolin::storage {
// Preserve the console's serialization. These services only handle host storage.
inline constexpr uint32_t kIoError = 0xC0000185, kDiskFull = 0xC000007F;
uint32_t Flush(FILE* file);
void SetDataRoot(const std::filesystem::path& root);
bool IsSave(const std::filesystem::path& file);
uint32_t BeforeWrite(const std::filesystem::path& file);
bool PrepareDirectory(const std::filesystem::path& legacy, const std::filesystem::path& destination, std::string& error);
std::filesystem::path SavedGamesRoot(std::string& error, bool create = true);
class DirectoryLock {
    void* handle_ = nullptr;
public:
    DirectoryLock() = default;
    DirectoryLock(const DirectoryLock&) = delete;
    DirectoryLock& operator=(const DirectoryLock&) = delete;
    ~DirectoryLock();
    bool Acquire(const std::filesystem::path& root, std::string& error);
};
}
