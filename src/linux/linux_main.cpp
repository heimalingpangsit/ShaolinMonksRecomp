// The Linux host: the SDL3 window and event loop, the app:: services the
// runtime expects from the Windows host (app/main.cpp, splash, handover and
// system information), and crash logging. Startup follows the Windows order.
#include <SDL3/SDL.h>
#include <csignal>
#include <signal.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#include "image_symbols.h"
#include "kernel/kernel.h"

#include "controller.h"
#include "host.h"
#include "keyboard.h"
#include "language_pack.h"
#include "launcher.h"
#include "linux/linux_input.h"
#include "linux/linux_platform.h"
#include "linux/virtual_keys.h"
#include "performance_overlay.h"
#include "runtime_paths.h"

namespace app {
namespace {

SDL_Window* g_window = nullptr;
SDL_ThreadID g_main_thread = 0;
Uint32 g_events = 0;  // first of the registered event types below
enum EventCode : Sint32 { kApplyDisplay = 1, kOpenWindow = 2 };
std::mutex g_open_lock;
std::condition_variable g_open_cv;
bool g_open_done = false;
std::atomic<bool> g_window_open{false};

std::string Utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {bytes.begin(), bytes.end()};
}

// main thread only
void ApplyNow() {
    if (shaolin::options.background || !g_window) return;
    const Settings& s = Get();
    const SDL_DisplayID display = SDL_GetDisplayForWindow(g_window);
    if (s.display_mode == DisplayMode::Windowed) {
        SDL_SetWindowFullscreen(g_window, false);
        SDL_Rect area{};
        if (!SDL_GetDisplayUsableBounds(display, &area)) area = {0, 0, 1280, 720};
        // as on Windows: the requested size, or 80% of the usable area, never larger than it
        double w = s.width && s.height ? s.width : area.w * 0.8, h = s.width && s.height ? s.height : area.h * 0.8;
        const double scale = std::min({1.0, area.w / w, area.h / h});
        SDL_SetWindowSize(g_window, std::max(1, int(w * scale)), std::max(1, int(h * scale)));
        SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        return;
    }
    if (s.display_mode == DisplayMode::Fullscreen && s.width && s.height) {
        SDL_DisplayMode mode{};
        if (SDL_GetClosestFullscreenDisplayMode(display, int(s.width), int(s.height), 0.0f, false, &mode))
            SDL_SetWindowFullscreenMode(g_window, &mode);
        else
            SDL_SetWindowFullscreenMode(g_window, nullptr);
    } else {
        SDL_SetWindowFullscreenMode(g_window, nullptr);  // borderless: the desktop mode
    }
    SDL_SetWindowFullscreen(g_window, true);
}

void OpenNow(bool apply) {
    if (shaolin::options.background || !g_window) return;
    if (apply) ApplyNow();
    SDL_ShowWindow(g_window);
    SDL_RaiseWindow(g_window);
    g_window_open = true;
}

void PushEvent(EventCode code, bool apply = false) {
    SDL_Event e{};
    e.type = g_events;
    e.user.code = code;
    e.user.data1 = reinterpret_cast<void*>(intptr_t(apply));
    SDL_PushEvent(&e);
}

void CrashSignal(int signal) {
    if (kernel::Thread* t = kernel::Current(); t && t->ctx) {
        LOGE("signal %d on guest thread %X", signal, t->id);
        kernel::LogGuestStack(*t->ctx, 256);
    } else {
        LOGE("signal %d on a host thread", signal);
    }
    logx::Flush();
    const std::string text = "The game stopped because of an unexpected error (signal " + std::to_string(signal) + ").";
    shaolin::paths::ReportError(std::filesystem::path(std::u8string(text.begin(), text.end())).wstring());
    std::signal(signal, SIG_DFL);
    raise(signal);
}

std::string CpuName() {
    std::ifstream info("/proc/cpuinfo");
    for (std::string line; std::getline(info, line);)
        if (line.rfind("model name", 0) == 0) {
            const auto colon = line.find(':');
            return colon == std::string::npos ? line : line.substr(colon + 2);
        }
    return "unknown";
}

std::string OsName() {
    std::ifstream release("/etc/os-release");
    std::string name;
    for (std::string line; std::getline(release, line);)
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            name = line.substr(12);
            name.erase(std::remove(name.begin(), name.end(), '"'), name.end());
        }
    utsname u{};
    uname(&u);
    return (name.empty() ? std::string(u.sysname) : name) + " (kernel " + u.release + ")";
}

}  // namespace

