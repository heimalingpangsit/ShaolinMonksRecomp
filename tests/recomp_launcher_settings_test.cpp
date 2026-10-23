#include "platform_compat.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include "launcher_settings.h"

int main() {
    using namespace shaolin::launcher;
    int failures = 0;
    const auto check = [&](bool good, const char* message) { if (!good) { ++failures; std::cerr << message << '\n'; } };
    auto table = toml::parse("custom = 42\n[controller]\ndeadzone = 0.23\ncustom = true\n[controller.bindings]\na = 'cross'\ncustom = 'keep'\n");
    auto config = Read(table);
    config.language = "fr";
    const auto merged = Merge(table, config);
    check(merged["custom"].value_or(0) == 42, "Saving must preserve unrelated application settings");
    check(merged["controller"].as_table()->size() == 3 && merged["controller"]["bindings"].as_table()->size() == 10,
          "Saving must keep only supported controller settings and button mappings");
    check(Read(merged) == config && std::abs(config.deadzone - .23) < .0001, "The settings must survive a complete round trip");
    const auto mouse = Read(toml::parse("[mouse]\nsensitivity = 173\ninvert_y = true\n[mouse.bindings]\nleft = 'right_trigger'\n"));
    check(Read(Merge({}, mouse)) == mouse && mouse.mouse.sensitivity == 173 && mouse.mouse.invert_y &&
          mouse.mouse.bindings[0] == "right_trigger", "Mouse sensitivity, inversion, and button mappings must survive saving");
    std::string error;
    check(Merge({},Settings{})["fps_cap"].value_or(0)==60,"The frame rate must be fixed at 60 FPS");
    for(const int fps:{-1,0,29,30,45,60,61,120,1000}) {
        const toml::table legacy{{"fps_cap",fps}};
        const auto loaded=Read(legacy);
        check(loaded==Settings{},"Legacy frame-rate preferences must not change launcher settings");
        check(Merge(legacy,loaded)["fps_cap"].value_or(0)==60,"Saving legacy settings must use 60 FPS");
    }
    check(Validate(config, error), "Valid settings must be accepted");
    config.width = 1920; config.height = 0;
    check(!Validate(config, error), "An incomplete resolution must be rejected");
    config.width = 0; config.deadzone = NAN;
    check(!Validate(config, error), "A nonfinite dead zone must be rejected");
    config = Settings{}; config.language = "bad code";
    check(!Validate(config, error), "An unavailable game language must be rejected");
    config.language = "pt-BR";
    check(Validate(config, error) && GameLanguage(config.language) == 0,
          "Brazilian Portuguese must use its language pack over the English game slot");
    config.language="ja";
    check(Validate(config,error)&&GameLanguage(config.language)==0,"New language codes must be saved and use the translated English slot");
    check(!Validate(Read(toml::parse("[game]\nmusic_volume = 101\n")), error), "Original music volume must reject values above 100 percent");
    check(!Validate(Read(toml::parse("[game]\nversus_rounds = 2\n")), error), "Versus rounds must match an original game option");
    check(!Validate(Read(toml::parse("[mouse]\nsensitivity = 0\n")), error), "Mouse sensitivity must reject zero");
    check(!Validate(Read(toml::parse("[mouse.bindings]\nleft = 'missing'\n")), error), "Mouse buttons must reject unknown actions");
    check(GameLanguage("en") == 0 && GameLanguage("fr") == 1 && GameLanguage("de") == 3 &&
          GameLanguage("es") == 4 && GameLanguage("it") == 2, "Language selection must match the original text tables");
    const auto root = std::filesystem::temp_directory_path() / ("shaolin-settings-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directory(root);
    const auto file = root / "settings.toml";
    check(Write(file, merged, error) && Read(toml::parse_file(file.string())).language == "fr", "The atomic settings writer must write the selected language");
    std::filesystem::create_directory(root / "blocked");
    std::ofstream(root / "blocked/keep") << "preserve";
    check(!Write(root / "blocked", merged, error) && std::filesystem::exists(root / "blocked/keep"), "A failed replacement must preserve its destination");
    std::filesystem::remove_all(root);
    return failures ? 1 : 0;
}
