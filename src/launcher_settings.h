#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <toml++/toml.hpp>
#include "mouse_state.h"

namespace shaolin::launcher {
inline constexpr int FrameRate=60;
inline constexpr std::array<const char*, 6> languages{"en", "fr", "de", "es", "it", "pt-BR"};
inline constexpr std::array<const char*, 10> binding_keys{"a", "b", "x", "y", "black", "white", "start", "back", "left_stick", "right_stick"};
struct Settings {
    std::string language = "en", display_mode = "borderless";
    int width = 0, height = 0, render_percent = 100, aniso = 0;
    int master_volume = 100;
    double deadzone = .15;
    bool vsync = true, show_fps = false, rumble = true;
    mouse::Settings mouse;
    bool original_override = false, camera_reverse = false;
    int music_volume = 90, effects_volume = 90, speech_volume = 90;
    int brightness = 0, versus_rounds = 3, versus_time = 120;
    std::array<std::string, 10> bindings{"a", "b", "x", "y", "rightshoulder", "leftshoulder", "start", "back", "leftstick", "rightstick"};
    bool operator==(const Settings&) const = default;
};
Settings Read(const toml::table& table);
toml::table Merge(toml::table table, const Settings& settings);
bool Validate(const Settings& settings, std::string& error);
bool Write(const std::filesystem::path& file, const toml::table& table, std::string& error);
unsigned GameLanguage(const std::string& language);
}
