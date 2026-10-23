// Linux keyboard translation and the mouse camera. Mirrors src/mouse.cpp:
// the pointer is captured (SDL relative mode) on a click or a mapped key while
// the keyboard owns a player, F10 releases or captures it, and focus loss releases it.
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>

#include "controller.h"
#include "core/input.h"
#include "core/log.h"
#include "host.h"
#include "linux/linux_input.h"
#include "linux/virtual_keys.h"
#include "mouse.h"

namespace {
std::mutex lock;
shaolin::mouse::State state;
SDL_Window* window = nullptr;
std::atomic<bool> captured{false};
bool released = false;

double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

bool Available() {
    if (shaolin::options.background || !window || shaolin::controller::KeyboardPort() < 0) return false;
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    return (flags & SDL_WINDOW_INPUT_FOCUS) && !(flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED));
}

void Release() {
    if (captured.exchange(false) && window) SDL_SetWindowRelativeMouseMode(window, false);
    std::lock_guard guard(lock);
    state.Clear();
}

bool Capture() {
    if (!Available() || !SDL_SetWindowRelativeMouseMode(window, true)) return false;
    {
        std::lock_guard guard(lock);
        state.Clear();
    }
    captured = true;
    released = false;
    return true;
}

int ButtonIndex(uint8_t button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return 0;
    case SDL_BUTTON_RIGHT: return 1;
    case SDL_BUTTON_MIDDLE: return 2;
    case SDL_BUTTON_X1: return 3;
    case SDL_BUTTON_X2: return 4;
    default: return -1;
    }
}
}  // namespace

namespace shaolin::mouse {
bool Captured() { return captured.load(); }
void Configure(const Settings& settings) {
    auto valid = settings;
    valid.sensitivity = std::clamp(valid.sensitivity, 1, 300);
    std::lock_guard guard(lock);
    state.Configure(valid);
}
void Window(void*) {}  // linux_input::Window receives the SDL window
bool Message(void*, uint32_t, uintptr_t, intptr_t, intptr_t*) { return false; }
void Poll(input::Pad& pad) {
    if (!Captured()) return;
    std::lock_guard guard(lock);
    const auto mouse = state.Sample(Now());
    if (std::abs(int(mouse.rx)) > std::abs(int(pad.rx))) pad.rx = mouse.rx;
    if (std::abs(int(mouse.ry)) > std::abs(int(pad.ry))) pad.ry = mouse.ry;
    for (unsigned i = 0; i < 8; ++i) pad.analog[i] = std::max(pad.analog[i], mouse.analog[i]);
    pad.buttons |= mouse.buttons;
}
}  // namespace shaolin::mouse

namespace linux_input {
using shaolin::mouse::Captured;

unsigned VirtualKey(int scancode) {
    switch (scancode) {
    case SDL_SCANCODE_UP: return VK_UP;
    case SDL_SCANCODE_DOWN: return VK_DOWN;
    case SDL_SCANCODE_LEFT: return VK_LEFT;
    case SDL_SCANCODE_RIGHT: return VK_RIGHT;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
    case SDL_SCANCODE_BACKSPACE: return VK_BACK;
    case SDL_SCANCODE_SPACE: return VK_SPACE;
    case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL: return VK_CONTROL;  // WM_KEYDOWN reports both Ctrl keys as VK_CONTROL
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT: return VK_SHIFT;
    case SDL_SCANCODE_W: return 'W';
    case SDL_SCANCODE_A: return 'A';
    case SDL_SCANCODE_S: return 'S';
    case SDL_SCANCODE_D: return 'D';
    case SDL_SCANCODE_J: return 'J';
    case SDL_SCANCODE_K: return 'K';
    case SDL_SCANCODE_C: return 'C';
    case SDL_SCANCODE_Q: return 'Q';
    case SDL_SCANCODE_E: return 'E';
    case SDL_SCANCODE_F9: return VK_F9;
    case SDL_SCANCODE_F10: return VK_F10;
    default: return 0;
    }
}

void Window(void* sdl_window) {
    Release();
    window = static_cast<SDL_Window*>(sdl_window);
    released = false;
}

void Motion(float dx, float dy) {
    if (!Captured()) return;
    std::lock_guard guard(lock);
    state.Move(int(dx), int(dy), Now());
}

void Button(uint8_t sdl_button, bool down) {
    const int index = ButtonIndex(sdl_button);
    if (index < 0) return;
    if (!Captured()) {
        if (down) Capture();  // the capturing click is not passed to the game
        return;
    }
    std::lock_guard guard(lock);
    state.Button(unsigned(index), down);
}

void ToggleCapture() {
    if (Captured()) {
        Release();
        released = true;
    } else {
        Capture();
    }
}

void KeyPressed(unsigned key) {
    if (released || Captured()) return;
    if ((key >= 'A' && key <= 'Z') || key == VK_SPACE || key == VK_RETURN || (key >= VK_LEFT && key <= VK_DOWN)) Capture();
}

void FocusLost() { Release(); }

}  // namespace linux_input
