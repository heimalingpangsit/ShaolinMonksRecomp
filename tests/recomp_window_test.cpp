#define NOMINMAX
#include <windows.h>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "window.h"
#include "mouse.h"
#include "host.h"
#include "core/input.h"
namespace shaolin { Options options; }
namespace shaolin::controller { int KeyboardPort() { return 0; } }

static void Check(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ShaolinWindowTest";
    Check(RegisterClassW(&wc) != 0, "Register the hidden test window");
    const auto hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Window test",
        WS_POPUP, 0, 0, 32, 32, nullptr, nullptr, wc.hInstance, nullptr);
    Check(hwnd && !IsWindowVisible(hwnd), "The test must never show a window");
    shaolin::mouse::Window(hwnd);
    intptr_t result = 0;
    shaolin::mouse::Message(hwnd, WM_KEYDOWN, VK_F10, 0, &result);
    shaolin::mouse::Message(hwnd, WM_LBUTTONDOWN, 0, 0, &result);
    Check(!shaolin::mouse::Captured(), "A hidden window must never capture the user's cursor");
    input::Pad untouched{}; shaolin::mouse::Poll(untouched);
    Check(!untouched.rx && !untouched.analog[2], "Inactive mouse input must stay neutral");
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor); return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    Check(!monitors.empty(), "Enumerate connected monitors");
    for (const auto handle : monitors) {
        MONITORINFO monitor{sizeof(monitor)};
        Check(GetMonitorInfoW(handle, &monitor), "Read the current monitor bounds");
        SetWindowPos(hwnd, nullptr, monitor.rcWork.left + 20, monitor.rcWork.top + 20, 32, 32, SWP_NOZORDER | SWP_NOACTIVATE);
        for (const auto size : {POINT{0, 0}, POINT{640, 480}, POINT{16384, 16384}}) {
            Check(shaolin::window::Place(hwnd, true, size.x, size.y), "Apply windowed mode");
            RECT actual{}; GetWindowRect(hwnd, &actual);
            const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            Check((style & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW, "Windowed mode must expose caption, minimize, maximize, and close");
            Check(actual.left >= monitor.rcWork.left && actual.top >= monitor.rcWork.top &&
                  actual.right <= monitor.rcWork.right && actual.bottom <= monitor.rcWork.bottom,
                  "The complete window frame must fit in the monitor work area");
            Check(!IsWindowVisible(hwnd), "Applying a display mode must preserve a hidden window");
            RECT clipped{};
            Check(shaolin::window::ClientBounds(hwnd, clipped) && clipped.left >= monitor.rcMonitor.left &&
                  clipped.right <= monitor.rcMonitor.right && clipped.top >= monitor.rcMonitor.top && clipped.bottom <= monitor.rcMonitor.bottom,
                  "Mouse confinement must fit both the client area and its selected monitor");
        }
        Check(shaolin::window::Place(hwnd, false, 0, 0), "Apply borderless mode");
        Check(!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION), "Borderless mode must have no caption");
        RECT actual{}; GetWindowRect(hwnd, &actual);
        Check(EqualRect(&actual, &monitor.rcMonitor), "Borderless mode must fill only its selected monitor");
        Check(!IsWindowVisible(hwnd), "Borderless placement must not show the hidden test window");
    }
    shaolin::mouse::Message(hwnd, WM_DESTROY, 0, 0, &result);
    DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName, wc.hInstance);
    std::cout << "PASS: window frame and mouse confinement bounds on " << monitors.size() << " monitors\n";
}
