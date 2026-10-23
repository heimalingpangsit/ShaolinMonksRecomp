#include "performance_overlay.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <imgui.h>
namespace shaolin::performance {
namespace { std::atomic<bool> visible{false}; }
bool Visible() { return visible.load(std::memory_order_relaxed); }
void SetVisible(bool value) { visible.store(value, std::memory_order_relaxed); }
bool Message(uint32_t message, uintptr_t key, intptr_t flags) {
    constexpr uint32_t keydown = 0x100, keyup = 0x101, f9 = 0x78;
    if (key != f9 || (message != keydown && message != keyup)) return false;
    if (message == keydown && !(uintptr_t(flags) & (uintptr_t(1) << 30)))
        visible.store(!Visible(), std::memory_order_relaxed);
    return true;
}
std::string Format(double fps) {
    if (!std::isfinite(fps) || fps <= 0) return "-- FPS  |  -- ms";
    char text[80];
    std::snprintf(text, sizeof text, "%.1f FPS  |  %.2f ms", fps, 1000.0 / fps);
    return text;
}
void Draw(double fps) {
    if (!Visible()) return;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + 12, viewport->WorkPos.y + 12}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(.78f);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##shaolin-performance", nullptr, flags)) ImGui::TextUnformatted(Format(fps).c_str());
    ImGui::End();
}
}
