#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include "linux/virtual_keys.h"
#endif
#include <algorithm>
#include <bitset>
#include <mutex>
#include "keyboard.h"
#include "controller.h"
#include "mouse.h"
#include "core/input.h"
#include "core/log.h"

namespace shaolin::keyboard {
namespace {
std::mutex lock;
std::bitset<256> keys;
bool Mapped(unsigned key) {
    switch (key) {
    case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
    case VK_RETURN: case VK_BACK: case VK_SPACE: case VK_ESCAPE:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case 'W': case 'A': case 'S': case 'D': case 'J': case 'K':
    case 'C': case 'Q': case 'E': return true;
    default: return false;
    }
}
}

void Key(unsigned virtual_key, bool down) {
    std::lock_guard guard(lock);
    if (virtual_key < keys.size() && Mapped(virtual_key)) keys.set(virtual_key, down);
}
void ReleaseAll() {
    std::lock_guard guard(lock);
    keys.reset();
}
#ifdef _WIN32
bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t, intptr_t* result) {
    std::lock_guard guard(lock);
    if (msg == WM_KILLFOCUS || (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE)) {
        keys.reset();
        return false;
    }
    if ((msg == WM_KEYDOWN || msg == WM_KEYUP) && wp < keys.size() && Mapped(unsigned(wp))) {
        keys.set(wp, msg == WM_KEYDOWN);
        LOGI("shaolin input: window key=%u down=%u", unsigned(wp), msg == WM_KEYDOWN ? 1u : 0u);
        *result = 0;
        return true;
    }
    return false;
}
#else
bool Message(void*, uint32_t, uintptr_t, intptr_t, intptr_t*) { return false; }
#endif

void Poll(int port, input::Pad& pad) {
    if (port != controller::KeyboardPort()) return;
    std::lock_guard guard(lock);
    pad.connected = true;
    constexpr unsigned digital[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_BACK};
    for (unsigned i = 0; i < 6; ++i) if (keys[digital[i]]) pad.buttons |= uint16_t(1u << i);
    const bool analog[] = {keys[VK_SPACE], keys[VK_ESCAPE] || keys[VK_CONTROL] || keys[VK_LCONTROL] || keys[VK_RCONTROL],
                          keys['J'], keys['K'], keys['C'], keys['Q'],
                          keys[VK_SHIFT] || keys[VK_LSHIFT] || keys[VK_RSHIFT], keys['E']};
    for (unsigned i = 0; i < 8; ++i) if (analog[i]) pad.analog[i] = 255;
    if (keys['A'] || keys['D']) pad.lx = keys['A'] == keys['D'] ? 0 : keys['D'] ? 32767 : -32768;
    if (keys['W'] || keys['S']) pad.ly = keys['W'] == keys['S'] ? 0 : keys['W'] ? 32767 : -32768;
    mouse::Poll(pad);
}
}
