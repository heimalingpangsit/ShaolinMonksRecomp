#pragma once
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <cstdint>
namespace shaolin::mods {
struct Json {
    using Object=std::map<std::string,Json>;
    using Array=std::vector<Json>;
    std::variant<std::monostate,bool,int64_t,std::string,Array,Object> value;
    static Json Parse(std::string_view text);
    const Json& At(std::string_view key) const;
    bool Has(std::string_view key) const;
    const std::string& Text() const;
    int64_t Number() const;
    const Array& Items() const;
};
}
