// Optional, read-only real-game check. All installed output uses a disposable folder.
#include "mods/game_reader.h"
#include "mods/manager.h"
#include "mods/sha256.h"
#include "file_ops.h"
#include <chrono>
#include <iostream>
namespace fs=std::filesystem;
int main(int argc,char** argv){
    const auto root=fs::temp_directory_path()/("shaolin-mod-game-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        if(argc!=4)throw std::runtime_error("Expected archive, original game source, and finished pack paths");
        using namespace shaolin;
        const auto package=mods::InspectPackage(files::FromUtf8(argv[1]),root);
        mods::GameReader game(files::FromUtf8(argv[2]));
        mods::InstallPackage(root,*package,[&](const mods::Patch& p){return game.Original(p);});
        const auto expected=files::FromUtf8(argv[3]),installed=root/files::FromUtf8(package->mod.install_dir);
        unsigned count=0;
        for(const auto& file:fs::recursive_directory_iterator(expected))if(file.is_regular_file()){
            const auto output=installed/file.path().lexically_relative(expected);
            if(!fs::is_regular_file(output)||mods::Sha256File(output)!=mods::Sha256File(file.path()))throw std::runtime_error("Installed file differs from finished pack: "+files::Utf8(file.path()));
            ++count;
        }
        std::cout<<"Verified "<<count<<" files, including "<<package->mod.patches.size()<<" patches against the original game\n";
        mods::Uninstall(root,package->mod.id);
        fs::remove_all(root);return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::error_code ec;fs::remove_all(root,ec);return 1;}
}
