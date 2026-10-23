#pragma once
#include <filesystem>
#include <string>

namespace shaolin::paths {
bool Initialize(const std::filesystem::path& executable, bool isolated,
                const std::filesystem::path& requested, std::string& error, bool read_only = false);
const std::filesystem::path& Root();
std::filesystem::path Config();
std::filesystem::path Logs();
std::filesystem::path Cache();
std::filesystem::path Crashes();
std::filesystem::path Captures();
std::filesystem::path Selection();
std::filesystem::path Mods();
const std::filesystem::path& SaveRoot();
const std::string& MoveWarning();
bool ChangeFolders(const std::filesystem::path& data, const std::filesystem::path& saves, std::string& error);
bool DataFolderOverridden();
std::string Description();
std::wstring ErrorText(const std::wstring& reason, const std::filesystem::path& dump = {});
void ReportError(const std::wstring& reason, const std::filesystem::path& dump = {});
}
