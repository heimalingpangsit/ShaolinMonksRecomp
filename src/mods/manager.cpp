#include "manager.h"
#include "tar_xz.h"
#include "sha256.h"
#include "vcdiff.h"
#include "routines.h"
#include "../file_ops.h"
#include "../save_storage.h"
#include <toml++/toml.hpp>
#include <fstream>
#include <sstream>
#include <set>
#include <algorithm>
namespace fs=std::filesystem;
namespace shaolin::mods {
namespace {
std::string ReadText(const fs::path& path) {
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot read mod metadata");return {std::istreambuf_iterator<char>(f),{}};
}
std::string State(const std::vector<Installed>& installed) {
    toml::array rows;
    for(const auto& m:installed)rows.push_back(toml::table{{"id",m.mod.id},{"version",m.mod.version},{"enabled",m.enabled},{"manifest",m.mod.raw}});
    const toml::table table{{"schema",1},{"generation",files::Utf8(files::UniqueSibling("state","-").filename())},{"mods",rows}};
    std::ostringstream out;out<<toml::toml_formatter{table,toml::format_flags::none};return out.str();
}
void Lock(const fs::path& root,storage::DirectoryLock& lock) {
    fs::create_directories(root);std::string error;if(!lock.Acquire(root,error))throw std::runtime_error("Mods are in use. Close other launchers or games. "+error);
}
// Roll back any transaction whose registry commit did not complete. Only paths
// recorded under the owned mods root are eligible for recovery.
void RecoverLocked(const fs::path& root) {
    const auto journal=root/"transaction.toml";if(!fs::exists(journal))return;
    const auto j=toml::parse_file(files::Utf8(journal));
    const auto dir=j["destination"].value_or(std::string{}), backup=j["backup"].value_or(std::string{});
    if(!files::SafeRelative(dir)||!files::SafeRelative(backup)||!backup.starts_with(".backup-"))throw std::runtime_error("Invalid mod transaction; recovery requires manual inspection");
    const auto destination=root/files::FromUtf8(dir),old=root/files::FromUtf8(backup);
    if(!files::Within(destination,root)||!files::Within(old,root))throw std::runtime_error("Mod transaction points outside the mods folder");
    const auto previous=j["previous"].value_or(std::string{}), next=j["next"].value_or(std::string{});
    const bool committed=fs::exists(root/"installed.toml")&&ReadText(root/"installed.toml")==next;
    if(committed) {
        std::error_code ec;fs::remove_all(old,ec);
        if(ec)throw std::runtime_error("Cannot clean up the previous mod version: "+ec.message());
    } else {
        const bool existed=j["existed"].value_or(false);
        if(fs::exists(old)) {
            if(fs::exists(destination))fs::remove_all(destination);
            fs::create_directories(destination.parent_path());fs::rename(old,destination);
        }else if(!existed&&fs::exists(destination))fs::remove_all(destination);
        files::AtomicWrite(root/"installed.toml",previous);
    }
    fs::remove(journal);
}
void Switch(const fs::path& root,const fs::path& staging,const Manifest& mod,const std::vector<Installed>& rows) {
    const auto destination=root/files::FromUtf8(mod.install_dir);
    if(!files::Within(destination,root))throw std::runtime_error("Mod destination escapes the mods folder");
    const auto backup=files::UniqueSibling(root/".backup", "-");
    const auto previous=fs::exists(root/"installed.toml")?ReadText(root/"installed.toml"):State({});
    const auto next=State(rows);
    std::ostringstream out;
    const toml::table journal{{"destination",mod.install_dir},{"backup",files::Utf8(backup.filename())},
                             {"existed",fs::exists(destination)},{"previous",previous},{"next",next}};
    // The journal embeds a registry which itself embeds JSON. Basic escaped
    // strings avoid nested multiline delimiters at both serialization layers.
    out<<toml::toml_formatter{journal,toml::format_flags::none};
    files::AtomicWrite(root/"transaction.toml",out.str());
    try {
        if(fs::exists(destination))fs::rename(destination,backup);
        if(!staging.empty()) { fs::create_directories(destination.parent_path());fs::rename(staging,destination); }
        files::AtomicWrite(root/"installed.toml",next);
    }catch(...) { RecoverLocked(root);throw; }
    // The registry is now committed. Cleanup can be retried on the next launch.
    try { RecoverLocked(root); }catch(...){}
}
}
Package::~Package() { if(!folder.empty()){std::error_code ec;fs::remove_all(folder,ec);} }
std::shared_ptr<Package> InspectPackage(const fs::path& archive,const fs::path& root,const Progress& progress) {
    auto p=std::make_shared<Package>();fs::create_directories(root);
    p->folder=files::UniqueSibling(root/".package","-");fs::create_directory(p->folder);
    ExtractTarXz(archive,p->folder,ProgressSlice(progress,0,8000));
    Report(progress,"Reading mod information",8000,ProgressTotal);
    const bool current=fs::exists(p->folder/"mod.json"),legacy=fs::exists(p->folder/"manifest.json");
    if(current&&legacy)throw std::runtime_error("The package contains both mod.json and manifest.json. Rebuild it with the Krypt builder.");
    const std::string metadata=current?"mod.json":"manifest.json";
    try {p->mod=ParseManifest(ReadText(p->folder/metadata));}
    catch(const std::exception& e){throw std::runtime_error("Cannot read mod.json. Use a .tar.xz built by the Krypt release tool. "+std::string(e.what()));}
    std::set<std::string> expected{metadata};
    const auto verification=ProgressSlice(progress,8000,9500);
    uint64_t checked=0;
    for(const auto& f:p->mod.files) {
        Report(verification,"Verifying files",checked,p->mod.files.size()); const auto path=p->folder/files::FromUtf8(f.path);
        if(!fs::is_regular_file(path)||fs::file_size(path)!=f.size||Sha256File(path)!=f.sha256)throw std::runtime_error("Mod file failed SHA-256 verification: "+f.path);
        expected.insert(f.path);
        Report(verification,"Verifying files",++checked,p->mod.files.size());
    }
    Report(progress,"Checking package contents",9500,ProgressTotal);
    for(const auto& patch:p->mod.patches) {
        const auto name=patch.path+".xdelta";if(!fs::is_regular_file(p->folder/files::FromUtf8(name)))throw std::runtime_error("Missing xdelta patch: "+patch.path);
        expected.insert(name);
    }
    for(const auto& f:fs::recursive_directory_iterator(p->folder))if(f.is_regular_file()) {
        auto name=files::Utf8(f.path().lexically_relative(p->folder));std::replace(name.begin(),name.end(),'\\','/');
        if(!expected.erase(name))throw std::runtime_error("Undeclared file in mod archive: "+name);
    }
    if(!expected.empty())throw std::runtime_error("Mod archive is missing files");
    Report(progress,"Checking routine scripts",9800,ProgressTotal);
    if(!p->mod.routines.empty()) {
        RoutineEngine engine;std::vector<RoutineScript> scripts;std::string error;
        for(const auto& path:p->mod.routines)scripts.push_back({p->mod.id,p->folder/files::FromUtf8(path)});
        if(!engine.Load(scripts,CompiledRoutineTargets(),{},error))throw std::runtime_error(error);
    }
    return p;
}
void InstallPackage(const fs::path& root,const Package& package,const Original& original,const Progress& progress) {
    storage::DirectoryLock lock;Lock(root,lock);RecoverLocked(root);
    auto rows=ReadInstalled(root);
    for(const auto& row:rows)if(row.mod.id==package.mod.id&&row.mod.install_dir!=package.mod.install_dir)
        throw std::runtime_error("A mod update must keep its installation directory");
    if(fs::exists(root/files::FromUtf8(package.mod.install_dir))&&std::none_of(rows.begin(),rows.end(),[&](auto& r){return r.mod.id==package.mod.id;}))
        throw std::runtime_error("An unregistered folder already occupies this mod's installation path");
    for(const auto& row:rows)if(row.mod.id!=package.mod.id&&(files::Within(root/row.mod.install_dir,root/package.mod.install_dir)||files::Within(root/package.mod.install_dir,root/row.mod.install_dir)))throw std::runtime_error("Mod installation folders overlap");
    const auto stage=files::UniqueSibling(root/".install","-");fs::create_directory(stage);
    try {
        // Copy the inspected package so failures never modify it or the live mod.
        const auto copying=ProgressSlice(progress,0,3500),patching=ProgressSlice(progress,3500,9500);
        uint64_t copied=0;
        for(const auto& f:package.mod.files) {
            Report(copying,"Copying files",copied,package.mod.files.size());const auto src=package.folder/files::FromUtf8(f.path),target=stage/files::FromUtf8(f.path);
            fs::create_directories(target.parent_path());fs::copy_file(src,target);
            files::FlushFile(target);
            if(Sha256File(target)!=f.sha256)throw std::runtime_error("Mod file changed after inspection: "+f.path);
            Report(copying,"Copying files",++copied,package.mod.files.size());
        }
        uint64_t done=0;
        for(const auto& p:package.mod.patches) {
            Report(patching,"Applying patches",done,package.mod.patches.size());
            auto source=original(p);if(Sha256(source)!=p.source_hash)throw std::runtime_error("Original game data does not match this mod: "+p.path);
            const auto patch_path=package.folder/files::FromUtf8(p.path+".xdelta");
            std::ifstream f(patch_path,std::ios::binary);std::vector<uint8_t> delta{std::istreambuf_iterator<char>(f),{}};
            const auto target=ApplyDelta(source,delta);
            if(Sha256(target)!=p.target_hash)throw std::runtime_error("Patched file failed SHA-256 verification: "+p.path);
            const auto path=stage/files::FromUtf8(p.path);fs::create_directories(path.parent_path());
            files::AtomicWrite(path,{reinterpret_cast<const char*>(target.data()),target.size()});
            Report(patching,"Applying patches",++done,package.mod.patches.size());
        }
        Report(progress,"Checking installation",9500,ProgressTotal);
        files::AtomicWrite(stage/"mod.json",package.mod.raw);
        auto existing=std::find_if(rows.begin(),rows.end(),[&](auto& r){return r.mod.id==package.mod.id;});
        if(existing==rows.end())rows.push_back({package.mod,true});else existing->mod=package.mod;
        ValidateEnabled(rows);
        ValidateRoutines(root,rows,package.mod.id,stage);
        Report(progress,"Finishing installation",9900,ProgressTotal);Switch(root,stage,package.mod,rows);
    } catch(...) {std::error_code ec;fs::remove_all(stage,ec);throw;}
}
void SetEnabled(const fs::path& root,std::string_view id,bool enabled) {
    storage::DirectoryLock lock;Lock(root,lock);RecoverLocked(root);auto rows=ReadInstalled(root);bool found=false;
    for(auto& row:rows)if(row.mod.id==id){row.enabled=enabled;found=true;}
    if(!found)throw std::runtime_error("This mod is not installed");ValidateEnabled(rows);ValidateRoutines(root,rows);files::AtomicWrite(root/"installed.toml",State(rows));
}
void Uninstall(const fs::path& root,std::string_view id) {
    storage::DirectoryLock lock;Lock(root,lock);RecoverLocked(root);auto rows=ReadInstalled(root);
    const auto it=std::find_if(rows.begin(),rows.end(),[&](auto& r){return r.mod.id==id;});
    if(it==rows.end())throw std::runtime_error("This mod is not installed");
    const auto mod=it->mod;rows.erase(it);ValidateEnabled(rows);Switch(root,{},mod,rows);
}
void Recover(const fs::path& root) {storage::DirectoryLock lock;Lock(root,lock);RecoverLocked(root);}
}
