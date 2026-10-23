#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace shaolin::window {
bool Place(HWND window, bool windowed, unsigned width, unsigned height);
bool ClientBounds(HWND window, RECT& bounds);
}
