#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace shaolin::language {
using ReadAt = std::function<bool(uint64_t, std::span<uint8_t>)>;

// Immutable after Seal. Gaps read as zeroes; sources remain owned by their readers.
class VirtualFile {
    struct Segment { uint64_t at, size, source_offset; ReadAt read; };
    std::vector<Segment> segments_;
    uint64_t size_ = 0;
    bool sealed_ = false;
public:
    void Add(uint64_t at, uint64_t size, uint64_t source_offset, ReadAt read);
    void AddBytes(uint64_t at, std::vector<uint8_t> bytes);
    void SetSize(uint64_t size);
    void Seal();
    uint64_t Size() const { return size_; }
    bool Read(uint64_t at, std::span<uint8_t> bytes, size_t& count) const;
    ReadAt Reader() const;
};
ReadAt PackFileReader(const std::filesystem::path& path);
}
