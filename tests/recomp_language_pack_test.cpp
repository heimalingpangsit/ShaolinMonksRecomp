#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include "platform_compat.h"
#include "language_pack.h"

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
static void Put32(Bytes& b, size_t p, uint32_t n) { for (int i = 0; i < 4; ++i) b[p + i] = uint8_t(n >> (i * 8)); }
static void Write(const fs::path& p, const Bytes& b) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()), b.size());
}
static Bytes Read(const fs::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void Packet(Bytes& b, uint8_t id, const Bytes& body) {
    b.insert(b.end(), {0, 0, 1, id, uint8_t((body.size() + 1) >> 8), uint8_t(body.size() + 1), 0x0F});
    b.insert(b.end(), body.begin(), body.end());
}

static int VerifyReal(const fs::path& game, const fs::path& pack, const fs::path& reference) {
    using namespace shaolin::language;
    std::map<std::string, ReadAt> readers;
    for (char c = 'D'; c <= 'K'; ++c) {
        const auto name = std::string("GAMEDATA.WA") + c;
        readers.emplace(name, PackFileReader(game / name));
    }
    for (const auto* name : {"opening", "end006", "end011", "final"}) {
        const auto path = std::string("front\\movies\\") + name + ".sfd";
        readers.emplace(path, PackFileReader(game / path));
    }
    const SourceRead read = [readers](std::string_view name, uint64_t at, std::span<uint8_t> bytes) {
        const auto found = readers.find(std::string(name));
        return found != readers.end() && found->second(at, bytes);
    };
    const SourceSize size = [game](std::string_view name) { return fs::file_size(game / std::string(name)); };
    std::string error;
    if (!Mount(pack, Read(game / "default.xbe"), read, size, error)) { std::cerr << error << '\n'; return 1; }
    std::vector<std::vector<std::string>> files;
    for (char c = 'D'; c <= 'K'; ++c) files.push_back({std::string("GAMEDATA.WA") + c});
    for (const auto* name : {"opening", "end006", "end011", "final"}) files.push_back({"front", "movies", std::string(name) + ".sfd"});
    Bytes actual(0x100003), expected(actual.size());
    uint64_t compared = 0;
    for (const auto& parts : files) {
        const auto file = OverlayFile(parts);
        auto path = reference; for (const auto& part : parts) path /= part;
        if (!file || file->Size() != fs::file_size(path)) { std::cerr << "Virtual size differs: " << path << '\n'; return 1; }
        const auto wanted = PackFileReader(path);
        for (uint64_t at = 0; at < file->Size();) {
            const auto amount = size_t(std::min<uint64_t>(actual.size(), file->Size() - at));
            size_t count;
            if (!file->Read(at, std::span(actual).first(amount), count) || count != amount ||
                !wanted(at, std::span(expected).first(amount)) || !std::equal(actual.begin(), actual.begin() + amount, expected.begin())) {
                std::cerr << "Direct bytes differ at " << at << ": " << path << '\n'; return 1;
            }
            at += amount; compared += amount;
        }
        std::cout << "Verified direct reads: " << path.filename().string() << '\n' << std::flush;
    }
    const auto& stats = Statistics();
    std::cout << "Verified " << compared << " direct bytes; voices=" << stats.archive.voice_names
              << " occurrences=" << stats.archive.voice_occurrences << " unbound=" << stats.archive.unbound_voices.size() << '\n';
    Disable();
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 4) return VerifyReal(argv[1], argv[2], argv[3]);
    using namespace shaolin::language;
    unsigned failures = 0;
    const auto check = [&](bool good, const char* message) { if (!good) { ++failures; std::cerr << message << '\n'; } };
    const auto root = fs::temp_directory_path() / ("shaolin-language-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(root);
    check(FindInstalledPack(root, "pt-BR").empty(), "A missing pack must not be offered as installed");
    const auto installed = root / "languages/LanguagePack_PT-BR";
    fs::create_directories(installed);
    check(FindInstalledPack(root, "pt-BR").empty(), "An unregistered pack must not mount just because its folder exists");
    check(FindInstalledPack(root, "en").empty() && FindInstalledPack(root, "../outside").empty(),
          "Original languages and path fragments must not discover custom packs");

    std::string error;
    Bytes original(32, 0x11), translated(32, 0x55), memory(256, 0xCC);
    original[0] = 'X'; original[1] = 'P'; original[2] = 'R'; original[3] = '0';
    Put32(original, 4, 32); Put32(original, 8, 16);
    std::copy(original.begin(), original.begin() + 16, translated.begin());
    std::copy(original.begin(), original.end(), memory.begin() + 64);
    check(ApplyFontResource(memory, 64, original, translated, error), "A matching font resource must install");
    check(std::equal(translated.begin(), translated.end(), memory.begin() + 64) && memory[63] == 0xCC && memory[96] == 0xCC,
          "Font installation must preserve adjacent memory");
    auto invalid = translated; invalid[12] ^= 1;
    const auto saved = memory;
    check(!ApplyFontResource(memory, 64, original, invalid, error) && memory == saved,
          "A changed font header must fail without writing memory");
    check(!ApplyFontResource(memory, 240, original, translated, error), "A font must not exceed guest memory");

    Bytes adx = {0x80, 0x00, 0, 4, 1, 2, 3, 4};
    Bytes aix = {'A', 'I', 'X', 'F', 5, 6, 7, 8};
    Bytes movie;
    Packet(movie, 0xC0, Bytes(adx.begin(), adx.begin() + 4));
    Packet(movie, 0xC1, {'F', 'R', 'A', 'N', 'C', 'E'});
    Packet(movie, 0xC5, Bytes(aix.begin(), aix.begin() + 4));
    Packet(movie, 0xE0, {'V', 'I', 'D', 'E', 'O'});
    Packet(movie, 0xC0, Bytes(adx.begin() + 4, adx.end()));
    Packet(movie, 0xC5, Bytes(aix.begin() + 4, aix.end()));
    movie.insert(movie.end(), {0, 0, 1, 0xB9});
    const auto source = root / "original.sfd";
    Write(source, movie);
    adx[4] = 91; adx[5] = 92; aix[6] = 93;
    Write(root / "voice.adx", adx); Write(root / "voice.aix", aix);
    SourceRead read = [&](std::string_view, uint64_t at, std::span<uint8_t> data) {
        std::ifstream f(source, std::ios::binary); f.seekg(at);
        return bool(f.read(reinterpret_cast<char*>(data.data()), data.size()));
    };
    SourceSize size = [&](std::string_view) { return fs::file_size(source); };
    auto output = PlanMovie("original.sfd", root / "voice.adx", root / "voice.aix", read, size, error);
    check(bool(output), "Both movie audio representations must be mounted");
    Bytes expected;
    Packet(expected, 0xC0, {0x80, 0x00, 0, 4});
    Packet(expected, 0xC1, {'F', 'R', 'A', 'N', 'C', 'E'});
    Packet(expected, 0xC5, {'A', 'I', 'X', 'F'});
    Packet(expected, 0xE0, {'V', 'I', 'D', 'E', 'O'});
    Packet(expected, 0xC0, {91, 92, 3, 4});
    Packet(expected, 0xC5, {5, 6, 93, 8});
    expected.insert(expected.end(), {0, 0, 1, 0xB9});
    Bytes streamed(expected.size()); size_t count;
    check(output && output->Read(0, streamed, count) && count == streamed.size() && streamed == expected,
          "Direct movie reads must preserve video bytes and other languages");
    check(Read(source) == movie, "Movie preparation must preserve the original file");
    Write(root / "bad.adx", {1});
    check(!PlanMovie("original.sfd", root / "bad.adx", root / "voice.aix", read, size, error),
          "Mismatched audio payload sizes must be rejected");
    check(!fs::exists(root / "translated.sfd") && !fs::exists(root / "bad.sfd"),
          "Movie mounting must not write complete or temporary movies");

    Bytes wad(0x10000, 0);
    std::copy_n("PWF ", 4, wad.begin());
    Put32(wad, 4, uint32_t(wad.size())); Put32(wad, 8, 2); Put32(wad, 12, 7281);
    Put32(wad, 16, 0x12C00000); Put32(wad, 20, 0x800);
    std::copy_n("AFS\0", 4, wad.begin() + 0xE800);
    Put32(wad, 0xE804, 1);
    Put32(wad, 0xE808, 0x800); Put32(wad, 0xE80C, 3);
    Put32(wad, 0xE810, 0x1000); Put32(wad, 0xE814, 48);
    std::copy_n("original.adx", 12, wad.begin() + 0xF800);
    Put32(wad, 0xF82C, 3);
    for (const auto index : {7027,7041,7042,7043,7048,7080,7081,7108,7109,7132,7133,7154,7155,
                             7175,7176,7187,7213,7214,7232,7233,7244,7271,7272}) {
        Put32(wad, 32 + 8*index, 0xE800); Put32(wad, 36 + 8*index, 0x1800);
    }
    Bytes ewdf(32, 0);
    std::copy_n("EWDF", 4, ewdf.begin()); Put32(ewdf, 4, 3); Put32(ewdf, 8, 18);
    ewdf[16] = 'x';
    const uint8_t deflate[] = {0x4B, 0x4C, 0x4A, 0x06, 0x00};
    std::copy(std::begin(deflate), std::end(deflate), ewdf.begin() + 18);
    Put32(ewdf, 23, 0x352441C2); Put32(ewdf, 27, 3);
    Write(installed / "gamedata/record00156.ewdf", ewdf);
    Write(installed / "gamedata/record00158.bin", {1, 2, 3});
    Write(installed / "gamedata/record00164.ewdf", ewdf);
    Write(installed / "audio/voices/unmatched.adx", {1, 2, 3});
    Bytes xbe(0x560000, 0);
    for (const auto& region : {std::pair{0x51D3E8u, 0x4800u}, std::pair{0x532018u, 0x20800u}}) {
        Bytes font(region.second, 0x11);
        std::copy_n("XPR0", 4, font.begin()); Put32(font, 4, region.second); Put32(font, 8, 0x800);
        std::copy(font.begin(), font.end(), xbe.begin() + region.first);
        std::fill(font.begin() + 0x800, font.end(), 0x55);
        Write(installed / (region.first == 0x51D3E8 ? "fonts/small.xpr" : "fonts/large.xpr"), font);
    }
    for (const auto* stem : {"opening", "end006", "end011", "final"}) {
        Write(installed / "audio/movies" / (std::string(stem) + ".adx"), adx);
        Write(installed / "audio/movies" / (std::string(stem) + ".aix"), aix);
    }
    std::set<std::string> movies_read;
    SourceRead source_read = [&](std::string_view name, uint64_t at, std::span<uint8_t> bytes) {
        if (name.ends_with(".sfd")) movies_read.emplace(name);
        const auto& data = name == "GAMEDATA.WAD" ? wad : movie;
        if (at > data.size() || bytes.size() > data.size() - at) return false;
        std::copy_n(data.begin() + at, bytes.size(), bytes.begin()); return true;
    };
    SourceSize source_size = [&](std::string_view name) -> uint64_t {
        if (name == "GAMEDATA.WAD") return wad.size();
        if (name.ends_with(".sfd")) return movie.size();
        return 0;
    };
    const auto file_count = [&] {
        size_t count = 0; for (const auto& item : fs::recursive_directory_iterator(root)) ++count; return count;
    };
    const auto before_mount = file_count();
    check(Mount(installed, xbe, source_read, source_size, error), error.c_str());
    check(movies_read.empty(), "Starting the game must not scan unused movies");
    check(CreditsEnabled() && Statistics().archive.resources == 3 && Statistics().movies == 4,
          "A complete pack must activate resources, movies and credits together");
    auto virtual_wad = OverlayFile({"gamedata.wad"});
    check(virtual_wad && virtual_wad->Size() > 32, "Active DVD reads must resolve to a virtual archive");
    check(!OverlayFile({"..", "GAMEDATA.WAD"}) && !OverlayFile({"English", "unrelated.bin"}),
          "Traversal and unrelated files must not resolve into the pack");
    auto opening = OverlayFile({"front", "movies", "opening.sfd"});
    streamed.assign(expected.size(), 0);
    check(opening && opening->Read(0, streamed, count) && streamed == expected,
          "The first movie read must load its translated tracks correctly");
    check(movies_read == std::set<std::string>{"front\\movies\\opening.sfd"},
          "Reading one movie must leave the other three unscanned");
    check(ValidateMovies(error) && movies_read.size() == 4,
          "Explicit validation must still check every movie without waiting for gameplay");
    check(Mount(installed, xbe, source_read, source_size, error), "A pack must mount again without disk cache");
    check(Statistics().archive.unbound_voices == std::vector<std::string>{"unmatched.adx"},
          "Mounting must retain recordings without an original binding");
    check(file_count() == before_mount && !fs::exists(root / "cache"),
          "Loading directly from mods must not create cache, temporary files or copied archives");
    Disable();
    check(!CreditsEnabled() && !OverlayFile({"GAMEDATA.WAD"}),
          "Original language sessions must disable every custom resource and credits hook");
    // Catch mounting that still requires the complete PT-BR file layout.
    const auto textures = root / "textures";
    Write(textures / "gamedata/record00159.bin", {9, 8, 7});
    Write(textures / "files/Textures/Extra.bin", {4, 5, 6});
    const bool texture_mounted=Mount(textures, xbe, source_read, source_size, error);
    if(!texture_mounted)std::cerr<<"Content mount: "<<error<<'\n';
    check(texture_mounted, "A content mod must mount without PT-BR fonts or movie audio");
    const auto direct = OverlayFile({"textures", "extra.BIN"});
    Bytes direct_bytes(3);
    check(direct && direct->Read(0, direct_bytes, count) && direct_bytes == Bytes({4,5,6}), "Generic game files must resolve case-insensitively");
    check(!OverlayFile({"Textures", "..", "Extra.bin"}), "Generic overrides must reject traversal");
    check(!CreditsEnabled(), "Texture mods must not activate translation credits hooks");
    Disable();
    const std::array<fs::path,2> combined{installed,textures};
    check(MountPacks(combined,xbe,source_read,source_size,true,error),"Compatible language and texture packs must combine in one archive plan");
    check(Statistics().archive.resources==4&&OverlayFile({"Textures","Extra.bin"})&&CreditsEnabled(),
          "Combined mounting must retain the language, texture and direct-file resources");
    Disable();
    const std::array<fs::path,2> conflict{textures,textures};
    check(!MountPacks(conflict,xbe,source_read,source_size,false,error)&&!OverlayFile({"gamedata.wad"}),
          "Conflicting resource plans must fail without publishing partial overlays");
    virtual_wad.reset(); output.reset(); opening.reset();
    fs::remove_all(root);
    return failures ? 1 : 0;
}
