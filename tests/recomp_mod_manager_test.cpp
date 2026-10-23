#include "mods/manager.h"
#include "mods/sha256.h"
#include "file_ops.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <toml++/toml.hpp>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace fs=std::filesystem;
using namespace shaolin;
void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string Read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){try{
    Require(argc==2,"Fixture folder required");const auto base=files::FromUtf8(argv[1]),root=base/"mods";
    const auto layout_rejected=[](std::initializer_list<const char*> paths) {
        mods::Installed row;row.mod.id="Layout";row.mod.name="Layout";row.mod.type="mixed";
        for(const auto* path:paths)row.mod.files.push_back({path,std::string(64,'0'),0});
        try {mods::ValidateEnabled({row});return false;}catch(...){return true;}
    };
    Require(layout_rejected({"files/GAMEDATA.WAD","gamedata/record00001.bin"}),"One mod must not replace a whole archive and its individual resources");
    Require(layout_rejected({"gamedata/record00001.bin","gamedata/record1.ewdf"}),"Different spellings must not replace the same numbered resource");
    Require(layout_rejected({"gamedata/record07027.bin","audio/voices/voice.adx"}),"One mod must not replace both a voice bank and its recordings");
    const auto original=[](const mods::Patch&){std::string text;for(int i=0;i<40;++i)text+="The original game data.\n";return std::vector<uint8_t>(text.begin(),text.end());};
    uint64_t last_progress=0;
    const mods::Progress progress=[&](auto,uint64_t done,uint64_t total){
        Require(total>0&&done<=total,"Mod operations must report a bounded overall progress value");
        const auto value=done*10000/total;
        Require(value>=last_progress,"Mod progress must not restart between stages");
        last_progress=value;return true;
    };
    auto good=mods::InspectPackage(base/"good.tar.xz",root,progress);
    const auto legacy=mods::InspectPackage(base/"legacy.tar.xz",root);
    Require(legacy->mod.raw==good->mod.raw,"Existing manifest.json packages must retain their metadata");
    mods::InstallPackage(root,*legacy,original);
    const auto metadata=root/"languages/LanguagePack_PT-BR/mod.json";
    Require(Read(metadata)==legacy->mod.raw&&!fs::exists(metadata.parent_path()/"manifest.json"),
            "Both metadata names must install as mod.json");
    fs::rename(metadata,metadata.parent_path()/"manifest.json");
    mods::SetEnabled(root,legacy->mod.id,false);
    mods::InstallPackage(root,*good,original);
    Require(Read(metadata)==good->mod.raw&&!fs::exists(metadata.parent_path()/"manifest.json")&&!mods::ReadInstalled(root)[0].enabled,
            "Updating an old installation must adopt mod.json and preserve its disabled state");
    mods::Uninstall(root,legacy->mod.id);
    // Catch silent last-writer-wins behavior and broken dependency state.
    const auto install = [&](const char* id) {
        const auto package = mods::InspectPackage(base/(std::string(id)+".tar.xz"),root);
        mods::InstallPackage(root,*package,original);
    };
    install("TextureA");
    const auto initial_registry = Read(root/"installed.toml");
    bool conflict = false;
    try { install("TextureB"); } catch (...) { conflict = true; }
    Require(conflict && Read(root/"installed.toml")==initial_registry && !fs::exists(root/"packages/TextureB"),
            "Conflicting enabled replacements must preserve the installed state");
    mods::SetEnabled(root,"TextureA",false);
    install("TextureB");
    conflict = false;
    try { mods::SetEnabled(root,"TextureA",true); } catch (...) { conflict = true; }
    Require(conflict,"Enabling conflicting installed mods must fail");
    mods::Uninstall(root,"TextureB");
    mods::SetEnabled(root,"TextureA",true);
    install("Accessory");
    for (const bool remove : {false,true}) {
        bool blocked = false;
        try { if(remove)mods::Uninstall(root,"TextureA");else mods::SetEnabled(root,"TextureA",false); } catch (...) { blocked = true; }
        Require(blocked,"Enabled dependencies must not be disabled or uninstalled");
    }
    mods::SetEnabled(root,"Accessory",false);
    mods::Uninstall(root,"TextureA");
    mods::Uninstall(root,"Accessory");
    install("Japanese");
    Require(mods::EnabledLanguagePack(root,"ja")==root/"packages/Japanese","A new language must load by metadata, without a fixed ID");
    conflict = false;
    try { install("OtherJapanese"); } catch (...) { conflict = true; }
    Require(conflict,"Only one provider of a language can be enabled");
    install("Korean");
    Require(mods::EnabledLanguagePack(root,"ko")==root/"packages/Korean","Different installed languages must coexist");
    mods::Uninstall(root,"Japanese");mods::Uninstall(root,"Korean");
    Require(good->mod.version=="1.0.0"&&good->mod.authors[0]=="Tester","Package inspection must read mod.json");
    Require(mods::ReadInstalled(root).empty(),"Inspection must not install a mod");
    last_progress=0;
    mods::InstallPackage(root,*good,original,progress);
    const auto live=root/"languages/LanguagePack_PT-BR";
    Require(Read(live/"notes/new.txt")=="new content","Plain files must install");
    std::string target;for(int i=0;i<40;++i)target+="The translated game data.\n";target+=std::string(100,'!');
    Require(Read(live/"fonts/test.xpr")==target,"Original data must be patched before publication");
    Require(mods::EnabledLanguagePack(root,"pt-BR")==live,"Installed pack must be enabled");
    mods::SetEnabled(root,"LanguagePack_PT-BR",false);
    Require(mods::EnabledLanguagePack(root,"pt-BR").empty()&&fs::exists(live),"Disabling must keep files and prevent mounting");
    mods::SetEnabled(root,"LanguagePack_PT-BR",true);
    auto bad=mods::InspectPackage(base/"bad-hash.tar.xz",root);bool rejected=false;
    try{mods::InstallPackage(root,*bad,original);}catch(...){rejected=true;}
    Require(rejected&&mods::ReadInstalled(root)[0].mod.version=="1.0.0"&&Read(live/"fonts/test.xpr")==target,"Failed update must preserve previous registry and files");
    for(const auto* name:{"traversal.tar.xz","extra.tar.xz","bad-xz.tar.xz","unfinished-tar.tar.xz","both-metadata.tar.xz","large-metadata.tar.xz"}){
        rejected=false;try{mods::InspectPackage(base/name,root);}catch(...){rejected=true;}
        Require(rejected,"Unsafe or corrupt archives must be rejected");
    }
    std::string metadata_error;
    try{mods::InspectPackage(base/"large-metadata.tar.xz",root);}catch(const std::exception& e){metadata_error=e.what();}
    Require(metadata_error.find("extraction limits")!=std::string::npos,"mod.json must retain the metadata extraction size limit");
    std::string incomplete_error;
    try{mods::InspectPackage(base/"unfinished-tar.tar.xz",root);}catch(const std::exception& e){incomplete_error=e.what();}
    Require(incomplete_error.find("incomplete")!=std::string::npos&&incomplete_error.find("Download")!=std::string::npos,
            "An unfinished TAR must explain how to obtain a complete package");
    rejected=false;try{mods::InstallPackage(root,*good,original,[](auto,uint64_t,uint64_t){return false;});}catch(...){rejected=true;}
    Require(rejected&&Read(live/"notes/new.txt")=="new content","Cancellation must preserve the live mod");
    const auto registry_before_cancel=Read(root/"installed.toml");
    rejected=false;
    try{mods::InstallPackage(root,*good,original,[](auto stage,uint64_t,uint64_t){return stage!="Finishing installation";});}catch(...){rejected=true;}
    Require(rejected&&Read(root/"installed.toml")==registry_before_cancel&&Read(live/"fonts/test.xpr")==target,
            "Cancellation after preparing all files must preserve the live mod and registry");
#ifdef _WIN32
    const auto held=CreateFileW((live/"fonts/test.xpr").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Require(held!=INVALID_HANDLE_VALUE,"Fixture must hold the installed file open");
    rejected=false;try{mods::InstallPackage(root,*good,original);}catch(...){rejected=true;}
    CloseHandle(held);
    Require(rejected&&Read(live/"fonts/test.xpr")==target,"A file in use must prevent replacement and preserve the old mod");
#endif
    // Simulate a process dying after the old directory was parked.
    const auto previous=Read(root/"installed.toml");fs::rename(live,root/".backup-test");
    std::ostringstream journal;journal<<toml::table{{"destination","languages/LanguagePack_PT-BR"},{"backup",".backup-test"},{"existed",true},{"previous",previous},{"next","uncommitted state"}};
    files::AtomicWrite(root/"transaction.toml",journal.str());mods::Recover(root);
    Require(Read(live/"fonts/test.xpr")==target&&!fs::exists(root/"transaction.toml"),"Interrupted update must restore old mod");
    mods::Uninstall(root,"LanguagePack_PT-BR");
    Require(mods::ReadInstalled(root).empty()&&!fs::exists(live),"Uninstall must remove only the selected mod");
    Require(mods::CompareVersions("1.0.0-beta.2","1.0.0-beta.11")<0&&mods::CompareVersions("1.0.0","1.0.0-beta.11")>0,"Version comparison must follow numeric prerelease order");
    std::cout<<"Mod manager checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
