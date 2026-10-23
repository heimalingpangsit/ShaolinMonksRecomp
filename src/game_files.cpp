#include "host.h"
#include "app/app.h"
#include "core/log.h"
#include "kernel/disc.h"
#include "game_setup.h"
#include "runtime_paths.h"
#include "launcher.h"
#include "language_pack.h"
#include "mods/manager.h"
#include "mods/languages.h"
#include "mods/runtime.h"
#include "mods/sha256.h"
#include "file_ops.h"
#include <algorithm>
#include <mutex>
#include <fstream>
#include <map>

namespace {
std::unique_ptr<shaolin::storage::DirectoryLock> mod_lock;
std::filesystem::path OriginalPath(const std::filesystem::path& root,std::string name) {
    std::replace(name.begin(),name.end(),'\\','/');
    if(!shaolin::files::SafeRelative(name))return {};
    auto path=root;
    for(const auto& part:shaolin::files::FromUtf8(name)) {
        auto next=path/part;
        if(!std::filesystem::exists(next)) {
            auto key=shaolin::files::Utf8(part);
            const auto lower=[](std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c+='a'-'A';return value;};
            key=lower(key);
            if(!std::filesystem::is_directory(path))return {};
            for(const auto& item:std::filesystem::directory_iterator(path))
                if(lower(shaolin::files::Utf8(item.path().filename()))==key){next=item.path();break;}
        }
        if(std::filesystem::is_symlink(next))return {};
        path=next;
    }
    return path;
}
bool MountLanguage(const shaolin::setup::Source& source,const app::GameFiles& game,std::string& error) {
    using namespace shaolin;
    language::Disable();mods::StageRoutines({});
    try {
        const auto selected=options.validate_language_pack?std::string("pt-BR"):launcher::Current().language;
        std::vector<std::filesystem::path> packs;
        std::vector<mods::RoutineScript> scripts;
        bool credits=false;
        if(!options.language_pack.empty())packs.push_back(options.language_pack);
        else {
            mods::Recover(paths::Mods());
            mod_lock=std::make_unique<storage::DirectoryLock>();
            if(!mod_lock->Acquire(paths::Mods(),error))return false;
            const auto active=mods::ActiveMods(paths::Mods(),selected);
            bool language_ready=mods::IsOriginalLanguage(selected);
            for(const auto& row:active) {
                const auto pack=paths::Mods()/files::FromUtf8(row.mod.install_dir);
                if(!files::Within(pack,paths::Mods())||!std::filesystem::is_directory(pack))
                    throw std::runtime_error("Installed mod folder is missing: "+row.mod.name);
                packs.push_back(pack);
                if(row.mod.type=="language") {language_ready=true;credits=selected=="pt-BR";}
                for(const auto& path:row.mod.routines) {
                    const auto script=pack/files::FromUtf8(path);
                    const auto file=std::find_if(row.mod.files.begin(),row.mod.files.end(),[&](auto& f){return f.path==path;});
                    if(file==row.mod.files.end()||mods::Sha256File(script)!=file->sha256)
                        throw std::runtime_error("Installed routine script changed: "+row.mod.name);
                    scripts.push_back({row.mod.id,script});
                }
            }
            if(!language_ready)throw std::runtime_error("Install and enable a pack for the selected language in Mods, or select another language.");
        }
        if(packs.empty())return true;
        const auto* disc=kernel::MountedDisc();
        struct Readers {std::mutex mutex;std::map<std::string,language::ReadAt> files;};
        const auto readers=std::make_shared<Readers>();
        const language::SourceSize size=[disc,path=source.path](std::string_view name)->uint64_t {
            if(disc){const auto* entry=disc->Find(name);return entry&&!entry->dir?entry->size:0;}
            const auto file=OriginalPath(path,std::string(name));std::error_code ec;
            const auto length=std::filesystem::file_size(file,ec);return ec?0:length;
        };
        const language::SourceRead read=[disc,readers,path=source.path](std::string_view name,uint64_t at,std::span<uint8_t> bytes) {
            if(disc) {
                const auto* entry=disc->Find(name);
                return entry&&!entry->dir&&bytes.size()<=UINT32_MAX&&disc->Read(*entry,at,bytes.data(),uint32_t(bytes.size()))==bytes.size();
            }
            language::ReadAt reader;
            {
                std::lock_guard lock(readers->mutex);
                auto found=readers->files.find(std::string(name));
                if(found==readers->files.end()) {
                    const auto file=OriginalPath(path,std::string(name));
                    if(file.empty())return false;
                    found=readers->files.emplace(name,language::PackFileReader(file)).first;
                }
                reader=found->second;
            }
            return reader(at,bytes);
        };
        const bool mounted=options.language_pack.empty()
            ?language::MountPacks(packs,game.xbe,read,size,credits,error)
            :language::Mount(packs.front(),game.xbe,read,size,error);
        if(!mounted)return false;
        if(options.validate_language_pack&&!language::ValidateMovies(error))return false;
        mods::StageRoutines(std::move(scripts));
        const auto& stats=language::Statistics();
        LOGR("Mods ready: packs=%zu resources=%u voices=%u occurrences=%u movies=%u",packs.size(),stats.archive.resources,
             stats.archive.voice_names,stats.archive.voice_occurrences,stats.movies);
        return true;
    }catch(const std::exception& e){error=e.what();language::Disable();mods::StageRoutines({});return false;}
}
}

bool app::FindGame(GameFiles& game) {
    bool remember = shaolin::options.register_disc;
    auto input = shaolin::options.disc.empty() ? shaolin::options.game : shaolin::options.disc;
    if (input.empty() && !shaolin::options.background) {
        if (!shaolin::options.setup) input = shaolin::setup::RememberedDisc();
        if (shaolin::options.setup || input.empty() || !std::filesystem::exists(input)) {
            if (!shaolin::setup::ChooseDisc(input)) return false;
            remember = true;
        }
    }
    const auto failure = [&](const std::string& message) {
        LOGE("Game setup: %s", message.c_str());
        if (!shaolin::options.validate_disc && !shaolin::options.validate_language_pack)
            shaolin::paths::ReportError(std::filesystem::path(std::u8string(message.begin(), message.end())).wstring());
        return false;
    };
    if (input.empty()) return failure("Select your Xbox game with --setup.");
    shaolin::setup::Source source;
    std::vector<uint8_t> bytes;
    std::string error;
    if (!shaolin::setup::PrepareGame(input, source, bytes, error,
                                    !shaolin::options.background && !shaolin::options.validate_disc && !shaolin::options.validate_language_pack))
        return failure(error);
    if (remember && shaolin::launcher::ShouldPersist() && !shaolin::setup::RememberDisc(source.path, error)) return failure(error);
    if (source.folder) {
        kernel::MountDisc(nullptr);
        game = {source.path, std::move(bytes)};
    } else {
        auto disc = std::make_unique<kernel::Disc>();
        if (!disc->Open(source.path, error)) return failure(error);
        kernel::MountDisc(std::move(disc));
        game = {shaolin::paths::Cache() / "dvd-scratch", std::move(bytes)};
    }
    if (!MountLanguage(source, game, error)) return failure(error);
    LOGR("Verified Shaolin Monks game files");
    return true;
}
