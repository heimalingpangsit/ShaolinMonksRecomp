#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <shellapi.h>
#else
#include <cstdio>
#include <cstdlib>
#include "linux/linux_platform.h"
#endif
#include <sstream>
#include <atomic>
#include <memory>
#include <toml++/toml.hpp>
#include "runtime_paths.h"
#include "core/log.h"
#include "host.h"
#include "path_preferences.h"
#include "save_storage.h"

namespace fs = std::filesystem;
namespace shaolin::paths {
namespace {
fs::path root, executable_directory;
fs::path save_root, locator;
std::string move_warning;
bool data_overridden = false;
std::unique_ptr<storage::DirectoryLock> data_lock;
bool isolated_run = false;
std::string Utf8(const fs::path& value) {
    const auto bytes = value.u8string();
    return {bytes.begin(), bytes.end()};
}
}
bool Initialize(const fs::path& executable, bool isolated, const fs::path& requested, std::string& error, bool read_only) {
    try {
        executable_directory = fs::absolute(executable);
        isolated_run = isolated;
        data_overridden = isolated || !requested.empty();
        move_warning.clear();
        data_lock.reset();
        if (isolated) root = requested.empty() ? executable_directory / "runtime-data" : fs::absolute(requested);
#ifndef _WIN32
        else root = linux_platform::DataHome() / "Shaolin Monks Recomp";
#else
        else {
            PWSTR value = nullptr;
            const HRESULT status = SHGetKnownFolderPath(FOLDERID_LocalAppData, read_only ? KF_FLAG_DEFAULT : KF_FLAG_CREATE, nullptr, &value);
            if (FAILED(status)) throw std::runtime_error("Windows LocalAppData is unavailable");
            root = fs::path(value) / L"Shaolin Monks Recomp";
            CoTaskMemFree(value);
        }
#endif
        const auto default_data = root;
        std::string save_error;
        save_root = isolated ? root / "saves" : storage::SavedGamesRoot(save_error, false);
        if (save_root.empty()) throw std::runtime_error(save_error);
        locator = default_data / "paths.toml";
        if (!isolated) {
            Locations selected;
            if (!LoadLocations(locator, {default_data, save_root}, selected, move_warning, !read_only && requested.empty()))
                throw std::runtime_error(move_warning);
            root = selected.data; save_root = selected.saves;
        }
        if (!requested.empty()) root = fs::absolute(requested);
        if (read_only) return true;
        for (const auto& directory : {root, Logs(), Cache(), Crashes()}) fs::create_directories(directory);
        if (!isolated) {
            data_lock = std::make_unique<storage::DirectoryLock>();
            if (!data_lock->Acquire(root, error)) { data_lock.reset(); return false; }
        }
        if (!fs::exists(Config())) {
            const auto legacy = executable_directory / "shaolin-recomp.toml";
            if (fs::is_regular_file(legacy)) {
                // Never replace destination settings or remove the original configuration.
                fs::copy_file(legacy, Config(), fs::copy_options::none);
            }
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
const fs::path& Root() { return root; }
fs::path Config() { return root / "settings.toml"; }
fs::path Logs() { return root / "logs"; }
fs::path Cache() { return root / "cache"; }
fs::path Crashes() { return root / "crashes"; }
fs::path Captures() { return isolated_run ? executable_directory : Cache(); }
fs::path Selection() { return root / "game-source.toml"; }
fs::path Mods() { return root / "mods"; }
const fs::path& SaveRoot() { return save_root; }
const std::string& MoveWarning() { return move_warning; }
bool DataFolderOverridden() { return data_overridden; }
bool ChangeFolders(const fs::path& data, const fs::path& saves, std::string& error) {
    if (data_overridden) { error = "Folder preferences are overridden by this launch's command-line options"; return false; }
    return ScheduleLocations(locator, {root, save_root}, {data, saves}, error);
}
std::string Description() {
    toml::table values{{"data", Utf8(Root())}, {"config", Utf8(Config())},
                       {"logs", Utf8(Logs())}, {"cache", Utf8(Cache())}, {"crashes", Utf8(Crashes())},
                       {"mods", Utf8(Mods())}, {"saves", Utf8(SaveRoot())}};
    std::ostringstream stream;
    stream << toml::json_formatter{values};
    return stream.str();
}
std::wstring ErrorText(const std::wstring& reason, const fs::path& dump) {
    std::wstring message = reason;
    const auto& log = logx::Path();
    if (log.empty()) message += L"\n\nThe session log is unavailable.";
    else message += L"\n\nLog:\n" + fs::path(std::u8string(log.begin(), log.end())).wstring();
    if (!dump.empty()) message += L"\n\nCrash report:\n" + dump.wstring();
    message += L"\n\nApplication data:\n" + Root().wstring();
    message += L"\n\nClose this message to open the application data folder.\nInclude the log when you report this problem.";
    return message;
}
void ReportError(const std::wstring& reason, const fs::path& dump) {
    static std::atomic_flag displayed = ATOMIC_FLAG_INIT;
    if (options.background || displayed.test_and_set()) return;
    const auto message = ErrorText(reason, dump);
#ifndef _WIN32
    const auto text = Utf8(fs::path(message));
    std::fprintf(stderr, "%s\n", text.c_str());
    linux_platform::ShowError("Shaolin Monks Recomp", text);
    logx::Flush();
    LOGR("Error dialog closed. Application data folder: %s", Utf8(Root()).c_str());
    linux_platform::OpenFolder(Root());
    return;
#else
    // A diagnostic on a private desktop must not open Explorer on the user's desktop.
    wchar_t current[256]{}, input[256]{}; DWORD needed = 0;
    const auto desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    const bool interactive = desktop &&
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, current, sizeof(current), &needed) &&
        GetUserObjectInformationW(desktop, UOI_NAME, input, sizeof(input), &needed) && std::wstring(current) == input;
    if (desktop) CloseDesktop(desktop);
    DWORD foreground_process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
    if (interactive && foreground_process == GetCurrentProcessId()) ClipCursor(nullptr);
    MessageBoxW(nullptr, message.c_str(), L"Shaolin Monks Recomp", MB_OK | MB_ICONERROR);
    logx::Flush();
    LOGR("Error dialog closed. Application data folder: %s", Utf8(Root()).c_str());
    if (interactive) {
        ShellExecuteW(nullptr, L"open", Root().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
#endif
}
}
