#pragma once
#include "manager.h"
#include <atomic>
#include <mutex>
#include <thread>
namespace shaolin::mods {
struct BrowserState {
    std::vector<Mod> catalog;
    std::vector<Installed> installed;
    std::string status,error;
    uint64_t done=0,total=0,revision=0;
    bool busy=false,updates=false,checking=false;
};
struct LanguageChoice {std::string code,name;};
std::vector<LanguageChoice> LanguageChoices(const BrowserState& state,std::string_view selected={});
std::string LanguageProvider(const BrowserState& state,std::string_view code);
class Browser {
    std::filesystem::path root_;
    mutable std::mutex mutex_;
    BrowserState state_;
    std::atomic<bool> cancel_{false};
    std::jthread worker_;
    using Job=std::function<void()>;
    void Start(const Job& job,bool checking=false);
    Progress ProgressCallback();
    void Reload();
public:
    explicit Browser(const std::filesystem::path& root);
    ~Browser();
    BrowserState View() const;
    void Refresh();
    void InstallFile(const std::filesystem::path& file,const std::filesystem::path& game);
    void Install(std::string id,const std::filesystem::path& game);
    void Enable(std::string id,bool enabled);
    void Remove(std::string id);
    void Cancel();
};
std::vector<Mod> DisplayMods(const BrowserState& state);
const Release* LatestCompatible(const Mod& mod);
}
