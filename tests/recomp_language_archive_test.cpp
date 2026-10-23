#include "language_archive.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
using namespace shaolin::language;

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static uint32_t Get(const Bytes& data, size_t offset) {
    Check(offset + 4 <= data.size(), "fixture read bounds");
    return uint32_t(data[offset]) | uint32_t(data[offset+1]) << 8 |
           uint32_t(data[offset+2]) << 16 | uint32_t(data[offset+3]) << 24;
}
static void Put(Bytes& data, size_t offset, uint32_t value) {
    Check(offset + 4 <= data.size(), "fixture write bounds");
    for (unsigned i = 0; i != 4; ++i) data[offset+i] = uint8_t(value >> (8*i));
}
static void Write(const fs::path& path, const Bytes& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    Check(bool(out), "fixture write failed");
}
static Bytes Read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    Check(bool(input), "output file missing");
    Bytes bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    Check(bool(input), "output read failed");
    return bytes;
}
static Bytes Afs(uint8_t value) {
    Bytes data(0x2000);
    std::memcpy(data.data(), "AFS\0", 4);
    Put(data, 4, 2);
    Put(data, 8, 0x800); Put(data, 12, 100);
    Put(data, 16, 0x1000); Put(data, 20, 120);
    Put(data, 24, 0x1800); Put(data, 28, 96);
    std::fill_n(data.begin()+0x800, 100, value);
    std::fill_n(data.begin()+0x1000, 120, uint8_t(value+1));
    std::memcpy(data.data()+0x1800, "voice.adx", 10);
    std::memcpy(data.data()+0x1830, "untouched.adx", 14);
    for (size_t i = 0; i != 12; ++i) data[0x1820+i] = uint8_t(20+i);
    Put(data, 0x182c, 100); Put(data, 0x185c, 120);
    return data;
}
static Bytes Ewdf() {
    Bytes data(35);
    std::memcpy(data.data(), "EWDF", 4);
    Put(data, 4, 3); Put(data, 8, 22); Put(data, 12, 0x12345678);
    std::memcpy(data.data()+16, "x.bin", 6);
    const uint8_t deflate[] = {0x4b, 0x4c, 0x4a, 0x06, 0x00};
    std::copy(std::begin(deflate), std::end(deflate), data.begin()+22);
    Put(data, 27, 0x352441c2); Put(data, 31, 3);
    return data;
}
struct Fixture {
    fs::path root;
    std::map<std::string, Bytes> source;
    Fixture() {
        root = fs::temp_directory_path() / ("shaolin-language-archive-test-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "pack/gamedata");
        fs::create_directories(root / "pack/audio/voices");
        auto& wad = source["GAMEDATA.WAD"];
        wad.resize(0x6000);
        std::memcpy(wad.data(), "PWF ", 4);
        Put(wad, 4, uint32_t(wad.size())); Put(wad, 8, 2);
        Put(wad, 12, 6); Put(wad, 16, 0x8000); Put(wad, 20, 0x800);
        auto a = Afs(10), b = Afs(30), e = Ewdf();
        Put(wad, 32, 0x800); Put(wad, 36, uint32_t(a.size()));
        Put(wad, 40, 0x2800); Put(wad, 44, uint32_t(b.size()));
        // Native PWF packing uses exact byte sizes and decoded capacity in eight-byte units.
        Put(wad, 48, (uint32_t(e.size()) & 0x3ff) << 22 | 9);
        Put(wad, 52, 0x80004000 | uint32_t(e.size()>>10));
        Put(wad, 56, (uint32_t(e.size()) & 0x3ff) << 22 | 11);
        Put(wad, 60, 0x80004000 | uint32_t(e.size()>>10));
        Put(wad, 64, 0x5000); Put(wad, 68, 3);
        Put(wad, 72, 0x6000); Put(wad, 76, 0);
        std::copy(a.begin(), a.end(), wad.begin()+0x800);
        std::copy(b.begin(), b.end(), wad.begin()+0x2800);
        std::copy(e.begin(), e.end(), wad.begin()+0x4800);
        std::copy(e.begin(), e.end(), wad.begin()+0x5800);
        wad[0x5000] = 1; wad[0x5001] = 2; wad[0x5002] = 3;
        for (int i = 1; i != 8; ++i) source[std::string("GAMEDATA.WA")+char('D'+i)] = {};
    }
    ~Fixture() { std::error_code ignored; fs::remove_all(root, ignored); }
    SourceRead Reader() {
        return [this](std::string_view name, uint64_t offset, std::span<uint8_t> out) {
            auto it = source.find(std::string(name));
            if (it == source.end() || offset > it->second.size() || out.size() > it->second.size()-offset)
                return false;
            std::copy_n(it->second.begin()+static_cast<ptrdiff_t>(offset), out.size(), out.begin());
            return true;
        };
    }
    SourceSize Sizer() {
        return [this](std::string_view name) {
            auto it = source.find(std::string(name));
            return it == source.end() ? uint64_t(0) : uint64_t(it->second.size());
        };
    }
};

static void GrowingVoicePreservesForeignBank() {
    Fixture fixture;
    Bytes voice(5000, 0xa5);
    Write(fixture.root / "pack/audio/voices/VOICE.ADX", voice);
    Write(fixture.root / "pack/audio/voices/unbound.adx", Bytes(9, 0x11));
    auto replacement = Ewdf(); replacement[12] = 0x99;
    Write(fixture.root / "pack/gamedata/record00002.ewdf", replacement);
    const std::array<uint32_t, 1> selected{0};
    ArchiveStats stats;
    std::string error;
    const bool built = BuildArchivesForRecords(fixture.root / "pack", fixture.root / "output",
        fixture.Reader(), fixture.Sizer(), selected, stats, error);
    Check(built, error.c_str());
    const auto output = Read(fixture.root / "output/GAMEDATA.WAD");
    Check(Get(output, 12) == 6, "record IDs changed");
    const auto voice_bank = Get(output, 32), foreign_bank = Get(output, 40);
    Check(Get(output, 36) == 0x3000, "growing AFS size was not rebuilt");
    Check(Get(output, voice_bank+12) == voice.size(), "AFS member size did not change");
    Check(Get(output, voice_bank+16) == 0x2000, "following AFS member was not relocated");
    Check(std::equal(voice.begin(), voice.end(), output.begin()+voice_bank+0x800), "voice bytes differ");
    Check(std::equal(fixture.source["GAMEDATA.WAD"].begin()+0x2800,
                     fixture.source["GAMEDATA.WAD"].begin()+0x4800,
                     output.begin()+foreign_bank), "foreign AFS changed");
    const auto names = Get(output, voice_bank+24);
    Check(Get(output, voice_bank+names+44) == voice.size(), "AFS filename size not updated");
    for (size_t i = 0; i != 12; ++i)
        Check(output[voice_bank+names+32+i] == uint8_t(20+i), "AFS filename date metadata changed");
    Check(Get(output, 52) == 3, "prepared EWDF replacement was not decoded into a plain PWF resource");
    const auto replaced_at = Get(output, 48);
    Check(std::equal(output.begin()+replaced_at, output.begin()+replaced_at+3, "abc"), "decoded resource differs");
    const auto a = Get(output, 56), b = Get(output, 60);
    const uint32_t packed_offset = (a & 0x1fffff)*0x800;
    Check((b & 0x80000000) && (((a>>22)|(b<<10))&0x3fffff) == replacement.size(), "packed TOC invalid");
    const auto original_ewdf = Ewdf();
    Check(std::equal(original_ewdf.begin(), original_ewdf.end(), output.begin()+packed_offset), "unchanged EWDF bytes differ");
    Check((b & 0xfffff000) == 0x80004000, "unchanged EWDF allocation metadata differs");
    Check(Get(output, 76) == 0 && Get(output, 72) <= output.size(), "empty record points beyond the volume");
    Check(stats.resources == 1 && stats.voice_names == 1 && stats.voice_occurrences == 1, "incorrect archive statistics");
    Check(stats.unbound_voices == std::vector<std::string>{"unbound.adx"}, "unbound voice was silently lost");
    ArchiveView virtual_files;
    Check(PlanArchivesForRecords(fixture.root / "pack", fixture.Reader(), fixture.Sizer(), selected,
                                virtual_files, stats, error), error.c_str());
    Bytes streamed(output.size());
    for (size_t at = 0; at < streamed.size(); at += 137) {
        const auto amount = std::min<size_t>(137, streamed.size() - at);
        size_t got = 0;
        Check(virtual_files[0]->Read(at, std::span(streamed).subspan(at, amount), got) && got == amount,
              "Virtual archive reads failed across record boundaries");
    }
    Check(streamed == output, "Direct reads must match the verified archive layout");

}
static void InvalidSourceBoundsAreRejected() {
    Fixture fixture;
    Put(fixture.source["GAMEDATA.WAD"], 36, 0x8001);
    ArchiveStats stats; std::string error;
    const std::array<uint32_t, 1> selected{0};
    Check(!BuildArchivesForRecords(fixture.root / "pack", fixture.root / "output",
        fixture.Reader(), fixture.Sizer(), selected, stats, error), "invalid source bounds accepted");
    Check(!error.empty(), "invalid source diagnostic missing");
}
static void ExistingOutputIsPreserved() {
    Fixture fixture;
    Write(fixture.root / "output/keep.dat", Bytes{7, 8, 9});
    ArchiveStats stats; std::string error;
    const std::array<uint32_t, 1> selected{0};
    Check(!BuildArchivesForRecords(fixture.root / "pack", fixture.root / "output",
        fixture.Reader(), fixture.Sizer(), selected, stats, error), "nonempty output accepted");
    Check(Read(fixture.root / "output/keep.dat") == Bytes({7, 8, 9}), "existing output changed");
}
int main() {
    try {
        GrowingVoicePreservesForeignBank();
        InvalidSourceBoundsAreRejected();
        ExistingOutputIsPreserved();
        std::cout << "PASS: language archive rebuilding\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
