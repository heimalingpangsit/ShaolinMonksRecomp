#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include "mouse.h"
#include "window.h"
#include "controller.h"
#include "host.h"
#include "core/input.h"
#include "core/log.h"

namespace shaolin::mouse {
namespace {
std::mutex lock;
State state;
HWND window = nullptr;
std::atomic<bool> captured{false};
bool registered = false, released = false;
RECT confinement{};
constexpr UINT_PTR timer = 0x534D4D53;
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool Interactive() {
    wchar_t current[256]{}, input[256]{}; DWORD needed = 0;
    const auto desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    const bool result = desktop &&
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, current, sizeof(current), &needed) &&
        GetUserObjectInformationW(desktop, UOI_NAME, input, sizeof(input), &needed) && std::wstring(current) == input;
    if (desktop) CloseDesktop(desktop);
    return result;
}
bool Available() {
    return !options.background && registered && window && IsWindowVisible(window) && !IsIconic(window) &&
        GetForegroundWindow() == window && controller::KeyboardPort() >= 0 && Interactive();
}
void Release() {
    if (captured.exchange(false)) {
        RECT current{};
        if (GetClipCursor(&current) && EqualRect(&current, &confinement)) ClipCursor(nullptr);
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
    }
    std::lock_guard guard(lock); state.Clear();
}
bool Capture() {
    RECT bounds{};
    if (!Available() || !shaolin::window::ClientBounds(window, bounds) || !ClipCursor(&bounds)) return false;
    confinement = bounds;
    { std::lock_guard guard(lock); state.Clear(); }
    captured = true; released = false; SetCursor(nullptr);
    return true;
}
int ButtonIndex(uint32_t message, uintptr_t key) {
    switch (message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: return 0;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: return 1;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: return 2;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: return HIWORD(key) == XBUTTON1 ? 3 : 4;
    default: return -1;
    }
}
}
bool Captured() { return captured.load(); }
void Configure(const Settings& settings) {
    auto valid = settings; valid.sensitivity = std::clamp(valid.sensitivity, 1, 300);
    std::lock_guard guard(lock); state.Configure(valid);
}
void Window(void* handle) {
    Release();
    if (window) KillTimer(window, timer);
    if (registered) { RAWINPUTDEVICE device{1, 2, RIDEV_REMOVE, nullptr}; RegisterRawInputDevices(&device, 1, sizeof(device)); }
    registered = false; window = static_cast<HWND>(handle); released = false;
    if (options.background) return;
    RAWINPUTDEVICE device{1, 2, 0, window};
    registered = RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
    if (!registered) LOGW("mouse: raw input registration failed (%lu)", GetLastError());
    if (!SetTimer(window, timer, 16, nullptr)) LOGW("mouse: capture timer failed");
}
bool Message(void* handle, uint32_t message, uintptr_t key, intptr_t value, intptr_t* result) {
    if (options.background || handle != window) return false;
    if (message == WM_DESTROY) {
        Release(); KillTimer(window, timer);
        if (registered) { RAWINPUTDEVICE device{1, 2, RIDEV_REMOVE, nullptr}; RegisterRawInputDevices(&device, 1, sizeof(device)); }
        registered = false; window = nullptr;
        return false;
    }
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATE && LOWORD(key) == WA_INACTIVE) ||
        (message == WM_ACTIVATEAPP && !key) || (message == WM_SIZE && key == SIZE_MINIMIZED) ||
        message == WM_ENTERSIZEMOVE || message == WM_ENTERMENULOOP || message == WM_NCLBUTTONDOWN ||
        (message == WM_CAPTURECHANGED && reinterpret_cast<HWND>(value) != window)) {
        Release(); return false;
    }
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && key == VK_F10) {
        if (!(value & (intptr_t(1) << 30))) {
            if (Captured()) { Release(); released = true; }
            else Capture();
        }
        *result = 0; return true;
    }
    if ((message == WM_KEYUP || message == WM_SYSKEYUP) && key == VK_F10) { *result = 0; return true; }
    if (message == WM_KEYDOWN && !released && !Captured() &&
        ((key >= 'A' && key <= 'Z') || key == VK_SPACE || key == VK_RETURN || (key >= VK_LEFT && key <= VK_DOWN))) Capture();
    if (message == WM_TIMER && key == timer) {
        if (Captured()) {
            RECT bounds{};
            if (!Available() || !shaolin::window::ClientBounds(window, bounds)) Release();
            else if (!EqualRect(&bounds, &confinement)) {
                if (ClipCursor(&bounds)) confinement = bounds;
                else Release();
            }
        }
        *result = 0; return true;
    }
    if (message == WM_SETCURSOR && Captured() && LOWORD(value) == HTCLIENT) {
        SetCursor(nullptr); *result = TRUE; return true;
    }
    if (message == WM_INPUT && Captured()) {
        RAWINPUT packet{}; UINT size = sizeof(packet);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(value), RID_INPUT, &packet, &size, sizeof(RAWINPUTHEADER)) != UINT(-1) &&
            packet.header.dwType == RIM_TYPEMOUSE && !(packet.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            std::lock_guard guard(lock); state.Move(packet.data.mouse.lLastX, packet.data.mouse.lLastY, Now());
        }
        return false; // DefWindowProc must perform WM_INPUT cleanup.
    }
    const auto button = ButtonIndex(message, key);
    if (button >= 0) {
        const bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
        if (!Captured()) {
            if (!down || !Capture()) return false;
        } else {
            std::lock_guard guard(lock); state.Button(button, down);
        }
        *result = message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ? TRUE : 0; return true;
    }
    return false;
}
void Poll(input::Pad& pad) {
    if (!Captured()) return;
    std::lock_guard guard(lock);
    const auto mouse = state.Sample(Now());
    if (std::abs(int(mouse.rx)) > std::abs(int(pad.rx))) pad.rx = mouse.rx;
    if (std::abs(int(mouse.ry)) > std::abs(int(pad.ry))) pad.ry = mouse.ry;
    for (unsigned i = 0; i < 8; ++i) pad.analog[i] = std::max(pad.analog[i], mouse.analog[i]);
    pad.buttons |= mouse.buttons;
}
}
