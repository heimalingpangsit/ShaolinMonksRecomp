#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include "virtual_file.h"

namespace shaolin::language {
using SourceRead = std::function<bool(std::string_view name, uint64_t offset, std::span<uint8_t> output)>;
using SourceSize = std::function<uint64_t(std::string_view name)>;
struct ArchiveStats {
    unsigned resources = 0;
    unsigned voice_names = 0;
    unsigned voice_occurrences = 0;
    std::vector<std::string> unbound_voices;
};
using ArchiveView = std::array<std::shared_ptr<const VirtualFile>, 8>;
bool PlanArchivesForPacks(std::span<const std::filesystem::path> packs, const SourceRead& read, const SourceSize& size,
                         ArchiveView& files, ArchiveStats& stats, std::string& error);
bool PlanArchivesForPacks(std::span<const std::filesystem::path> packs, const SourceRead& read, const SourceSize& size,
                         std::span<const uint32_t> selected_records, ArchiveView& files, ArchiveStats& stats, std::string& error);
bool PlanArchives(const std::filesystem::path& pack, const SourceRead& read, const SourceSize& size,
                  ArchiveView& files, ArchiveStats& stats, std::string& error);
bool PlanArchivesForRecords(const std::filesystem::path& pack, const SourceRead& read, const SourceSize& size,
                            std::span<const uint32_t> selected_records, ArchiveView& files,
                            ArchiveStats& stats, std::string& error);
bool BuildArchives(const std::filesystem::path& pack, const std::filesystem::path& output,
                   const SourceRead& read, const SourceSize& size, ArchiveStats& stats, std::string& error);
bool BuildArchivesForRecords(const std::filesystem::path& pack, const std::filesystem::path& output,
                             const SourceRead& read, const SourceSize& size,
                             std::span<const uint32_t> selected_records, ArchiveStats& stats, std::string& error);
}
