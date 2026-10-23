#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>
#else
#include <SDL3/SDL.h>
#include <imgui.h>
#include "linux/linux_launcher_window.h"
#include "linux/linux_platform.h"
#endif
#include <cstdio>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <fstream>
#include <vector>
#include <SDL3/SDL_gamepad.h>
#include "launcher.h"
#include "launcher_settings.h"
#include "launcher_ids.h"
#include "app/app.h"
#include "controller.h"
#include "mouse.h"
#include "core/log.h"
#include "game_setup.h"
#include "host.h"
#include "performance_overlay.h"
#include "runtime_paths.h"
#include "language_pack.h"
#include "mods/browser.h"
#include "mods/languages.h"

namespace fs = std::filesystem;
namespace shaolin::launcher {
namespace {
Settings current;
std::atomic<bool> persist{true};
std::wstring Wide(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())).wstring(); }
std::string Narrow(const std::wstring& text) { auto bytes = fs::path(text).u8string(); return {bytes.begin(), bytes.end()}; }
toml::table Disk() {
    std::ifstream file(paths::Config(), std::ios::binary);
    return file ? toml::parse(file) : toml::table{};
}
void Apply(const Settings& s, bool save) {
    current = s; persist = save;
    auto& host = app::Get();
    host.display_mode = s.display_mode == "fullscreen" ? app::DisplayMode::Fullscreen :
                        s.display_mode == "windowed" ? app::DisplayMode::Windowed : app::DisplayMode::Borderless;
    host.width = s.width; host.height = s.height; host.render_percent = s.render_percent;
    host.fps_cap = FrameRate; host.vsync = s.vsync; host.aniso = s.aniso;
    auto table = Merge({}, s);
    controller::Load(&table);
    mouse::Configure(s.mouse);
    performance::SetVisible(s.show_fps);
}
struct LanguageRequest {std::string code;bool started=false;};
bool NeedsLanguagePack(std::string_view code) {
    return !mods::IsOriginalLanguage(code)&&options.language_pack.empty()&&language::FindInstalledPack(paths::Mods(),code).empty();
}
const mods::Installed* InstalledLanguagePack(const mods::BrowserState& view,std::string_view code) {
    const auto it=std::find_if(view.installed.begin(),view.installed.end(),[&](auto& row){
        return row.mod.type=="language"&&mods::SameLanguage(row.mod.language,code);
    });
    return it==view.installed.end()?nullptr:&*it;
}
std::string LanguagePackQuestion(const mods::BrowserState& view,std::string_view code) {
    const auto choices=mods::LanguageChoices(view,code);
    const auto choice=std::find_if(choices.begin(),choices.end(),[&](auto& row){return mods::SameLanguage(row.code,code);});
    const auto name=choice==choices.end()?std::string(code):choice->name;
    return name+" needs a language pack. "+(InstalledLanguagePack(view,code)?"Enable it now?":"Download, install and enable it now?");
}
template<typename ChooseGame>
void ContinueLanguageInstall(mods::Browser& browser,LanguageRequest& request,ChooseGame choose_game) {
    const auto view=browser.View();
    if(request.code.empty()||view.busy)return;
    if(request.started&&!view.error.empty()){request={};return;}
    const auto* installed=InstalledLanguagePack(view,request.code);
    if(installed&&fs::is_directory(paths::Mods()/installed->mod.install_dir)) {
        if(!installed->enabled)browser.Enable(installed->mod.id,true);
        request={};return;
    }
    if(request.started){request={};return;}
    const auto id=mods::LanguageProvider(view,request.code);
    const auto mod=std::find_if(view.catalog.begin(),view.catalog.end(),[&](auto& row){return row.id==id;});
    if(mod==view.catalog.end()||!mods::LatestCompatible(*mod)) {
        browser.Install(id,{});request={};return;
    }
    const auto game=choose_game();
    if(game.empty()){request={};return;}
    browser.Install(id,game);request.started=true;
}
#ifdef _WIN32
enum Id { Mode = 2301, Width, Height, Scale, Aniso = 2306, Vsync, Volume = 2401, Overlay,
          Deadzone = 2501, Rumble, Binding = 2600, Source = 2701, Browse, Saves, Logs, Data,
          Original = 2801, Music, Effects, Speech, Brightness, Rounds, MatchTime, Camera,
          Sensitivity = 2901, InvertMouse, MouseBinding = 2910,
          ModList = 3001, ModDetails, ModInstall, ModToggle, ModRemove, ModUpdate, ModRefresh, ModFile, ModCancel, ModStatus, ModNotice, ModProgress,
          DataPath = 3101, ChangeData, SavePath, ChangeSaves };
