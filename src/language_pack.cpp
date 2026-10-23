#include "language_pack.h"
#include "mods/manager.h"
#include "file_ops.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>

namespace fs = std::filesystem;
namespace shaolin::language {
namespace {
using Bytes = std::vector<uint8_t>;
std::atomic<bool> active{false};
bool translated_credits=false;
PackStats statistics;
struct Font { uint32_t address; Bytes original, replacement; };
std::vector<Font> fonts;
std::map<std::string, std::shared_ptr<const VirtualFile>> overlays;
struct DeferredMovie {
    std::once_flag once;
    std::function<std::shared_ptr<const VirtualFile>(std::string&)> build;
    std::shared_ptr<const VirtualFile> file;
    std::string error;
    bool Initialize() {
        std::call_once(once, [&] { file = build(error); });
        return bool(file);
    }
};
std::vector<std::shared_ptr<DeferredMovie>> deferred_movies;
constexpr std::array<const char*, 4> movies{"opening", "end006", "end011", "final"};
uint32_t U32(std::span<const uint8_t> b, size_t at) {
    if (at > b.size() || b.size() - at < 4) throw std::runtime_error("Truncated language resource");
    return uint32_t(b[at]) | uint32_t(b[at+1]) << 8 | uint32_t(b[at+2]) << 16 | uint32_t(b[at+3]) << 24;
}
Bytes Load(const fs::path& path, uint64_t limit = 0x4000000) {
    const auto size = fs::file_size(path);
    if (size > limit) throw std::runtime_error("Language resource exceeds its size limit");
    Bytes bytes(static_cast<size_t>(size));
    std::ifstream f(path, std::ios::binary);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) throw std::runtime_error("Cannot read language resource");
    return bytes;
}
std::string Key(std::string name) {
    for (auto& c : name) { if (c == '\\') c = '/'; if (c >= 'a' && c <= 'z') c -= 'a' - 'A'; }
    return name;
}
struct Window {
    const SourceRead& source;
    std::string name;
    uint64_t length, begin = UINT64_MAX;
    Bytes buffer;
    Window(const SourceRead& read, std::string_view file, uint64_t size) : source(read), name(file), length(size) {}
    uint8_t Byte(uint64_t at) {
        if (at >= length) throw std::runtime_error("Truncated SFD packet");
        if (begin == UINT64_MAX || at < begin || at - begin >= buffer.size()) {
            begin = at;
            buffer.resize(size_t(std::min<uint64_t>(0x100000, length - at)));
            if (!source(name, begin, buffer)) throw std::runtime_error("Cannot read original movie");
        }
        return buffer[size_t(at - begin)];
    }
};
struct AudioSpan { uint64_t begin, length, replacement_offset; unsigned track; };

} // namespace

fs::path FindInstalledPack(const fs::path& directory, std::string_view language) {
    return mods::EnabledLanguagePack(directory, language);
}

std::shared_ptr<const VirtualFile> PlanMovie(std::string_view source_name, const fs::path& stereo, const fs::path& surround,
                const SourceRead& read, const SourceSize& size, std::string& error) {
    try {
        const uint64_t length = size(source_name);
        if (!length) throw std::runtime_error("The original movie is missing");
        Window input(read, source_name, length);
        uint64_t cursor = 0;
        std::array<uint64_t, 2> consumed{};
        std::vector<AudioSpan> spans;
        while (cursor < length) {
            if (length - cursor <= 0x800 && input.Byte(cursor) == 0xFF) {
                bool padding = true;
                for (auto at = cursor; at < length; ++at) padding &= input.Byte(at) == 0xFF;
                if (padding) { cursor = length; break; }
            }
            if (length - cursor < 4 || input.Byte(cursor) || input.Byte(cursor+1) || input.Byte(cursor+2) != 1)
                throw std::runtime_error("Invalid MPEG program-stream packet");
            const auto id = input.Byte(cursor + 3);
            uint64_t packet;
            if (id == 0xBA) packet = (input.Byte(cursor+4) & 0xF0) == 0x20 ? 12 : 14 + (input.Byte(cursor+13) & 7);
            else if (id == 0xB9) packet = 4;
            else {
                const auto payload = unsigned(input.Byte(cursor+4)) * 256 + input.Byte(cursor+5);
                if (!payload) throw std::runtime_error("Invalid empty MPEG packet");
                packet = 6 + payload;
                if (packet > length - cursor) throw std::runtime_error("MPEG packet exceeds the movie");
                if (id == 0xC0 || id == 0xC5) {
                    auto begin = cursor + 6;
                    const auto end = cursor + packet;
                    while (begin < end && input.Byte(begin) == 0xFF) ++begin;
                    if (begin + 2 <= end && (input.Byte(begin) & 0xC0) == 0x40) begin += 2;
                    if (begin < end) {
                        const auto marker = input.Byte(begin);
                        if ((marker & 0xF0) == 0x20) begin += 5;
                        else if ((marker & 0xF0) == 0x30) begin += 10;
                        else if (marker == 0x0F) ++begin;
                        else if ((marker & 0xC0) == 0x80 && begin + 3 <= end) begin += 3 + input.Byte(begin+2);
                        else throw std::runtime_error("Unsupported PES audio header");
                    }
                    if (begin > end) throw std::runtime_error("PES audio header exceeds its packet");
                    const unsigned track = id == 0xC0 ? 0 : 1;
                    spans.push_back({begin, end - begin, consumed[track], track});
                    consumed[track] += end - begin;
                }
            }
            if (packet > length - cursor) throw std::runtime_error("Truncated MPEG packet");
            cursor += packet;
        }
        if (!consumed[0] || !consumed[1] || consumed[0] != fs::file_size(stereo) || consumed[1] != fs::file_size(surround))
            throw std::runtime_error("The movie audio payload sizes do not match the original tracks");
        auto movie = std::make_shared<VirtualFile>();
        const auto source = [read, name = std::string(source_name)](uint64_t at, std::span<uint8_t> bytes) { return read(name, at, bytes); };
        const std::array<ReadAt, 2> audio{PackFileReader(stereo), PackFileReader(surround)};
        uint64_t at = 0;
        for (const auto& span : spans) {
            if (span.begin > at) movie->Add(at, span.begin-at, at, source);
            movie->Add(span.begin, span.length, span.replacement_offset, audio[span.track]);
            at = span.begin + span.length;
        }
        if (at < length) movie->Add(at, length-at, at, source);
        movie->SetSize(length); movie->Seal();
        return movie;
    } catch (const std::exception& e) { error = e.what(); return {}; }

}

