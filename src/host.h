#pragma once
#include <filesystem>
#include "save_storage.h"

namespace shaolin {
struct Options {
    bool background = false;
    bool info = false;
    bool host_test = false;
    bool portable_saves = false;
    bool setup = false;
    bool no_launcher = false;
    bool configure_only = false;
    bool paths = false;
    bool validate_disc = false;
    bool register_disc = false;
    bool validate_language_pack = false;
    unsigned virtual_gamepads = 0; // isolated hidden diagnostics only
    unsigned timeout = 0;
    std::filesystem::path game;
    std::filesystem::path disc;
    std::filesystem::path save_dir;
    std::filesystem::path data_dir;
    std::filesystem::path language_pack;
};
extern Options options;
// Returns 0 to start, 1 after --info, and 2 for invalid arguments.
int Configure();
void StartWatchdog();
int CheckHiddenWindow(void* window, bool display_changed);
bool OpenSaveDirectory(const std::wstring& name, std::filesystem::path& result, storage::DirectoryLock& lock);
}
