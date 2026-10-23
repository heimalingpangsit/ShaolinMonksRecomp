#include "window.h"
#include <algorithm>
#include <cmath>
bool shaolin::window::ClientBounds(HWND hwnd, RECT& bounds) {
    RECT client{}; MONITORINFO monitor{sizeof(monitor)};
    if (!GetClientRect(hwnd, &client) || !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
    POINT top{client.left, client.top}, bottom{client.right, client.bottom};
    if (!ClientToScreen(hwnd, &top) || !ClientToScreen(hwnd, &bottom)) return false;
    const RECT screen{top.x, top.y, bottom.x, bottom.y};
    return IntersectRect(&bounds, &screen, &monitor.rcMonitor) != FALSE;
}
bool shaolin::window::Place(HWND hwnd, bool windowed, unsigned width, unsigned height) {
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
    const auto style = (windowed ? WS_OVERLAPPEDWINDOW : WS_POPUP) |
        (IsWindowVisible(hwnd) ? WS_VISIBLE : 0);
    RECT bounds = monitor.rcMonitor;
    if (windowed) {
        RECT frame{};
        if (!AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE,
            DWORD(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), GetDpiForWindow(hwnd))) return false;
        const auto& area = monitor.rcWork;
        const int extra_width = frame.right - frame.left, extra_height = frame.bottom - frame.top;
        const int available_width = std::max(1L, area.right - area.left - extra_width);
        const int available_height = std::max(1L, area.bottom - area.top - extra_height);
        const double requested_width = width && height ? width : available_width * .8;
        const double requested_height = width && height ? height : available_height * .8;
        const auto scale = std::min({1.0, available_width / requested_width, available_height / requested_height});
        const int w = std::max(1, int(std::floor(requested_width * scale))) + extra_width;
        const int h = std::max(1, int(std::floor(requested_height * scale))) + extra_height;
        bounds.left = area.left + (area.right - area.left - w) / 2;
        bounds.top = area.top + (area.bottom - area.top - h) / 2;
        bounds.right = bounds.left + w; bounds.bottom = bounds.top + h;
    }
    SetLastError(0);
    if (!SetWindowLongPtrW(hwnd, GWL_STYLE, style) && GetLastError()) return false;
    return SetWindowPos(hwnd, HWND_NOTOPMOST, bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top,
        SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE;
}
