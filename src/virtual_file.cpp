#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include "virtual_file.h"
#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace shaolin::language {
void VirtualFile::Add(uint64_t at, uint64_t size, uint64_t offset, ReadAt read) {
    if (sealed_ || size > UINT64_MAX - at || size > UINT64_MAX - offset || !read)
        throw std::runtime_error("Invalid virtual file segment");
    if (size) segments_.push_back({at, size, offset, std::move(read)});
}
void VirtualFile::AddBytes(uint64_t at, std::vector<uint8_t> bytes) {
    const auto data = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    Add(at, data->size(), 0, [data](uint64_t offset, std::span<uint8_t> out) {
        if (offset > data->size() || out.size() > data->size() - offset) return false;
        std::copy_n(data->begin() + offset, out.size(), out.begin()); return true;
    });
}
void VirtualFile::SetSize(uint64_t size) {
    if (sealed_) throw std::runtime_error("A mounted virtual file is immutable");
    size_ = size;
}
void VirtualFile::Seal() {
    std::sort(segments_.begin(), segments_.end(), [](const auto& a, const auto& b) { return a.at < b.at; });
    uint64_t end = 0;
    for (const auto& piece : segments_) {
        if (piece.at < end || piece.at + piece.size > size_) throw std::runtime_error("Virtual file segments overlap or exceed EOF");
        end = piece.at + piece.size;
    }
    sealed_ = true;
}
bool VirtualFile::Read(uint64_t at, std::span<uint8_t> bytes, size_t& count) const {
    count = 0;
    if (!sealed_) return false;
    if (at >= size_ || bytes.empty()) return true;
    const auto amount = size_t(std::min<uint64_t>(bytes.size(), size_ - at));
    auto out = bytes.first(amount);
    std::fill(out.begin(), out.end(), 0);
    auto piece = std::upper_bound(segments_.begin(), segments_.end(), at,
        [](uint64_t position, const Segment& segment) { return position < segment.at; });
    if (piece != segments_.begin()) --piece;
    for (; piece != segments_.end() && piece->at < at + amount; ++piece) {
        const auto begin = std::max(at, piece->at), end = std::min(at + amount, piece->at + piece->size);
        if (begin < end && !piece->read(piece->source_offset + begin - piece->at, out.subspan(size_t(begin-at), size_t(end-begin)))) return false;
    }
    count = amount;
    return true;
}
ReadAt VirtualFile::Reader() const {
    const auto file = std::make_shared<const VirtualFile>(*this);
    return [file](uint64_t offset, std::span<uint8_t> bytes) { size_t count; return file->Read(offset, bytes, count) && count == bytes.size(); };
}
#ifndef _WIN32
ReadAt PackFileReader(const std::filesystem::path& path) {
    struct Input {
        std::filesystem::path path;
        std::mutex mutex;
        int fd = -1;
        ~Input() { if (fd >= 0) close(fd); }
    };
    auto input = std::make_shared<Input>(); input->path = path;
    return [input](uint64_t at, std::span<uint8_t> bytes) {
        std::lock_guard lock(input->mutex);
        if (input->fd < 0) input->fd = open(input->path.c_str(), O_RDONLY | O_CLOEXEC);
        if (input->fd < 0 || at > INT64_MAX) return false;
        while (!bytes.empty()) {
            const auto count = pread(input->fd, bytes.data(), std::min<size_t>(bytes.size(), 0x40000000), off_t(at));
            if (count <= 0) return false;
            bytes = bytes.subspan(size_t(count));
            at += uint64_t(count);
        }
        return true;
    };
}
#else
ReadAt PackFileReader(const std::filesystem::path& path) {
    struct Input {
        std::filesystem::path path;
        std::mutex mutex;
        HANDLE handle = INVALID_HANDLE_VALUE;
        ~Input() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    };
    auto input = std::make_shared<Input>(); input->path = path;
    return [input](uint64_t at, std::span<uint8_t> bytes) {
        std::lock_guard lock(input->mutex);
        // Voice banks can touch more files than the CRT's default 512-stream limit.
        if (input->handle == INVALID_HANDLE_VALUE)
            input->handle = CreateFileW(input->path.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (input->handle == INVALID_HANDLE_VALUE || at > INT64_MAX) return false;
        LARGE_INTEGER offset; offset.QuadPart = int64_t(at);
        if (!SetFilePointerEx(input->handle, offset, nullptr, FILE_BEGIN)) return false;
        while (!bytes.empty()) {
            const auto amount = static_cast<DWORD>(std::min<size_t>(bytes.size(), 0x40000000));
            DWORD count = 0;
            if (!ReadFile(input->handle, bytes.data(), amount, &count, nullptr) || count != amount) return false;
            bytes = bytes.subspan(count);
        }
        return true;
    };
}
#endif
}
