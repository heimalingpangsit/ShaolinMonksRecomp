#pragma once
#include <cstdint>
#include "mouse_state.h"

namespace shaolin::mouse {
void Configure(const Settings& settings);
void Window(void* window);
bool Message(void*, uint32_t, uintptr_t, intptr_t, intptr_t*);
void Poll(input::Pad& pad);
bool Captured();
}
