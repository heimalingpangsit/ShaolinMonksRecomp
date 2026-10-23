#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <commctrl.h>
#include <wrl/client.h>
#else
#include <unistd.h>
#include "linux/linux_platform.h"
#endif
#include "platform_compat.h"
#include <fstream>
#include <cwctype>
#include <toml++/toml.hpp>
#include "game_setup.h"
#include "runtime_paths.h"

namespace fs = std::filesystem;
namespace shaolin::setup {
fs::path RememberedDisc() {
    try {
        std::ifstream file(paths::Selection(), std::ios::binary);
        if (!file) return {};
        const auto data = toml::parse(file);
        const auto value = data["disc"].value<std::string>();
        if (value && !value->empty()) return fs::path(std::u8string(value->begin(), value->end()));
    } catch (...) {}
    return {};
}
bool RememberDisc(const fs::path& disc, std::string& error) {
    fs::path temporary;
    try {
        const auto value = fs::absolute(disc).u8string();
        const toml::table data{{"disc", std::string(value.begin(), value.end())}};
        temporary = paths::Selection();
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << data;
        file.flush();
        if (!file) throw std::runtime_error("Cannot write the selected game path");
        file.close();
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), paths::Selection().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot save the selected game path");
#else
        std::error_code replaced;
        fs::rename(temporary, paths::Selection(), replaced);
        if (replaced) throw std::runtime_error("Cannot save the selected game path");
#endif
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        std::error_code ignored;
        if (!temporary.empty()) fs::remove(temporary, ignored);
        return false;
    }
}
#ifndef _WIN32
bool ChooseDisc(fs::path& disc) {
    // Selecting default.xbe inside an extracted folder covers the folder case with one chooser.
    return linux_platform::ChooseFile(disc, false, "Shaolin Monks Recomp - Select your game");
}
bool PrepareGame(const fs::path& input, Source& source, std::vector<uint8_t>& executable,
                 std::string& error, bool) {
    return PrepareSource(input, source, executable, error);
}
#else
bool ChooseDisc(fs::path& disc) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool selected = false;
    {
        const TASKDIALOG_BUTTON buttons[] = {
            {100, L"Choose a game file\nISO, XISO, ZIP, or default.xbe"},
            {101, L"Choose an extracted game folder\nThe complete folder that contains default.xbe"}
        };
        TASKDIALOGCONFIG welcome{sizeof(welcome)};
        welcome.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
        welcome.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        welcome.pszWindowTitle = L"Shaolin Monks Recomp";
        welcome.hInstance = GetModuleHandleW(nullptr);
        welcome.pszMainIcon = MAKEINTRESOURCEW(1);
        welcome.pszMainInstruction = L"Select your copy of Shaolin Monks";
        welcome.pszContent = L"Use the original USA/Europe Xbox release.\n\nISO and XISO images are read directly. ZIP files are imported into your application data folder. Extracted game folders are used in place.";
        welcome.pButtons = buttons;
        welcome.cButtons = 2;
        welcome.nDefaultButton = 100;
        int choice = 0;
        if (FAILED(TaskDialogIndirect(&welcome, &choice, nullptr, nullptr)) || (choice != 100 && choice != 101)) {
            if (SUCCEEDED(initialized)) CoUninitialize();
            return false;
        }
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            const COMDLG_FILTERSPEC filters[] = {{L"Xbox game files (*.iso; *.xiso; *.zip; default.xbe)", L"*.iso;*.xiso;*.zip;default.xbe"},
                                                {L"All files", L"*.*"}};
            dialog->SetTitle(L"Shaolin Monks Recomp - Select your game");
            dialog->SetFileTypes(2, filters);
            dialog->SetOptions(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_DONTADDTORECENT |
                               (choice == 101 ? FOS_PICKFOLDERS : 0));
            if (SUCCEEDED(dialog->Show(nullptr))) {
                Microsoft::WRL::ComPtr<IShellItem> item;
                PWSTR value = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) {
                    disc = value;
                    CoTaskMemFree(value);
                    selected = true;
                }
            }
        }
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return selected;
}
bool PrepareGame(const fs::path& input, Source& source, std::vector<uint8_t>& executable,
                 std::string& error, bool show_progress) {
    auto extension = input.extension().wstring();
    for (auto& c : extension) c = std::towlower(c);
    if (!show_progress || extension != L".zip") return PrepareSource(input, source, executable, error);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool result;
    {
        Microsoft::WRL::ComPtr<IProgressDialog> progress;
        if (SUCCEEDED(CoCreateInstance(CLSID_ProgressDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&progress)))) {
            progress->SetTitle(L"Shaolin Monks Recomp");
            progress->SetLine(1, L"Importing your game files...", FALSE, nullptr);
            progress->SetCancelMsg(L"Cancelling game import...", nullptr);
            progress->StartProgressDialog(nullptr, nullptr, PROGDLG_AUTOTIME | PROGDLG_NOMINIMIZE, nullptr);
        }
        result = PrepareSource(input, source, executable, error, [&](uint64_t done, uint64_t total) {
            if (!progress) return true;
            progress->SetProgress64(done, total);
            return !progress->HasUserCancelled();
        });
        if (progress) progress->StopProgressDialog();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
}
#endif
}