bool ApplyFontResource(std::span<uint8_t> memory, uint32_t address, std::span<const uint8_t> original,
                       std::span<const uint8_t> replacement, std::string& error) {
    try {
        if (original.size() < 12 || replacement.size() != original.size() || std::memcmp(original.data(), "XPR0", 4) ||
            U32(original, 4) != original.size()) throw std::runtime_error("Invalid translated XPR font resource");
        const auto header = U32(original, 8);
        if (header < 12 || header > original.size() || !std::equal(original.begin(), original.begin() + header, replacement.begin()))
            throw std::runtime_error("The translated font changes the original resource header");
        if (address > memory.size() || replacement.size() > memory.size() - address ||
            !std::equal(original.begin(), original.begin() + header, memory.begin() + address))
            throw std::runtime_error("The guest font destination does not match the supported original");
        std::copy(replacement.begin() + header, replacement.end(), memory.begin() + address + header);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

void Disable() { active = false; translated_credits=false; overlays.clear(); deferred_movies.clear(); fonts.clear(); statistics = {}; }
bool CreditsEnabled() { return active.load() && translated_credits; }
const PackStats& Statistics() { return statistics; }

bool Mount(const fs::path& pack, std::span<const uint8_t> xbe,
           const SourceRead& read, const SourceSize& size, std::string& error) {
    const std::array<fs::path,1> packs{pack};
    const bool legacy=fs::is_regular_file(pack/"gamedata/record00156.ewdf") && fs::is_regular_file(pack/"gamedata/record00164.ewdf")
        && fs::is_regular_file(pack/"fonts/small.xpr") && fs::is_regular_file(pack/"fonts/large.xpr");
    if(!MountPacks(packs,xbe,read,size,legacy,error))return false;
    if(!active){error="This pack contains no supported game resources";return false;}
    return true;
}
bool MountPacks(std::span<const fs::path> packs,std::span<const uint8_t> xbe,
                const SourceRead& read,const SourceSize& size,bool credits,std::string& error) {
    Disable(); error.clear();
    try {
        bool archive_resources=false;
        for(const auto& pack:packs) {
            if(!fs::is_directory(pack))throw std::runtime_error("Installed mod folder is missing: "+pack.string());
            for(const auto& item:fs::recursive_directory_iterator(pack))
                if(item.is_symlink()||!item.is_directory()&&!item.is_regular_file())
                    throw std::runtime_error("Mods must contain regular files and directories");
            for(const auto* name:{"gamedata","audio/voices"})
                if(fs::exists(pack/name)&&!fs::is_empty(pack/name))archive_resources=true;
        }
        if(archive_resources) {
            ArchiveView archives;
            if(!PlanArchivesForPacks(packs,read,size,archives,statistics.archive,error))throw std::runtime_error(error);
            for(size_t i=0;i<archives.size();++i)
                overlays.emplace(std::string("GAMEDATA.WA")+char('D'+i),std::move(archives[i]));
        }
        const auto add=[&](std::string key,std::shared_ptr<const VirtualFile> file) {
            if(!overlays.emplace(Key(key),std::move(file)).second)throw std::runtime_error("Enabled mods replace the same game file: "+key);
        };
        for(const auto& pack:packs) {
          const auto movie_folder=pack/"audio/movies";
          std::map<std::string,unsigned> supplied_movies;
          if(fs::exists(movie_folder))for(const auto& item:fs::directory_iterator(movie_folder)) {
            auto ext=item.path().extension().string();
            if(ext!=".adx"&&ext!=".aix")throw std::runtime_error("Movie audio must use .adx and .aix files");
            supplied_movies[item.path().stem().string()] |= ext==".adx"?1:2;
          }
          for (const auto& [stem,tracks] : supplied_movies) {
            if(tracks!=3)throw std::runtime_error("Supply both movie audio tracks: "+stem);
            const auto source_name = "front\\movies\\" + stem + ".sfd";
            const auto stereo = pack / ("audio/movies/" + stem + ".adx");
            const auto surround = pack / ("audio/movies/" + stem + ".aix");
            const auto length = size(source_name);
            if (!length || !fs::is_regular_file(stereo) || !fs::is_regular_file(surround) ||
                !fs::file_size(stereo) || !fs::file_size(surround))
                throw std::runtime_error("The movie language resources are incomplete: " + stem);
            auto deferred = std::make_shared<DeferredMovie>();
            deferred->build = [source_name, stereo, surround, read, size](std::string& message) {
                return PlanMovie(source_name, stereo, surround, read, size, message);
            };
            auto movie = std::make_shared<VirtualFile>();
            movie->Add(0, length, 0, [deferred](uint64_t at, std::span<uint8_t> bytes) {
                size_t count;
                return deferred->Initialize() && deferred->file->Read(at, bytes, count) && count == bytes.size();
            });
            movie->SetSize(length); movie->Seal();
            deferred_movies.push_back(std::move(deferred));
            add("front/movies/" + stem + ".sfd",std::move(movie));
            ++statistics.movies;
        }
        struct Region { const char* file; uint32_t offset, address, size; };
        for (const auto& region : {Region{"small.xpr", 0x51D3E8, 0x538688, 0x4800}, Region{"large.xpr", 0x532018, 0x54D2B8, 0x20800}}) {
            if(!fs::exists(pack/"fonts"/region.file))continue;
            if(std::any_of(fonts.begin(),fonts.end(),[&](auto& font){return font.address==region.address;}))
                throw std::runtime_error("Enabled mods replace the same embedded font");
            if (uint64_t(region.offset) + region.size > xbe.size()) throw std::runtime_error("The original XBE lacks its expected embedded fonts");
            const auto original = xbe.subspan(region.offset, region.size);
            auto replacement = Load(pack / "fonts" / region.file);
            if (original.size() != replacement.size() || U32(original, 8) > original.size() ||
                !std::equal(original.begin(), original.begin() + U32(original, 8), replacement.begin()))
                throw std::runtime_error("The translated font header differs from the original");
            fonts.push_back({region.address, Bytes(original.begin(), original.end()), std::move(replacement)});
        }
        const auto files_folder=pack/"files";
        if(fs::exists(files_folder))for(const auto& item:fs::recursive_directory_iterator(files_folder))if(item.is_regular_file()) {
            auto target=files::Utf8(item.path().lexically_relative(files_folder));
            std::replace(target.begin(),target.end(),'\\','/');
            if(!files::SafeRelative(target)||Key(target)=="DEFAULT.XBE")throw std::runtime_error("Invalid direct game file override");
            auto file=std::make_shared<VirtualFile>();
            const auto length=item.file_size();
            if(length)file->Add(0,length,0,PackFileReader(item.path()));
            file->SetSize(length);file->Seal();add(target,std::move(file));
        }
        }
        active = !overlays.empty()||!fonts.empty();
        translated_credits=credits;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        Disable();
        return false;
    }
}

bool ValidateMovies(std::string& error) {
    for (const auto& movie : deferred_movies) {
        if (!movie->Initialize()) { error = movie->error; return false; }
    }
    return true;
}

bool ApplyFonts(std::span<uint8_t> memory, std::string& error) {
    if (!active) return true;
    for (const auto& font : fonts) if (!ApplyFontResource(memory, font.address, font.original, font.replacement, error)) return false;
    return true;
}
std::shared_ptr<const VirtualFile> OverlayFile(const std::vector<std::string>& parts) {
    if (!active) return {};
    std::string name;
    for (const auto& part : parts) {
        if (part.empty() || part == ".") continue;
        if (part == ".." || part.find_first_of(":/\\") != std::string::npos) return {};
        if (!name.empty()) name += '/';
        name += part;
    }
    const auto match = overlays.find(Key(name));
    return match == overlays.end() ? nullptr : match->second;
}
} // namespace shaolin::language
