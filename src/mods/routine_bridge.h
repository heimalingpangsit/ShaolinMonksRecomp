#pragma once
#include "cpu/x86_ops.h"
#include <atomic>
#include <string_view>
namespace shaolin::mods {
struct RoutineRoute {uint32_t limit;const uint8_t* flags;};
extern std::atomic<const RoutineRoute*> CurrentRoutines;
void InvokeRouted(uint32_t address,X86Context& ctx,uint8_t* base,X86Func* original);
constexpr uint32_t FunctionAddress(std::string_view name) {
    uint32_t address=0;
    for(size_t i=4;i<name.size();++i)address=(address<<4)|uint32_t(name[i]<='9'?name[i]-'0':name[i]-'A'+10);
    return address;
}
inline void RunRoutine(uint32_t address,X86Context& ctx,uint8_t* base,X86Func* original) {
    const auto* route=CurrentRoutines.load(std::memory_order_acquire);
    if(route&&address<route->limit&&route->flags[address])InvokeRouted(address,ctx,base,original);
    else original(ctx,base);
}
}
// Only generated translation units include this header. Strong host overrides
// retain the SDK definitions and are excluded from mod hook targets.
#undef X86_WEAK_FUNC
#define X86_WEAK_FUNC(name,impl) \
    extern "C" __attribute__((weak,noinline)) void name(X86Context& __restrict ctx,uint8_t* base) { \
        ::shaolin::mods::RunRoutine(::shaolin::mods::FunctionAddress(#name),ctx,base,impl); \
    }
#undef X86_TRACED_FUNC
#define X86_TRACED_FUNC(name,impl,va) \
    extern "C" __attribute__((weak,noinline)) void name(X86Context& __restrict ctx,uint8_t* base) { \
        const auto caller=x86_trace(ctx,va,0); \
        ::shaolin::mods::RunRoutine(va,ctx,base,impl); \
        x86_trace(ctx,va,caller); \
    }
