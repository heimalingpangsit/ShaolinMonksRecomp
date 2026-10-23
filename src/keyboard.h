#pragma once
#include <cstdint>
namespace input { struct Pad; }
namespace shaolin::keyboard {
bool Message(void*, uint32_t, uintptr_t, intptr_t, intptr_t*);
// Platform-neutral key state, indexed by Windows virtual-key code (Linux maps SDL keys to these).
void Key(unsigned virtual_key, bool down);
void ReleaseAll();
void Poll(int port, input::Pad& pad);
}