struct Dialog {
    Settings initial;
    fs::path source;
    bool source_changed = false;
    std::vector<std::string> language_codes;
    std::vector<HWND> pages[7];
    fs::path data_folder, save_folder;
    bool folders_saved = false;
    std::unique_ptr<mods::Browser> mods;
    std::vector<mods::Mod> mod_rows;
    uint64_t mod_revision = UINT64_MAX;
    LanguageRequest language_request;
    std::string focus_language_mod;
};
HWND Add(HWND parent, Dialog& d, int page, const wchar_t* kind, const wchar_t* text, int id,
         int x, int y, int width, int height, DWORD style = 0) {
    RECT rect{x, y, x + width, y + height}; MapDialogRect(parent, &rect);
    auto child = CreateWindowExW(kind == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, kind, text,
        WS_CHILD | WS_VISIBLE | style, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        parent, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, SendMessageW(parent, WM_GETFONT, 0, 0), TRUE);
    d.pages[page].push_back(child);
    return child;
}
void Label(HWND w, Dialog& d, int p, const wchar_t* text, int x, int y, int width = 155) {
    Add(w, d, p, L"STATIC", text, -1, x, y + 2, width, 14);
}
void Combo(HWND w, Dialog& d, int p, int id, int x, int y, int width, const std::vector<std::wstring>& choices, bool editable = false) {
    auto child = Add(w, d, p, L"COMBOBOX", L"", id, x, y, width, 180,
        WS_TABSTOP | WS_VSCROLL | (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST));
    for (const auto& choice : choices) SendMessageW(child, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.c_str()));
}
void Field(HWND w, Dialog& d, int p, const wchar_t* label, int id, int y) {
    Label(w, d, p, label, 20, y);
    Add(w, d, p, L"EDIT", L"", id, 185, y, 220, 15, WS_TABSTOP | ES_AUTOHSCROLL);
}
std::wstring Text(HWND window, int id) {
    auto child = GetDlgItem(window, id);
    std::wstring value(GetWindowTextLengthW(child) + 1, L'\0');
    value.resize(GetWindowTextW(child, value.data(), int(value.size())));
    return value;
}
void Set(HWND w, int id, const std::wstring& value) { SetDlgItemTextW(w, id, value.c_str()); }
void Number(HWND w, int id, int value) { Set(w, id, std::to_wstring(value)); }
int Number(HWND w, int id) {
    const auto text = Narrow(Text(w, id));
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() ? value : -1;
}
void Select(HWND w, int id, int index) { SendDlgItemMessageW(w, id, CB_SETCURSEL, index, 0); }
int Selected(HWND w, int id) { return int(SendDlgItemMessageW(w, id, CB_GETCURSEL, 0, 0)); }
void Check(HWND w, int id, bool value) { CheckDlgButton(w, id, value ? BST_CHECKED : BST_UNCHECKED); }
bool Checked(HWND w, int id) { return IsDlgButtonChecked(w, id) == BST_CHECKED; }
void Fill(HWND w, const Settings& s) {
    const auto* dialog = reinterpret_cast<Dialog*>(GetWindowLongPtrW(w, DWLP_USER));
    const auto& choices = dialog->language_codes;
    auto lang = std::find(choices.begin(), choices.end(), s.language);
    Select(w, IDC_LANGUAGE, lang == choices.end() ? 0 : int(lang - choices.begin()));
    Select(w, Mode, s.display_mode == "fullscreen" ? 0 : s.display_mode == "borderless" ? 1 : 2);
    Number(w, Width, s.width); Number(w, Height, s.height); Number(w, Scale, s.render_percent);
    Select(w, Aniso, s.aniso == 16 ? 4 : s.aniso == 8 ? 3 : s.aniso == 4 ? 2 : s.aniso == 2 ? 1 : 0);
    Check(w, Vsync, s.vsync); Number(w, Volume, s.master_volume); Check(w, Overlay, s.show_fps);
    Number(w, Deadzone, int(s.deadzone * 100 + .5)); Check(w, Rumble, s.rumble);
    for (int i = 0; i < 10; ++i) Set(w, Binding + i, Wide(s.bindings[i]));
    Number(w, Sensitivity, s.mouse.sensitivity); Check(w, InvertMouse, s.mouse.invert_y);
    for (int i = 0; i < 5; ++i) {
        const auto action = std::find(mouse::actions.begin(), mouse::actions.end(), s.mouse.bindings[i]);
        Select(w, MouseBinding + i, action == mouse::actions.end() ? 0 : int(action - mouse::actions.begin()));
    }
    Check(w, Original, s.original_override);
    Number(w, Music, s.music_volume); Number(w, Effects, s.effects_volume); Number(w, Speech, s.speech_volume);
    Number(w, Brightness, s.brightness); Select(w, Rounds, s.versus_rounds == 1 ? 0 : 1);
    Select(w, MatchTime, s.versus_time == 120 ? 0 : s.versus_time == 180 ? 1 : 2);
    Select(w, Camera, s.camera_reverse ? 1 : 0);
    for (int id = Music; id <= Camera; ++id) EnableWindow(GetDlgItem(w, id), s.original_override);
}
Settings Form(HWND w) {
    Settings s;
    const auto* state = reinterpret_cast<Dialog*>(GetWindowLongPtrW(w, DWLP_USER));
    const int language = Selected(w, IDC_LANGUAGE);
    s.language = state && language >= 0 && size_t(language) < state->language_codes.size() ? state->language_codes[language] : "";
    s.display_mode = Selected(w, Mode) == 0 ? "fullscreen" : Selected(w, Mode) == 1 ? "borderless" : "windowed";
    s.width = Number(w, Width); s.height = Number(w, Height); s.render_percent = Number(w, Scale);
    const int levels[] = {0, 2, 4, 8, 16}; const int level = Selected(w, Aniso);
    s.aniso = level >= 0 && level < 5 ? levels[level] : -1;
    s.vsync = Checked(w, Vsync); s.master_volume = Number(w, Volume); s.show_fps = Checked(w, Overlay);
    const int zone = Number(w, Deadzone);
    s.deadzone = state && std::isfinite(state->initial.deadzone) && zone == int(state->initial.deadzone * 100 + .5)
        ? state->initial.deadzone : zone / 100.;
    s.rumble = Checked(w, Rumble);
    for (int i = 0; i < 10; ++i) s.bindings[i] = Narrow(Text(w, Binding + i));
    s.mouse.sensitivity = Number(w, Sensitivity); s.mouse.invert_y = Checked(w, InvertMouse);
    for (int i = 0; i < 5; ++i) {
        const int selected = Selected(w, MouseBinding + i);
        s.mouse.bindings[i] = selected >= 0 && selected < int(mouse::actions.size()) ? mouse::actions[selected] : "";
    }
    s.original_override = Checked(w, Original);
    s.music_volume = Number(w, Music); s.effects_volume = Number(w, Effects); s.speech_volume = Number(w, Speech);
    s.brightness = Number(w, Brightness); s.versus_rounds = Selected(w, Rounds) == 0 ? 1 : 3;
    s.versus_time = Selected(w, MatchTime) == 0 ? 120 : Selected(w, MatchTime) == 1 ? 180 : 0;
    s.camera_reverse = Selected(w, Camera) == 1;
    return s;
}
bool Valid(HWND w, const Settings& s) {
    std::string error;
    if (Validate(s, error)) {
        const std::vector<std::string> aliases{"south", "cross", "east", "circle", "west", "square", "north", "triangle"};
        for (const auto& binding : s.bindings)
            if (SDL_GetGamepadButtonFromString(binding.c_str()) == SDL_GAMEPAD_BUTTON_INVALID &&
                std::find(aliases.begin(), aliases.end(), binding) == aliases.end()) {
                error = "Unknown controller button: " + binding; break;
            }
        if (error.empty()) return true;
    }
    MessageBoxW(w, Wide(error).c_str(), L"Check your settings", MB_OK | MB_ICONWARNING);
    return false;
}
bool Commit(HWND w, Dialog& d, const Settings& s) {
    try {
        std::string error;
        if (d.source_changed) {
            setup::Source source; std::vector<uint8_t> executable;
            if (!setup::PrepareGame(d.source, source, executable, error, true) || !setup::RememberDisc(source.path, error))
                throw std::runtime_error(error);
            d.source = source.path; d.source_changed = false;
        }
        if (!Write(paths::Config(), Merge(Disk(), s), error)) throw std::runtime_error(error);
        if (d.data_folder != paths::Root() || d.save_folder != paths::SaveRoot()) {
            if (!paths::ChangeFolders(d.data_folder, d.save_folder, error)) throw std::runtime_error(error);
            d.folders_saved = true;
        }
        return true;
    } catch (const std::exception& e) {
        MessageBoxW(w, Wide(e.what()).c_str(), L"Cannot save settings", MB_OK | MB_ICONERROR);
        return false;
    }
}
void OpenDirectory(const fs::path& path) { ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
bool ChoosePath(HWND owner, fs::path& path, bool folder) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool chosen = false;
    {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
            dialog->SetTitle(folder ? L"Choose an empty destination folder" : L"Install a mod from file");
            const COMDLG_FILTERSPEC filter[] = {{L"Mod archives (*.tar.xz)", L"*.tar.xz"}};
            if (!folder) dialog->SetFileTypes(1, filter);
            if (SUCCEEDED(dialog->Show(owner))) {
                Microsoft::WRL::ComPtr<IShellItem> item; PWSTR value = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) {
                    path = value; CoTaskMemFree(value); chosen = true;
                }
            }
        }
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return chosen;
}
fs::path ModGame(HWND w, Dialog& d) {
    setup::Source source; std::vector<uint8_t> executable; std::string error;
    if (d.source.empty() || !setup::PrepareGame(d.source, source, executable, error, true)) {
        MessageBoxW(w, Wide(error.empty() ? "Select your game in Files and support before installing mods." : error).c_str(), L"Cannot install mod", MB_OK | MB_ICONWARNING);
        return {};
    }
    if (source.path != d.source) { d.source = source.path; d.source_changed = true; Set(w, Source, d.source.wstring()); }
    return source.path;
}
void SelectPage(HWND w, Dialog& d, int selected) {
    TabCtrl_SetCurSel(GetDlgItem(w, IDC_PAGES), selected);
    for (int page = 0; page < 7; ++page)
        for (auto child : d.pages[page]) ShowWindow(child, page == selected ? SW_SHOW : SW_HIDE);
}
void OfferLanguagePack(HWND w, Dialog& d) {
    if (MessageBoxW(w, Wide(LanguagePackQuestion(d.mods->View(),Form(w).language)).c_str(), L"LanguagePack required",
                    MB_YESNO | MB_ICONQUESTION) == IDYES) {
        d.language_request={Form(w).language,false};
        d.focus_language_mod=d.language_request.code;
        SelectPage(w, d, 6);
    }
}
void UpdateMods(HWND w, Dialog& d) {
    ContinueLanguageInstall(*d.mods, d.language_request, [&] { return ModGame(w, d); });
    const auto view = d.mods->View();
    Set(w, ModNotice, view.updates ? L"Mod updates are available. Open the Mods tab." : L"Changes apply when you start the game.");
    std::wstring status = Wide(view.error.empty() ? view.status : view.error);
    if (view.busy && view.total) status += L" (" + std::to_wstring(std::min<uint64_t>(100, view.done * 100 / view.total)) + L"%)";
    Set(w, ModStatus, status);
    const auto progress = GetDlgItem(w, ModProgress);
    const int percent = view.total ? int(std::clamp(double(view.done) / double(view.total), 0., 1.) * 100) : 0;
    SendMessageW(progress, PBM_SETPOS, percent, 0);
    if (d.mod_revision != view.revision) {
        const auto selected_code=Form(w).language;
        const auto choices=mods::LanguageChoices(view,selected_code);
        d.language_codes.clear();SendDlgItemMessageW(w,IDC_LANGUAGE,CB_RESETCONTENT,0,0);
        for(const auto& choice:choices) {
            d.language_codes.push_back(choice.code);
            const auto name=Wide(choice.name);
            SendDlgItemMessageW(w,IDC_LANGUAGE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
        }
        for(size_t i=0;i<d.language_codes.size();++i)if(mods::SameLanguage(d.language_codes[i],selected_code))Select(w,IDC_LANGUAGE,int(i));
        const int selected = int(SendDlgItemMessageW(w, ModList, LB_GETCURSEL, 0, 0));
        d.mod_rows = mods::DisplayMods(view); d.mod_revision = view.revision;
        SendDlgItemMessageW(w, ModList, LB_RESETCONTENT, 0, 0);
        for (const auto& m : d.mod_rows) {
            auto label = Wide(m.name);
            for (const auto& i : view.installed) if (i.mod.id == m.id) label += L"  " + Wide(i.mod.version) + (i.enabled ? L" (enabled)" : L" (disabled)");
            SendDlgItemMessageW(w, ModList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        if (!d.mod_rows.empty()) SendDlgItemMessageW(w, ModList, LB_SETCURSEL, selected >= 0 && size_t(selected) < d.mod_rows.size() ? selected : 0, 0);
    }
    if (!d.focus_language_mod.empty()) {
        for (size_t i = 0; i < d.mod_rows.size(); ++i) if (d.mod_rows[i].type=="language"&&mods::SameLanguage(d.mod_rows[i].language,d.focus_language_mod)) {
            SendDlgItemMessageW(w, ModList, LB_SETCURSEL, i, 0);
            d.focus_language_mod.clear();
            break;
        }
    }
    const int selected = int(SendDlgItemMessageW(w, ModList, LB_GETCURSEL, 0, 0));
    const mods::Mod* mod = selected >= 0 && size_t(selected) < d.mod_rows.size() ? &d.mod_rows[selected] : nullptr;
    const mods::Installed* installed = nullptr;
    if (mod) for (const auto& i : view.installed) if (i.mod.id == mod->id) installed = &i;
    const auto* release = mod ? mods::LatestCompatible(*mod) : nullptr;
    std::string details = mod ? mod->summary : "Install official mods from the Krypt, or choose a local .tar.xz archive.";
    if (mod) { details += "\r\nAuthors: "; for (const auto& a : mod->authors) details += a + " "; details += !mod->unsupported_reason.empty()?"\r\n"+mod->unsupported_reason:release ? "\r\nAvailable version: " + release->version : "\r\nNo published version is available yet."; }
    Set(w, ModDetails, Wide(details));
    EnableWindow(GetDlgItem(w, ModInstall), !view.busy && mod && release);
    Set(w,ModInstall,installed?L"Reinstall":L"Install");
    EnableWindow(GetDlgItem(w, ModToggle), !view.busy && installed);
    Set(w, ModToggle, installed && installed->enabled ? L"Disable" : L"Enable");
    EnableWindow(GetDlgItem(w, ModRemove), !view.busy && installed);
    EnableWindow(GetDlgItem(w, ModUpdate), !view.busy && installed && release && mods::CompareVersions(release->version, installed->mod.version) > 0);
    for (int id : {ModRefresh, ModFile, ChangeData, ChangeSaves}) EnableWindow(GetDlgItem(w, id), !view.busy && (id != ChangeData && id != ChangeSaves || !paths::DataFolderOverridden() && (id != ChangeSaves || options.save_dir.empty() && !options.portable_saves)));
    EnableWindow(GetDlgItem(w, ModCancel), view.busy);
}
INT_PTR CALLBACK Procedure(HWND w, UINT message, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(w, DWLP_USER));
    if (message == WM_INITDIALOG) {
        d = reinterpret_cast<Dialog*>(lp); SetWindowLongPtrW(w, DWLP_USER, lp);
        SendMessageW(w, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1))));
        if (options.configure_only) { Set(w, IDC_SAVE_PLAY, L"Save and close"); Set(w, IDC_PLAY, L"Close"); }
        const wchar_t* names[] = {L"General", L"Video", L"Controls", L"Mouse", L"Original game", L"Files and support", L"Mods"};
        for (int i = 0; i < 7; ++i) { TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(names[i]); TabCtrl_InsertItem(GetDlgItem(w, IDC_PAGES), i, &item); }
        Label(w, *d, 0, L"Game language", 20, 38);
        std::vector<std::wstring> language_names;
        for(const auto& choice:mods::LanguageChoices(d->mods->View(),d->initial.language)) {
            d->language_codes.push_back(choice.code);language_names.push_back(Wide(choice.name));
        }
        Combo(w, *d, 0, IDC_LANGUAGE, 185, 36, 220, language_names);
        Field(w, *d, 0, L"Master volume (%)", Volume, 66);
        Add(w, *d, 0, L"BUTTON", L"Show FPS and frame time (F9)", Overlay, 20, 94, 310, 16, WS_TABSTOP | BS_AUTOCHECKBOX);
        Label(w, *d, 0, L"Original game options", 20, 127, 380);
        Add(w, *d, 0, L"STATIC", L"The Original game tab can set music, effects, speech, brightness, versus rules, and camera control. You can also change them in the game's Options menu.", -1, 20, 147, 384, 44);
        Add(w, *d, 0, L"STATIC", L"Language packs add translated text, voices, and movies. Original language audio depends on the files included with your copy.", -1, 20, 202, 384, 38);
        Label(w, *d, 1, L"Display mode", 20, 38);
        Combo(w, *d, 1, Mode, 185, 36, 220, {L"Fullscreen", L"Borderless", L"Windowed"});
        Field(w, *d, 1, L"Width (0 = desktop)", Width, 66); Field(w, *d, 1, L"Height (0 = desktop)", Height, 91);
        Field(w, *d, 1, L"Render scale (%)", Scale, 116);
        Label(w, *d, 1, L"Anisotropic filtering", 20, 143);
        Combo(w, *d, 1, Aniso, 185, 141, 220, {L"Game default", L"2x", L"4x", L"8x", L"16x"});
        Add(w, *d, 1, L"BUTTON", L"V-Sync", Vsync, 20, 169, 200, 16, WS_TABSTOP | BS_AUTOCHECKBOX);
        Label(w, *d, 2, L"Dead zone (%)", 20, 37, 100);
        Add(w, *d, 2, L"EDIT", L"", Deadzone, 120, 35, 55, 15, WS_TABSTOP | ES_AUTOHSCROLL);
        Add(w, *d, 2, L"BUTTON", L"Vibration", Rumble, 275, 37, 120, 16, WS_TABSTOP | BS_AUTOCHECKBOX);
        const wchar_t* labels[] = {L"A", L"B", L"X", L"Y", L"Black / R1", L"White / L1", L"Start", L"Back", L"Left stick click", L"Right stick click"};
        const std::vector<std::wstring> buttons{L"a", L"b", L"x", L"y", L"leftshoulder", L"rightshoulder", L"start", L"back", L"leftstick", L"rightstick", L"dpup", L"dpdown", L"dpleft", L"dpright", L"guide"};
        for (int i = 0; i < 10; ++i) {
            const int x = i < 5 ? 20 : 225, y = 94 + i % 5 * 26;
            Label(w, *d, 2, labels[i], x, y, 80); Combo(w, *d, 2, Binding + i, x + 80, y, 100, buttons, true);
        }
        Add(w, *d, 2, L"STATIC", L"Keyboard: WASD, arrows, Space, Esc/Ctrl, J, K, Q, C, Shift, E, Enter, Backspace. Controller fields use SDL button names.", -1, 20, 233, 385, 31);
        Field(w, *d, 3, L"Mouse sensitivity (%)", Sensitivity, 36);
        Add(w, *d, 3, L"BUTTON", L"Invert vertical camera movement", InvertMouse, 20, 64, 385, 16, WS_TABSTOP | BS_AUTOCHECKBOX);
        const wchar_t* mouse_labels[] = {L"Left button", L"Right button", L"Middle button", L"Side button 1", L"Side button 2"};
        const std::vector<std::wstring> mouse_actions{L"None", L"A", L"B", L"X", L"Y", L"Black", L"White", L"Left trigger", L"Right trigger", L"Start", L"Back", L"Left stick click", L"Right stick click"};
        for (int i = 0; i < 5; ++i) {
            Label(w, *d, 3, mouse_labels[i], 20, 96 + i * 26);
            Combo(w, *d, 3, MouseBinding + i, 185, 94 + i * 26, 220, mouse_actions);
        }
        Add(w, *d, 3, L"STATIC", L"Move the mouse to control the original camera. F10 releases or captures the pointer. Click inside the game to capture it again.", -1, 20, 233, 385, 31);
        Add(w, *d, 4, L"BUTTON", L"Apply these original game options at startup", Original, 20, 35, 385, 16, WS_TABSTOP | BS_AUTOCHECKBOX);
        Field(w, *d, 4, L"Music volume (%)", Music, 62);
        Field(w, *d, 4, L"Sound effects (%)", Effects, 85);
        Field(w, *d, 4, L"Speech volume (%)", Speech, 108);
        Field(w, *d, 4, L"Brightness (%)", Brightness, 131);
        Label(w, *d, 4, L"Versus rounds", 20, 156);
        Combo(w, *d, 4, Rounds, 185, 154, 220, {L"1", L"3"});
        Label(w, *d, 4, L"Versus match time", 20, 179);
        Combo(w, *d, 4, MatchTime, 185, 177, 220, {L"120 seconds", L"180 seconds", L"Unlimited"});
        Label(w, *d, 4, L"Camera control", 20, 202);
        Combo(w, *d, 4, Camera, 185, 200, 220, {L"Normal", L"Reverse"});
        Add(w, *d, 4, L"STATIC", L"When disabled, the game keeps its own saved options. Brightness zero uses the original neutral adjustment.", -1, 20, 233, 385, 31);
        Label(w, *d, 5, L"Game source", 20, 38);
        Add(w, *d, 5, L"EDIT", L"", Source, 20, 58, 315, 31, ES_READONLY | ES_MULTILINE | ES_AUTOHSCROLL);
        Add(w, *d, 5, L"BUTTON", L"Choose...", Browse, 342, 58, 65, 18, WS_TABSTOP);
        Set(w, Source, d->source.empty() ? L"No game selected" : d->source.wstring());
        Add(w, *d, 5, L"BUTTON", L"Open saves", Saves, 20, 107, 112, 20, WS_TABSTOP);
        Add(w, *d, 5, L"BUTTON", L"Open logs", Logs, 153, 107, 112, 20, WS_TABSTOP);
        Add(w, *d, 5, L"BUTTON", L"Open configuration", Data, 286, 107, 121, 20, WS_TABSTOP);
        Label(w, *d, 5, L"Data folder", 20, 139);
        Add(w, *d, 5, L"EDIT", d->data_folder.c_str(), DataPath, 20, 157, 315, 16, ES_READONLY | ES_AUTOHSCROLL);
        Add(w, *d, 5, L"BUTTON", L"Change...", ChangeData, 342, 156, 65, 18, WS_TABSTOP);
        Label(w, *d, 5, L"Saves folder (contains hdd)", 20, 181, 300);
        Add(w, *d, 5, L"EDIT", d->save_folder.c_str(), SavePath, 20, 199, 315, 16, ES_READONLY | ES_AUTOHSCROLL);
        Add(w, *d, 5, L"BUTTON", L"Change...", ChangeSaves, 342, 198, 65, 18, WS_TABSTOP);
        Add(w, *d, 5, L"STATIC", L"Folder changes move your files after you save, close and reopen the launcher. Command-line folder overrides take priority.", -1, 20, 233, 385, 31);
        Add(w, *d, 6, L"LISTBOX", L"", ModList, 20, 36, 385, 64, WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT);
        Add(w, *d, 6, L"STATIC", L"", ModDetails, 20, 105, 385, 47);
        Add(w, *d, 6, L"BUTTON", L"Install", ModInstall, 20, 158, 86, 19, WS_TABSTOP);
        Add(w, *d, 6, L"BUTTON", L"Enable", ModToggle, 117, 158, 86, 19, WS_TABSTOP);
        Add(w, *d, 6, L"BUTTON", L"Uninstall", ModRemove, 215, 158, 86, 19, WS_TABSTOP);
        Add(w, *d, 6, L"BUTTON", L"Update", ModUpdate, 313, 158, 92, 19, WS_TABSTOP);
        Add(w, *d, 6, L"BUTTON", L"Check for updates", ModRefresh, 20, 187, 122, 19, WS_TABSTOP);
        Add(w, *d, 6, L"BUTTON", L"Install from file...", ModFile, 150, 187, 135, 19, WS_TABSTOP);
        Add(w, *d, 6, L"BUTTON", L"Cancel operation", ModCancel, 293, 187, 112, 19, WS_TABSTOP);
        const auto progress = Add(w, *d, 6, PROGRESS_CLASSW, L"", ModProgress, 20, 217, 385, 10);
        SendMessageW(progress, PBM_SETRANGE32, 0, 100);
        Add(w, *d, 6, L"STATIC", L"", ModStatus, 20, 233, 385, 31);
        RECT notice{10, 289, 430, 302}; MapDialogRect(w, &notice);
        auto notice_window = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, notice.left, notice.top, notice.right-notice.left, notice.bottom-notice.top, w, reinterpret_cast<HMENU>(INT_PTR(ModNotice)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(notice_window, WM_SETFONT, SendMessageW(w, WM_GETFONT, 0, 0), TRUE);
        Fill(w, d->initial);
        for (int page = 1; page < 7; ++page) for (auto child : d->pages[page]) ShowWindow(child, SW_HIDE);
        SetTimer(w, 1, 150, nullptr); UpdateMods(w, *d);
        if (!paths::MoveWarning().empty()) MessageBoxW(w, Wide(paths::MoveWarning()).c_str(), L"Folder move", MB_OK | MB_ICONWARNING);
        return TRUE;
    }
    if (!d) return FALSE;
    if (message == WM_TIMER) { UpdateMods(w, *d); return TRUE; }
    if (message == WM_DESTROY) { KillTimer(w, 1); d->mods->Cancel(); return TRUE; }
    if (message == WM_CTLCOLORSTATIC) {
        const auto child = reinterpret_cast<HWND>(lp);
        wchar_t kind[32]{}; GetClassNameW(child, kind, 32);
        if (std::wstring(kind) == L"Static" || std::wstring(kind) == L"Button") {
            const bool page = std::any_of(std::begin(d->pages), std::end(d->pages), [&](const auto& controls) {
                return std::find(controls.begin(), controls.end(), child) != controls.end();
            });
            if (page) {
                SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
                return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
            }
        }
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lp)->idFrom == IDC_PAGES && reinterpret_cast<NMHDR*>(lp)->code == TCN_SELCHANGE) {
        SelectPage(w, *d, TabCtrl_GetCurSel(GetDlgItem(w, IDC_PAGES)));
        return TRUE;
    }
    if (message == WM_COMMAND || message == WM_CLOSE) {
        const auto id = message == WM_CLOSE ? IDCANCEL : LOWORD(wp);
        if (id == IDC_LANGUAGE && HIWORD(wp) == CBN_SELCHANGE) {
            const auto selected = Selected(w, IDC_LANGUAGE);
            if (selected >= 0 && size_t(selected) < d->language_codes.size() && NeedsLanguagePack(d->language_codes[selected]))
                OfferLanguagePack(w, *d);
            return TRUE;
        }
        if (id == ChangeData || id == ChangeSaves) {
            if (paths::DataFolderOverridden() || id == ChangeSaves && (!options.save_dir.empty() || options.portable_saves)) return TRUE;
            fs::path path; if (ChoosePath(w, path, true)) { if (id == ChangeData) { d->data_folder = path; Set(w, DataPath, path.wstring()); } else { d->save_folder = path; Set(w, SavePath, path.wstring()); } }
            return TRUE;
        }
        if (id == ModRefresh) { d->mods->Refresh(); return TRUE; }
        if (id == ModCancel) { d->language_request = {}; d->mods->Cancel(); return TRUE; }
        if (id == ModFile) { fs::path path; if (ChoosePath(w, path, false)) { const auto game = ModGame(w, *d); if (!game.empty()) d->mods->InstallFile(path, game); } return TRUE; }
        if (id == ModList) { UpdateMods(w, *d); return TRUE; }
        if (id >= ModInstall && id <= ModUpdate) {
            const auto index = int(SendDlgItemMessageW(w, ModList, LB_GETCURSEL, 0, 0));
            if (index < 0 || size_t(index) >= d->mod_rows.size()) return TRUE;
            const auto& mod = d->mod_rows[index]; const auto view = d->mods->View(); if (view.busy) return TRUE;
            if (id == ModInstall || id == ModUpdate) { const auto game = ModGame(w, *d); if (!game.empty()) d->mods->Install(mod.id, game); }
            else if (id == ModToggle) { for (const auto& i : view.installed) if (i.mod.id == mod.id) d->mods->Enable(mod.id, !i.enabled); }
            else if (MessageBoxW(w, (L"Uninstall " + Wide(mod.name) + L"?").c_str(), L"Uninstall mod", MB_YESNO | MB_ICONQUESTION) == IDYES) d->mods->Remove(mod.id);
            return TRUE;
        }
        if (id == IDC_DEFAULTS) { Fill(w, Settings{}); return TRUE; }
        if (id == Original) {
            for (int field = Music; field <= Camera; ++field) EnableWindow(GetDlgItem(w, field), Checked(w, Original));
            return TRUE;
        }
        if (id == Browse) {
            fs::path selected;
            if (setup::ChooseDisc(selected)) { d->source = selected; d->source_changed = true; Set(w, Source, selected.wstring()); }
            return TRUE;
        }
        if (id == Logs) { OpenDirectory(paths::Logs()); return TRUE; }
        if (id == Data) { OpenDirectory(paths::Root()); return TRUE; }
        if (id == Saves) {
            std::string error;
            const auto root = !options.save_dir.empty() ? fs::absolute(options.save_dir) : options.portable_saves ?
                app::ExeDir() / "hdd" : paths::SaveRoot() / "hdd";
            std::error_code ec;
            if (fs::is_directory(root, ec)) OpenDirectory(root);
            else MessageBoxW(w, (L"Save folder:\n" + root.wstring() + L"\n\nThe game creates this folder when it starts.").c_str(), L"Saves", MB_OK);
            return TRUE;
        }
        if (id != IDC_SAVE_PLAY && id != IDC_PLAY && id != IDCANCEL) return FALSE;
        const auto draft = Form(w);
        const bool closing = id == IDCANCEL || options.configure_only;
        const auto operation = d->mods->View();
        if (!closing && operation.busy && !operation.checking) {
            SelectPage(w, *d, 6);
            MessageBoxW(w, L"Wait for the mod operation to finish, or cancel it in the Mods tab.", L"Mod operation in progress", MB_OK);
            return TRUE;
        }
        if (!closing && NeedsLanguagePack(draft.language)) { OfferLanguagePack(w, *d); return TRUE; }
        const bool dirty = draft != d->initial || d->source_changed || d->data_folder != paths::Root() || d->save_folder != paths::SaveRoot();
        int choice = id == IDC_SAVE_PLAY ? IDYES : IDNO;
        if (dirty && id != IDC_SAVE_PLAY) choice = MessageBoxW(w,
            closing ? L"Save your changes before closing?" : L"Save your changes before starting the game?",
            L"Unsaved settings", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (choice == IDCANCEL) return TRUE;
        if (!closing || choice == IDYES) if (!Valid(w, draft)) return TRUE;
        if (choice == IDYES && !Commit(w, *d, draft)) return TRUE;
        if (d->folders_saved) { MessageBoxW(w, L"Folder changes are saved. Reopen the launcher to move your files and use the new folders.", L"Folders saved", MB_OK); EndDialog(w, 0); return TRUE; }
        if (!closing) {
            Apply(draft, choice == IDYES || !dirty);
            if (!d->source.empty()) { options.disc = d->source; options.game.clear(); options.register_disc = d->source_changed && ShouldPersist(); }
        }
        EndDialog(w, closing ? 0 : 1);
        return TRUE;
    }
    return FALSE;
}
}
bool Show() {
    Dialog dialog;
    dialog.source = !options.disc.empty() ? options.disc : !options.game.empty() ? options.game : setup::RememberedDisc();
    std::error_code unavailable;
    if (!options.configure_only && options.disc.empty() && options.game.empty() &&
        (dialog.source.empty() || !fs::exists(dialog.source, unavailable))) {
        fs::path selected;
        if (!setup::ChooseDisc(selected)) return false;
        setup::Source source; std::vector<uint8_t> executable; std::string error;
        if (!setup::PrepareGame(selected, source, executable, error, true) || !setup::RememberDisc(source.path, error)) {
            paths::ReportError(Wide(error)); return false;
        }
        dialog.source = source.path;
        options.disc = source.path;
    }
    try { dialog.initial = Read(Disk()); }
    catch (const std::exception& e) { paths::ReportError(L"Cannot read settings.toml.\n" + Wide(e.what())); return false; }
    dialog.data_folder = paths::Root(); dialog.save_folder = paths::SaveRoot();
    dialog.mods = std::make_unique<mods::Browser>(paths::Mods());
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
    const auto result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_LAUNCHER), nullptr, Procedure, reinterpret_cast<LPARAM>(&dialog));
    if (result == -1) paths::ReportError(L"Cannot open the settings window.");
    return result == 1;
}
#else
// The Linux settings window: the same tabs and settings as the Windows dialog, drawn with Dear ImGui.
// Without a display or Vulkan it is skipped, and the saved settings.toml applies as it is.
struct Draft {
    Settings initial, s;
    fs::path source;
    bool source_changed = false;
    std::vector<std::string> language_codes, language_names;
    enum class Popup { None, Unsaved, Error, RemoveMod, LanguagePack } popup = Popup::None;
    bool popup_opened = false;
    std::string message, message_title;
    enum class Action { None, Play, Close } pending = Action::None;
    int result = -1;  // 0 close, 1 play
    fs::path data_folder, save_folder;
    bool folders_saved = false, close_after_message = false;
    std::unique_ptr<mods::Browser> mods;
    std::string remove_id;
    LanguageRequest language_request;
    bool select_mods_tab=false;
    uint64_t language_revision=UINT64_MAX;
};
void OfferLanguagePack(Draft& d) {
    d.message = LanguagePackQuestion(d.mods->View(),d.s.language);
    d.popup = Draft::Popup::LanguagePack;
    d.popup_opened = false;
}
void Fail(Draft& d, const std::string& title, const std::string& text) {
    d.popup = Draft::Popup::Error; d.popup_opened = false; d.message_title = title; d.message = text;
}
bool Valid(Draft& d, const Settings& s) {
    std::string error;
    if (Validate(s, error)) {
        const std::vector<std::string> aliases{"south", "cross", "east", "circle", "west", "square", "north", "triangle"};
        for (const auto& binding : s.bindings)
            if (SDL_GetGamepadButtonFromString(binding.c_str()) == SDL_GAMEPAD_BUTTON_INVALID &&
                std::find(aliases.begin(), aliases.end(), binding) == aliases.end()) {
                error = "Unknown controller button: " + binding; break;
            }
        if (error.empty()) return true;
    }
    Fail(d, "Check your settings", error);
    return false;
}
bool Commit(Draft& d) {
    try {
        std::string error;
        if (d.source_changed) {
            setup::Source source; std::vector<uint8_t> executable;
            if (!setup::PrepareGame(d.source, source, executable, error, true) || !setup::RememberDisc(source.path, error))
                throw std::runtime_error(error);
            d.source = source.path; d.source_changed = false;
        }
        if (!Write(paths::Config(), Merge(Disk(), d.s), error)) throw std::runtime_error(error);
        if (d.data_folder != paths::Root() || d.save_folder != paths::SaveRoot()) {
            if (!paths::ChangeFolders(d.data_folder, d.save_folder, error)) throw std::runtime_error(error);
            d.folders_saved = true;
        }
        d.initial = d.s;
        return true;
    } catch (const std::exception& e) {
        Fail(d, "Cannot save settings", e.what());
        return false;
    }
}
// the chosen action once the settings are saved or left as they are
void Finish(Draft& d, Draft::Action action, bool saved) {
    if (d.folders_saved) {
        Fail(d, "Folders saved", "Reopen the launcher to move your files and use the new folders.");
        d.close_after_message = true; return;
    }
    if (action == Draft::Action::Play && !options.configure_only) {
        Apply(d.s, saved);
        if (!d.source.empty()) { options.disc = d.source; options.game.clear(); options.register_disc = d.source_changed && ShouldPersist(); }
        d.result = 1;
    } else {
        d.result = 0;
    }
}
// Play, Close and the window's close button; Save and play saves first
void Request(Draft& d, Draft::Action action, bool save) {
    const bool closing = action == Draft::Action::Close || options.configure_only;
    const auto view = d.mods->View();
    if (!closing && view.busy && !view.checking) {
        d.select_mods_tab = true;
        Fail(d, "Mod operation in progress", "Wait for the mod operation to finish, or cancel it in the Mods tab."); return;
    }
    if (!closing && NeedsLanguagePack(d.s.language)) { OfferLanguagePack(d); return; }
    const bool dirty = d.s != d.initial || d.source_changed || d.data_folder != paths::Root() || d.save_folder != paths::SaveRoot();
    if (save) {
        if (!Valid(d, d.s) || !Commit(d)) return;
        Finish(d, action, true);
        return;
    }
    if (dirty) { d.pending = action; d.popup = Draft::Popup::Unsaved; d.popup_opened = false; return; }
    if (!closing && !Valid(d, d.s)) return;
    Finish(d, action, true);
}
float g_label = 0;  // label column width
void Row(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(g_label);
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 16));
}
void Note(const char* text) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
bool Choice(const char* id, int& index, const std::vector<std::string>& names) {
    bool changed = false;
    const char* preview = index >= 0 && size_t(index) < names.size() ? names[size_t(index)].c_str() : "";
    if (ImGui::BeginCombo(id, preview)) {
        for (size_t i = 0; i < names.size(); ++i) {
            const bool selected = int(i) == index;
            if (ImGui::Selectable(names[i].c_str(), selected)) { index = int(i); changed = true; }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}
// one value of a list in the settings, shown by its name
template <typename T>
void Pick(const char* id, T& value, const std::vector<T>& values, const std::vector<std::string>& names) {
    int index = int(std::find(values.begin(), values.end(), value) - values.begin());
    if (Choice(id, index, names) && size_t(index) < values.size()) value = values[size_t(index)];
}
void Number(const char* id, int& value) { ImGui::InputInt(id, &value, 0, 0); }
void Percent(const char* id, int& value, int low, int high) { ImGui::SliderInt(id, &value, low, high, "%d%%", ImGuiSliderFlags_AlwaysClamp); }
void GeneralTab(Draft& d) {
    Settings& s = d.s;
    const auto previous_language = s.language;
    Row("Game language"); Pick("##language", s.language, d.language_codes, d.language_names);
    if (s.language != previous_language && NeedsLanguagePack(s.language)) OfferLanguagePack(d);
    Row("Master volume"); Percent("##volume", s.master_volume, 0, 100);
    ImGui::Checkbox("Show FPS and frame time (F9)", &s.show_fps);
    ImGui::Spacing(); ImGui::SeparatorText("Original game options");
    Note("The Original game tab can set music, effects, speech, brightness, versus rules, and camera control. You can also change them in the game's Options menu.");
    Note("Language packs add translated text, voices, and movies. Original language audio depends on the files included with your copy.");
}
void VideoTab(Draft& d) {
    Settings& s = d.s;
    Row("Display mode"); Pick<std::string>("##mode", s.display_mode, {"fullscreen", "borderless", "windowed"}, {"Fullscreen", "Borderless", "Windowed"});
    Row("Width (0 = desktop)"); Number("##width", s.width);
    Row("Height (0 = desktop)"); Number("##height", s.height);
    Row("Render scale (%)"); Number("##scale", s.render_percent);
    Row("Anisotropic filtering"); Pick<int>("##aniso", s.aniso, {0, 2, 4, 8, 16}, {"Game default", "2x", "4x", "8x", "16x"});
    ImGui::Checkbox("V-Sync", &s.vsync);
}
void ControlsTab(Draft& d) {
    Settings& s = d.s;
    int zone = int(s.deadzone * 100 + .5);
    Row("Dead zone");
    if (ImGui::SliderInt("##deadzone", &zone, 0, 50, "%d%%", ImGuiSliderFlags_AlwaysClamp) && zone != int(d.initial.deadzone * 100 + .5))
        s.deadzone = zone / 100.;
    else if (zone == int(d.initial.deadzone * 100 + .5)) s.deadzone = d.initial.deadzone;
    ImGui::Checkbox("Vibration", &s.rumble);
    ImGui::Spacing(); ImGui::SeparatorText("Controller buttons");
    const char* labels[] = {"A", "B", "X", "Y", "Black / R1", "White / L1", "Start", "Back", "Left stick click", "Right stick click"};
    const std::vector<std::string> buttons{"a", "b", "x", "y", "leftshoulder", "rightshoulder", "start", "back", "leftstick", "rightstick", "dpup", "dpdown", "dpleft", "dpright", "guide"};
    if (ImGui::BeginTable("##buttons", 4, ImGuiTableFlags_SizingStretchProp)) {
        for (int i = 0; i < 10; ++i) {
            if (i % 2 == 0) ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(labels[i]);
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-FLT_MIN);
            std::vector<std::string> names = buttons;
            if (std::find(names.begin(), names.end(), s.bindings[i]) == names.end()) names.push_back(s.bindings[i]);
            ImGui::PushID(i); Pick("##button", s.bindings[i], names, names); ImGui::PopID();
        }
        ImGui::EndTable();
    }
    Note("Keyboard: WASD, arrows, Space, Esc/Ctrl, J, K, Q, C, Shift, E, Enter, Backspace. Controller fields use SDL button names.");
}
void MouseTab(Draft& d) {
    Settings& s = d.s;
    Row("Mouse sensitivity"); Percent("##sensitivity", s.mouse.sensitivity, 1, 300);
    ImGui::Checkbox("Invert vertical camera movement", &s.mouse.invert_y);
    ImGui::Spacing(); ImGui::SeparatorText("Mouse buttons");
    const char* labels[] = {"Left button", "Right button", "Middle button", "Side button 1", "Side button 2"};
    const std::vector<std::string> actions(mouse::actions.begin(), mouse::actions.end());
    const std::vector<std::string> names{"None", "A", "B", "X", "Y", "Black", "White", "Left trigger", "Right trigger", "Start", "Back", "Left stick click", "Right stick click"};
    for (int i = 0; i < 5; ++i) { Row(labels[i]); ImGui::PushID(i); Pick("##action", s.mouse.bindings[i], actions, names); ImGui::PopID(); }
    Note("Move the mouse to control the original camera. F10 releases or captures the pointer. Click inside the game to capture it again.");
}
void OriginalTab(Draft& d) {
    Settings& s = d.s;
    ImGui::Checkbox("Apply these original game options at startup", &s.original_override);
    ImGui::BeginDisabled(!s.original_override);
    Row("Music volume"); Percent("##music", s.music_volume, 0, 100);
    Row("Sound effects"); Percent("##effects", s.effects_volume, 0, 100);
    Row("Speech volume"); Percent("##speech", s.speech_volume, 0, 100);
    Row("Brightness"); Percent("##brightness", s.brightness, 0, 100);
    Row("Versus rounds"); Pick<int>("##rounds", s.versus_rounds, {1, 3}, {"1", "3"});
    Row("Versus match time"); Pick<int>("##time", s.versus_time, {120, 180, 0}, {"120 seconds", "180 seconds", "Unlimited"});
    Row("Camera control"); Pick<bool>("##camera", s.camera_reverse, {false, true}, {"Normal", "Reverse"});
    ImGui::EndDisabled();
    Note("When disabled, the game keeps its own saved options. Brightness zero uses the original neutral adjustment.");
}
void FilesTab(Draft& d) {
    ImGui::TextUnformatted("Game source");
    const std::string source = d.source.empty() ? "No game selected" : Narrow(d.source.wstring());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::BeginChild("##source", ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 2), ImGuiChildFlags_FrameStyle);
    ImGui::TextWrapped("%s", source.c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (ImGui::Button("Choose...")) {
        fs::path selected;
        if (setup::ChooseDisc(selected)) { d.source = selected; d.source_changed = true; }
    }
    ImGui::Spacing(); ImGui::SeparatorText("Folders");
    if (ImGui::Button("Open saves")) {
        std::string error;
        const auto root = !options.save_dir.empty() ? fs::absolute(options.save_dir) : options.portable_saves ?
            app::ExeDir() / "hdd" : paths::SaveRoot() / "hdd";
        std::error_code ec;
        if (fs::is_directory(root, ec)) linux_platform::OpenFolder(root);
        else Fail(d, "Saves", "Save folder:\n" + Narrow(root.wstring()) + "\n\nThe game creates this folder when it starts.");
    }
    ImGui::SameLine(); if (ImGui::Button("Open logs")) linux_platform::OpenFolder(paths::Logs());
    ImGui::SameLine(); if (ImGui::Button("Open configuration")) linux_platform::OpenFolder(paths::Root());
    ImGui::Spacing();
    ImGui::TextWrapped("Data folder: %s", Narrow(d.data_folder.wstring()).c_str());
    const auto view = d.mods->View();
    ImGui::BeginDisabled(paths::DataFolderOverridden() || view.busy);
    if (ImGui::Button("Change data folder...")) {
        fs::path selected;
        if (linux_platform::ChooseFile(selected, true, "Choose an empty data folder")) d.data_folder = selected;
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Saves folder (contains hdd): %s", Narrow(d.save_folder.wstring()).c_str());
    ImGui::BeginDisabled(paths::DataFolderOverridden() || view.busy || !options.save_dir.empty() || options.portable_saves);
    if (ImGui::Button("Change saves folder...")) {
        fs::path selected;
        if (linux_platform::ChooseFile(selected, true, "Choose an empty saves folder")) d.save_folder = selected;
    }
    ImGui::EndDisabled();
    Note("Folder changes move your files after you save, close and reopen the launcher. Command-line folder overrides take priority.");
}
fs::path ModGame(Draft& d) {
    setup::Source source; std::vector<uint8_t> executable; std::string error;
    if (d.source.empty() || !setup::PrepareGame(d.source, source, executable, error, true)) {
        Fail(d, "Cannot install mod", error.empty() ? "Select your game in Files and support before installing mods." : error); return {};
    }
    if (source.path != d.source) { d.source = source.path; d.source_changed = true; }
    return source.path;
}
void ModsTab(Draft& d) {
    const auto view = d.mods->View();
    ImGui::BeginDisabled(view.busy);
    if (ImGui::Button("Check for updates")) d.mods->Refresh();
    ImGui::SameLine();
    if (ImGui::Button("Install from file...")) {
        fs::path file;
        if (linux_platform::ChooseFile(file, false, "Install a mod from file")) { const auto game = ModGame(d); if (!game.empty()) d.mods->InstallFile(file, game); }
    }
    ImGui::EndDisabled();
    if (view.busy) {
        ImGui::SameLine(); if (ImGui::Button("Cancel operation")) { d.language_request = {}; d.mods->Cancel(); }
        const float fraction = view.total ? float(double(view.done) / double(view.total)) : 0.f;
        ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0));
    }
    ImGui::TextWrapped("%s", (view.error.empty() ? view.status : view.error).c_str());
    const auto rows = mods::DisplayMods(view);
    if (rows.empty()) Note("The catalog is unavailable or has no mods. You can install a local .tar.xz archive.");
    for (const auto& mod : rows) {
        ImGui::PushID(mod.id.c_str()); ImGui::Spacing(); ImGui::SeparatorText(mod.name.c_str());
        ImGui::TextWrapped("%s", mod.summary.c_str());
        std::string authors = "Authors: "; for (const auto& a : mod.authors) authors += a + " ";
        ImGui::TextWrapped("%s", authors.c_str());
        const mods::Installed* installed = nullptr;
        for (const auto& i : view.installed) if (i.mod.id == mod.id) installed = &i;
        const auto* version = mods::LatestCompatible(mod);
        if (installed) ImGui::Text("Installed: %s (%s)", installed->mod.version.c_str(), installed->enabled ? "enabled" : "disabled");
        if (version) ImGui::Text("Available: %s", version->version.c_str());
        else Note(mod.unsupported_reason.empty()?"No compatible published version is available.":mod.unsupported_reason.c_str());
        ImGui::BeginDisabled(view.busy);
        if (!installed) {
            ImGui::BeginDisabled(!version);
            if (ImGui::Button("Install")) { const auto game = ModGame(d); if (!game.empty()) d.mods->Install(mod.id, game); }
            ImGui::EndDisabled();
        } else {
            if (ImGui::Button(installed->enabled ? "Disable" : "Enable")) d.mods->Enable(mod.id, !installed->enabled);
            ImGui::SameLine(); if (ImGui::Button("Uninstall")) { d.remove_id = mod.id; d.message = "Uninstall " + mod.name + "?"; d.popup = Draft::Popup::RemoveMod; d.popup_opened = false; }
            ImGui::SameLine(); ImGui::BeginDisabled(!version || mods::CompareVersions(version->version, installed->mod.version) <= 0);
            if (ImGui::Button("Update")) { const auto game = ModGame(d); if (!game.empty()) d.mods->Install(mod.id, game); }
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled(); ImGui::PopID();
    }
}
void Popups(Draft& d) {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const float width = ImGui::GetFontSize() * 24;
    if (d.popup == Draft::Popup::Unsaved) {
        if (!d.popup_opened) { ImGui::OpenPopup("Unsaved settings"); d.popup_opened = true; }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(.5f, .5f));
        ImGui::SetNextWindowSize(ImVec2(width, 0));
        if (ImGui::BeginPopupModal("Unsaved settings", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            const bool closing = d.pending == Draft::Action::Close || options.configure_only;
            ImGui::TextWrapped("%s", closing ? "Save your changes before closing?" : "Save your changes before starting the game?");
            ImGui::Spacing();
            const auto action = d.pending;
            if (ImGui::Button("Save")) {
                ImGui::CloseCurrentPopup(); d.popup = Draft::Popup::None;
                if (closing || Valid(d, d.s)) if (Commit(d)) Finish(d, action, true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't save")) {
                ImGui::CloseCurrentPopup(); d.popup = Draft::Popup::None;
                if (closing || Valid(d, d.s)) Finish(d, action, false);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { ImGui::CloseCurrentPopup(); d.popup = Draft::Popup::None; }
            ImGui::EndPopup();
        }
    } else if (d.popup == Draft::Popup::LanguagePack) {
        if (!d.popup_opened) { ImGui::OpenPopup("LanguagePack required"); d.popup_opened = true; }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(.5f, .5f));
        ImGui::SetNextWindowSize(ImVec2(width, 0));
        if (ImGui::BeginPopupModal("LanguagePack required", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::TextWrapped("%s", d.message.c_str());
            if (ImGui::Button("Yes")) {
                d.language_request={d.s.language,false}; d.select_mods_tab = true;
                ImGui::CloseCurrentPopup(); d.popup = Draft::Popup::None;
            }
            ImGui::SameLine();
            if (ImGui::Button("No") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup(); d.popup = Draft::Popup::None;
            }
            ImGui::EndPopup();
        }
    } else if (d.popup == Draft::Popup::RemoveMod) {
        if (!d.popup_opened) { ImGui::OpenPopup("Confirm mod operation"); d.popup_opened = true; }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(.5f, .5f));
        ImGui::SetNextWindowSize(ImVec2(width, 0));
        if (ImGui::BeginPopupModal("Confirm mod operation", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::TextWrapped("%s", d.message.c_str());
            if (ImGui::Button("Uninstall")) {
                ImGui::CloseCurrentPopup(); d.popup = Draft::Popup::None;
                d.mods->Remove(d.remove_id);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); d.popup = Draft::Popup::None; }
            ImGui::EndPopup();
        }
    } else if (d.popup == Draft::Popup::Error) {
        if (!d.popup_opened) { ImGui::OpenPopup("##message"); d.popup_opened = true; }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(.5f, .5f));
        ImGui::SetNextWindowSize(ImVec2(width, 0));
        if (ImGui::BeginPopupModal("##message", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
            ImGui::TextUnformatted(d.message_title.c_str());
            ImGui::Separator();
            ImGui::TextWrapped("%s", d.message.c_str());
            ImGui::Spacing();
            if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup(); d.popup = Draft::Popup::None;
                if (d.close_after_message) d.result = 0;
            }
            ImGui::EndPopup();
        }
    }
}
void Frame(Draft& d) {
    ContinueLanguageInstall(*d.mods, d.language_request, [&] { return ModGame(d); });
    const auto view = d.mods->View();
    if(d.language_revision!=view.revision) {
        d.language_codes.clear();d.language_names.clear();d.language_revision=view.revision;
        for(const auto& choice:mods::LanguageChoices(view,d.s.language)) {
            d.language_codes.push_back(choice.code);d.language_names.push_back(choice.name);
        }
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus);
    g_label = ImGui::GetFontSize() * 13;
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted("Shaolin Monks Recomp");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(options.configure_only ? "Settings" : "Settings before you play");
    ImGui::PopStyleColor();
    if (view.updates) ImGui::TextUnformatted("Mod updates are available. Open the Mods tab.");
    ImGui::Spacing();
    const float bar = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2;
    ImGui::BeginChild("##pages", ImVec2(0, -bar));
    if (ImGui::BeginTabBar("##tabs")) {
        struct Page { const char* name; void (*draw)(Draft&); };
        const Page pages[] = {{"General", GeneralTab}, {"Video", VideoTab}, {"Controls", ControlsTab},
                              {"Mouse", MouseTab}, {"Original game", OriginalTab}, {"Files and support", FilesTab}, {"Mods", ModsTab}};
        for (const auto& page : pages) {
            const bool select = d.select_mods_tab && page.draw == ModsTab;
            if (ImGui::BeginTabItem(page.name, nullptr, select ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                if (select) d.select_mods_tab = false;
                ImGui::Spacing();
                page.draw(d);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Defaults")) d.s = Settings{};
    const char* play = options.configure_only ? "Close" : "Play";
    const char* save = options.configure_only ? "Save and close" : "Save and play";
    const ImGuiStyle& style = ImGui::GetStyle();
    const float play_w = ImGui::CalcTextSize(play).x + style.FramePadding.x * 2;
    const float save_w = ImGui::CalcTextSize(save).x + style.FramePadding.x * 2;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - play_w - save_w - style.ItemSpacing.x);
    if (ImGui::Button(play)) Request(d, options.configure_only ? Draft::Action::Close : Draft::Action::Play, false);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(save)) Request(d, options.configure_only ? Draft::Action::Close : Draft::Action::Play, true);
    ImGui::PopStyleColor();
    Popups(d);
    ImGui::End();
}
// the saved settings as they are, when no window can open
bool PlayWithSavedSettings() {
    Settings settings;
    try { settings = Read(Disk()); }
    catch (const std::exception& e) { paths::ReportError(L"Cannot read settings.toml.\n" + Wide(e.what())); return false; }
    std::string error;
    if (!Validate(settings, error)) { paths::ReportError(L"Check settings.toml: " + Wide(error)); return false; }
    if (NeedsLanguagePack(settings.language)) {
        paths::ReportError(L"Install and enable a pack for the selected language in Mods, or select another language.");
        return false;
    }
    Apply(settings, true);
    if (options.configure_only) {
        std::printf("Settings: %s\n", Narrow(paths::Config().wstring()).c_str());
        return false;
    }
    return true;
}
}
bool Show() {
    Draft d;
    d.source = !options.disc.empty() ? options.disc : !options.game.empty() ? options.game : setup::RememberedDisc();
    std::error_code unavailable;
    if (!options.configure_only && options.disc.empty() && options.game.empty() &&
        (options.setup || d.source.empty() || !fs::exists(d.source, unavailable))) {
        fs::path selected;
        if (!setup::ChooseDisc(selected)) {
            paths::ReportError(L"Select your copy of Shaolin Monks to play. You can also start the game with --disc path/to/game.iso.");
            return false;
        }
        setup::Source prepared; std::vector<uint8_t> executable; std::string error;
        if (!setup::PrepareGame(selected, prepared, executable, error, true) || !setup::RememberDisc(prepared.path, error)) {
            paths::ReportError(Wide(error)); return false;
        }
        d.source = prepared.path;
        options.disc = prepared.path;
    }
    try { d.initial = Read(Disk()); }
    catch (const std::exception& e) { paths::ReportError(L"Cannot read settings.toml.\n" + Wide(e.what())); return false; }
    d.s = d.initial;
    for(const auto& choice:mods::LanguageChoices({},d.s.language)) {
        d.language_codes.push_back(choice.code);d.language_names.push_back(choice.name);
    }
    d.data_folder = paths::Root(); d.save_folder = paths::SaveRoot();
    linux_launcher::Window window;
    if (!window.Open("Shaolin Monks Recomp", 720, 560)) {
        if (options.disc.empty() && options.game.empty() && !d.source.empty()) options.disc = d.source;
        return PlayWithSavedSettings();
    }
    d.mods = std::make_unique<mods::Browser>(paths::Mods());
    if (!paths::MoveWarning().empty()) Fail(d, "Folder move", paths::MoveWarning());
    while (d.result < 0) {
        if (!window.BeginFrame() && d.popup == Draft::Popup::None) Request(d, Draft::Action::Close, false);
        Frame(d);
        window.EndFrame();
    }
    window.Close();
    return d.result == 1;
}
#endif
bool ShouldPersist() { return persist.load(); }
void Load(const void* raw) {
    const auto& table = *static_cast<const toml::table*>(raw);
    current = Read(table); app::Get().fps_cap = FrameRate;
    controller::Load(raw); mouse::Configure(current.mouse); performance::SetVisible(current.show_fps);
    if (!options.language_pack.empty()) current.language = "pt-BR";
}
void Save(void* raw) {
    auto& table = *static_cast<toml::table*>(raw);
    table.insert_or_assign("language", current.language); table.insert_or_assign("master_volume", current.master_volume);
    table.insert_or_assign("show_fps", performance::Visible()); controller::Save(raw);
    const auto extras = Merge({}, current);
    table.insert_or_assign("game", *extras["game"].as_table());
    table.insert_or_assign("mouse", *extras["mouse"].as_table());
}
bool Persist(const toml::table& table, std::string& error) {
    if (!ShouldPersist()) return true;
    try { return Write(paths::Config(), Merge(Disk(), Read(table)), error); }
    catch (const std::exception& e) { error = e.what(); return false; }
}
unsigned Language() { return GameLanguage(current.language); }
float AudioGain() { return current.master_volume / 100.f; }
const Settings& Current() { return current; }
}
