#pragma once
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
namespace shaolin {
inline std::filesystem::path UserDataHome() {
    const char* value=std::getenv("XDG_DATA_HOME");
    if(value&&*value=='/') {const std::string s=value;return std::filesystem::path(std::u8string(s.begin(),s.end()));}
    value=std::getenv("HOME");if(!value||!*value)throw std::runtime_error("HOME is not set");
    const std::string s=value;return std::filesystem::path(std::u8string(s.begin(),s.end()))/".local/share";
}
}
