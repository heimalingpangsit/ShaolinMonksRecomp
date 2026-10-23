#pragma once
#include <array>
#include <string_view>
namespace shaolin::mods {
inline constexpr std::array<std::string_view, 5> OriginalLanguages{"en", "fr", "de", "es", "it"};
inline bool SameLanguage(std::string_view a,std::string_view b) {
    if(a.size()!=b.size())return false;
    const auto lower=[](char c){return c>='A'&&c<='Z'?char(c+'a'-'A'):c;};
    for(size_t i=0;i<a.size();++i)if(lower(a[i])!=lower(b[i]))return false;
    return true;
}
inline bool IsOriginalLanguage(std::string_view code) {
    for (const auto original : OriginalLanguages) if (SameLanguage(code,original)) return true;
    return false;
}
inline bool ValidLanguageCode(std::string_view code) {
    if (code.size() < 2 || code.size() > 35) return false;
    size_t count = 0, group = 0;
    for (const auto c : code) {
        if (c == '-') {
            if (!count || count > 8 || (!group && count < 2)) return false;
            ++group; count = 0;
        } else {
            const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            if (!alpha && !(group && c >= '0' && c <= '9')) return false;
            ++count;
        }
    }
    return count > 0 && count <= 8 && (group || count >= 2);
}
}
