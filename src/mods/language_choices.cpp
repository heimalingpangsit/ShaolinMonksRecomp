#include "browser.h"
#include "languages.h"
#include "version.h"
#include <algorithm>
namespace shaolin::mods {
const Release* LatestCompatible(const Mod& mod) {
    if(!mod.unsupported_reason.empty())return nullptr;
    for(const auto& release:mod.versions)if(CompareVersions(release.recomp,RecompVersion)<=0)return &release;
    return nullptr;
}
std::string LanguageProvider(const BrowserState& state,std::string_view code) {
    for(const bool enabled:{true,false})for(const auto& row:state.installed)
        if(row.enabled==enabled&&row.mod.type=="language"&&SameLanguage(row.mod.language,code))return row.mod.id;
    for(const auto& mod:state.catalog)
        if(mod.type=="language"&&SameLanguage(mod.language,code)&&LatestCompatible(mod))return mod.id;
    return {};
}
std::vector<LanguageChoice> LanguageChoices(const BrowserState& state,std::string_view selected) {
    std::vector<LanguageChoice> choices{{"en","English"},{"fr","French"},{"de","German"},{"es","Spanish"},
        {"it","Italian"},{"pt-BR","Brazilian Portuguese"}};
    const auto add=[&](const Mod& mod) {
        if(mod.type!="language"||!mod.unsupported_reason.empty())return;
        const auto it=std::find_if(choices.begin(),choices.end(),[&](auto& c){return SameLanguage(c.code,mod.language);});
        if(it==choices.end())choices.push_back({mod.language,mod.language_name.empty()?mod.language:mod.language_name});
        else if(!mod.language_name.empty()&&!IsOriginalLanguage(mod.language))it->name=mod.language_name;
    };
    for(const auto& mod:state.catalog)if(LatestCompatible(mod))add(mod);
    for(const auto& row:state.installed)add(row.mod);
    if(!selected.empty()&&std::none_of(choices.begin(),choices.end(),[&](auto& c){return SameLanguage(c.code,selected);}))
        choices.push_back({std::string(selected),std::string(selected)});
    return choices;
}
}
