#pragma once
#include <cstdint>
#include <string>
namespace shaolin::performance {
bool Visible();
void SetVisible(bool visible);
bool Message(uint32_t message, uintptr_t key, intptr_t flags);
std::string Format(double fps);
void Draw(double fps);
}
