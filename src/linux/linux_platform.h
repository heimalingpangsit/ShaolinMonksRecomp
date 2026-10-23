#pragma once
// Linux services the shared code reaches for where Windows uses shell and
// known-folder APIs. Implemented with POSIX calls and SDL3.
#include <filesystem>
#include <string>
#include <vector>

namespace linux_platform {

// main() stores the command line; Configure() reads it like CommandLineToArgvW.
void SetArguments(int argc, char** argv);
std::vector<std::wstring> Arguments();

std::filesystem::path ExecutablePath();
// $XDG_DATA_HOME, or ~/.local/share
std::filesystem::path DataHome();

// A modal message box. Falls back to stderr when no display is available.
void ShowError(const char* title, const std::string& text);
// Opens a folder in the desktop's file manager without waiting for it.
void OpenFolder(const std::filesystem::path& folder);

// A native file chooser: the desktop portal through SDL, or zenity/kdialog.
// folder chooses a directory. Returns false when cancelled or unavailable.
bool ChooseFile(std::filesystem::path& selected, bool folder, const std::string& title);

}  // namespace linux_platform