std::filesystem::path ExeDir() { return linux_platform::ExecutablePath().parent_path(); }

int Instance() {
    // --instance N: a second copy on this PC for system link tests
    static const int n = [] {
        const auto args = linux_platform::Arguments();
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i].rfind(L"--instance", 0) != 0) continue;
            const std::wstring value = args[i].size() > 10 ? args[i].substr(10) : i + 1 < args.size() ? args[i + 1] : L"";
            return int(std::clamp(std::wcstol(value.c_str(), nullptr, 10), 0L, 8L));
        }
        return 0;
    }();
    return n;
}

void* Window() { return g_window; }

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

void ApplyDisplay() {
    if (SDL_GetCurrentThreadID() == g_main_thread) ApplyNow();
    else PushEvent(kApplyDisplay);
}

void OpenGameWindow(bool apply) {
    if (SDL_GetCurrentThreadID() == g_main_thread) {
        OpenNow(apply);
        return;
    }
    // returns once the window is up, as the Windows host's SendMessage does
    std::unique_lock lock(g_open_lock);
    g_open_done = false;
    PushEvent(kOpenWindow, apply);
    g_open_cv.wait_for(lock, std::chrono::seconds(5), [] { return g_open_done; });
}

void Pace() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    uint32_t cap = Get().fps_cap;
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    while (clock::now() + std::chrono::milliseconds(2) < next) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    while (clock::now() < next) std::this_thread::yield();
}

// The Windows host shows a splash and hands the window over between images; Linux opens on the first frame.
bool OpenSplash() { return false; }
void SplashFirstFrame() {
    if (!g_window_open.load()) OpenGameWindow();
}
void CloseSplash() {}
bool StartHandover() { return false; }
bool HandingOver() { return false; }
void FinishHandover() { OpenGameWindow(); }

Mode PlatformDesktop() {
    SDL_DisplayID display = g_window ? SDL_GetDisplayForWindow(g_window) : SDL_GetPrimaryDisplay();
    if (!display) display = SDL_GetPrimaryDisplay();
    if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(display)) return {uint32_t(mode->w), uint32_t(mode->h)};
    return {1920, 1080};
}

void LogSystem() {
    std::time_t now = std::time(nullptr);
    char date[32] = {};
    std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    LOGR("%ls (%s), %s", kGame.title, IMAGE_XBE, date);
    LOGR("Path: %s", Utf8(ExeDir()).c_str());
    LOGR("OS: %s", OsName().c_str());
    LOGR("CPU: %s (%ld threads)", CpuName().c_str(), sysconf(_SC_NPROCESSORS_ONLN));
    LOGR("RAM: %.1f GB", double(sysconf(_SC_PHYS_PAGES)) * double(sysconf(_SC_PAGE_SIZE)) / 1073741824.0);
    LOGR("Video driver: %s", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");
    Mode desk = Desktop();
    LOGR("Desktop: %ux%u", desk.width, desk.height);
    LogSettings();
}

}  // namespace app

