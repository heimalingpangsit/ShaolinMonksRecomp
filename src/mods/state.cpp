#include "manager.h"
#include "languages.h"
#include "../file_ops.h"
#include <toml++/toml.hpp>
namespace fs=std::filesystem;
namespace shaolin::mods {
std::vector<Installed> ReadInstalled(const fs::path& root) {
    std::vector<Installed> installed;
    if(!fs::exists(root/"installed.toml")) return installed;
    const auto table=toml::parse_file(files::Utf8(root/"installed.toml"));
    const auto* rows=table["mods"].as_array();
    if(!rows)throw std::runtime_error("Invalid installed mod registry");
    for(const auto& node:*rows) {
        const auto* table=node.as_table();if(!table)throw std::runtime_error("Invalid installed mod entry");const auto& row=*table;
        Installed m{ParseManifest(row["manifest"].value_or(std::string{})),row["enabled"].value_or(false)};
        if(row["id"].value_or(std::string{})!=m.mod.id||row["version"].value_or(std::string{})!=m.mod.version) throw std::runtime_error("Inconsistent installed mod registry");
        for(const auto& other:installed) if(other.mod.id==m.mod.id)throw std::runtime_error("Duplicate installed mod id");
        installed.push_back(std::move(m));
    }
    return installed;
}
fs::path EnabledLanguagePack(const fs::path& root,std::string_view language) {
    try {
        if(!ValidLanguageCode(language)||fs::exists(root/"transaction.toml"))return {};
        fs::path selected;
        for(const auto& m:ReadInstalled(root)) if(m.enabled&&m.mod.type=="language"&&SameLanguage(m.mod.language,language)) {
            const auto path=root/files::FromUtf8(m.mod.install_dir);
            if(files::Within(path,root)&&fs::is_directory(path)) {if(!selected.empty())return {};selected=path;}
        }
        return selected;
    }catch(...){}
    return {};
}
}
