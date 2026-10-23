#pragma once
#include <cstdint>
struct X86Context;
namespace shaolin::preferences {
void Apply(X86Context& context, uint8_t* base);
}