namespace {

void HandleEvent(const SDL_Event& e, bool& running) {
    using namespace shaolin;
    switch (e.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        running = false;
        return;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        controller::SetFocused(true);
        return;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
        controller::SetFocused(false);
        keyboard::ReleaseAll();
        linux_input::FocusLost();
        return;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = e.type == SDL_EVENT_KEY_DOWN;
        const unsigned key = linux_input::VirtualKey(e.key.scancode);
        if (key == VK_F9) {
            // the overlay toggles on the first press, as WM_KEYDOWN without the repeat bit
            performance::Message(down ? 0x100 : 0x101, 0x78, e.key.repeat ? intptr_t(1) << 30 : 0);
            return;
        }
        if (key == VK_F10) {
            if (down && !e.key.repeat) linux_input::ToggleCapture();
            return;
        }
        if (!key) return;
        keyboard::Key(key, down);
        if (down) linux_input::KeyPressed(key);
        return;
    }
    case SDL_EVENT_MOUSE_MOTION:
        linux_input::Motion(e.motion.xrel, e.motion.yrel);
        return;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        linux_input::Button(e.button.button, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        return;
    default:
        break;
    }
    if (e.type == app::g_events) {
        if (e.user.code == app::kApplyDisplay) app::ApplyNow();
        if (e.user.code == app::kOpenWindow) {
            app::OpenNow(e.user.data1 != nullptr);
            std::lock_guard lock(app::g_open_lock);
            app::g_open_done = true;
            app::g_open_cv.notify_all();
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    linux_platform::SetArguments(argc, argv);
    const int configured = shaolin::Configure();
    if (configured) return configured == 1 ? 0 : configured;
    for (int s : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) std::signal(s, app::CrashSignal);
    std::signal(SIGPIPE, SIG_IGN);
    bool relaunched = false;
    for (const auto& argument : linux_platform::Arguments()) relaunched |= argument == L"--relaunched";
    std::string log = app::kGame.log;
    if (app::Instance() > 1) log += "-" + std::to_string(app::Instance());
    logx::Init(shaolin::paths::Logs().string(), log, relaunched);
    app::Load();
    if (shaolin::options.background) {
        app::Get().display_mode = app::DisplayMode::Windowed;
        app::Get().width = 640;
        app::Get().height = 480;
    }
    if (shaolin::options.configure_only ||
        (!shaolin::options.background && !shaolin::options.validate_disc &&
         !shaolin::options.validate_language_pack && !shaolin::options.no_launcher)) {
        if (!shaolin::launcher::Show()) return 0;
    }
    shaolin::StartWatchdog();
    if (app::kGame.init) app::kGame.init();

    // background diagnostics run without a display, as the hidden window does on Windows
    if (shaolin::options.background) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen,dummy");
    SDL_SetHint(SDL_HINT_APP_ID, "ShaolinMonksRecomp");
    SDL_SetHint(SDL_HINT_APP_NAME, "Shaolin Monks Recomp");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL video initialization failed: %s\n", SDL_GetError());
        shaolin::paths::ReportError(L"The display could not be opened. Check that a desktop session is running.");
        return 4;
    }
    app::g_main_thread = SDL_GetCurrentThreadID();
    app::g_events = SDL_RegisterEvents(1);

    if (relaunched && !logx::kVerbose) LOGR("Relaunched: %s", IMAGE_XBE);
    else app::LogSystem();
    LOGR("Shaolin Monks Recomp version: %s", SHAOLIN_RECOMP_VERSION);

    app::GameFiles game;
    if (!app::FindGame(game)) return 3;
    if (shaolin::options.validate_language_pack) {
        const auto& stats = shaolin::language::Statistics();
        std::printf("{\"language_pack_valid\":true,\"direct_loading\":true,\"resources\":%u,\"voice_names\":%u,\"voice_occurrences\":%u,\"movies\":%u}\n",
                    stats.archive.resources, stats.archive.voice_names, stats.archive.voice_occurrences, stats.movies);
        return 0;
    }
    if (shaolin::options.validate_disc) {
        std::printf("{\"valid\":true}\n");
        return 0;
    }

    const SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    app::g_window = SDL_CreateWindow("Shaolin Monks Recomp", 1280, 720, flags);
    if (!app::g_window) {
        // without Vulkan support in the window system the null renderer still runs diagnostics
        app::g_window = SDL_CreateWindow("Shaolin Monks Recomp", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
        if (!app::g_window) {
            LOGE("Cannot create the game window: %s", SDL_GetError());
            shaolin::paths::ReportError(L"The game window could not be created.");
            return 4;
        }
    }
    if (app::Instance())
        SDL_SetWindowTitle(app::g_window, ("Shaolin Monks Recomp [" + std::to_string(app::Instance()) + "]").c_str());

    if (!shaolin::options.background || shaolin::options.virtual_gamepads) input::Init();
    linux_input::Window(app::g_window);
    if (app::kGame.window) app::kGame.window(app::g_window);
    std::wstring hdd = app::Instance() > 1 ? L"hdd" + std::to_wstring(app::Instance()) : L"hdd";
    std::filesystem::path save_directory;
    shaolin::storage::DirectoryLock save_lock;
    if (!shaolin::OpenSaveDirectory(hdd, save_directory, save_lock)) return 6;
    if (!kernel::Boot(std::move(game.xbe), IMAGE_XBE, game.dir.string(), save_directory.string())) {
        shaolin::paths::ReportError(L"The game failed to start.");
        return 1;
    }

    bool running = true;
    while (running) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 8)) {
            do HandleEvent(e, running);
            while (running && SDL_PollEvent(&e));
        }
        // the Windows host drives this from an 8 ms timer on the window thread
        shaolin::controller::Update();
    }
    logx::Flush();
    kernel::Shutdown(0);
}
