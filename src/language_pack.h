#pragma once
#include "language_archive.h"
#include <span>
#include <string>
#include <vector>

namespace shaolin::language {
struct PackStats {
    ArchiveStats archive;
    unsigned movies = 0;
};
std::filesystem::path FindInstalledPack(const std::filesystem::path& mods_directory, std::string_view language);
std::shared_ptr<const VirtualFile> PlanMovie(std::string_view source_name, const std::filesystem::path& stereo,
                const std::filesystem::path& surround, const SourceRead& read, const SourceSize& size, std::string& error);
bool ApplyFontResource(std::span<uint8_t> memory, uint32_t address, std::span<const uint8_t> original,
                       std::span<const uint8_t> replacement, std::string& error);
bool Mount(const std::filesystem::path& pack, std::span<const uint8_t> original_xbe,
           const SourceRead& read, const SourceSize& size, std::string& error);
bool MountPacks(std::span<const std::filesystem::path> packs, std::span<const uint8_t> original_xbe,
                const SourceRead& read, const SourceSize& size, bool translated_credits, std::string& error);
bool ValidateMovies(std::string& error);

void Disable();
bool CreditsEnabled();
bool ApplyFonts(std::span<uint8_t> memory, std::string& error);
std::shared_ptr<const VirtualFile> OverlayFile(const std::vector<std::string>& relative_parts);
const PackStats& Statistics();
}
