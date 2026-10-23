#pragma once
// The Linux settings launcher's window: SDL for the window and input, Dear ImGui drawn with Vulkan, the API the
// game itself needs. Open fails without a display or a Vulkan device, and the launcher falls back to settings.toml.
struct SDL_Window;

namespace linux_launcher {

class Window {
public:
    Window();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Open(const char* title, int width, int height);
    // one frame: events, then ImGui::NewFrame; false when the window's close button was pressed
    bool BeginFrame();
    void EndFrame();
    void Close();
    SDL_Window* Handle() const;

private:
    struct State;
    State* s_;
};

}  // namespace linux_launcher
