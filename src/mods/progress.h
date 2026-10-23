#pragma once
#include <functional>
#include <string_view>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
namespace shaolin::mods {
using Progress=std::function<bool(std::string_view stage,uint64_t done,uint64_t total)>;
inline constexpr uint64_t ProgressTotal=10000;
// Nested stages use the same scale, so byte and file counters never reset the operation.
inline Progress ProgressSlice(const Progress& progress,uint64_t begin,uint64_t end) {
    if(!progress)return {};
    return [progress,begin,end](std::string_view stage,uint64_t done,uint64_t total) {
        const auto amount=total?uint64_t((end-begin)*(static_cast<long double>(std::min(done,total))/total)):0;
        return progress(stage,begin+amount,ProgressTotal);
    };
}
inline void Report(const Progress& p,std::string_view stage,uint64_t done=0,uint64_t total=0) {
    if(p && !p(stage,done,total)) throw std::runtime_error("Operation cancelled");
}
}
