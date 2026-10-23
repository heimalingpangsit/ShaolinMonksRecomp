#include "app/app.h"
#include "keyboard.h"
#include "controller.h"
#include "performance_overlay.h"
#include "launcher.h"

const app::GameInfo app::kGame = {
    .title = L"Shaolin Monks Recomp",
    .short_title = L"Shaolin Monks Recomp",
    .name = L"ShaolinMonksRecomp",
    .log = "shaolin-recomp",
    .settings = "settings.toml",
    .splash = L"shaolin-recomp",
    .missing = L"Select your Xbox game files.",
    .images = {{"default.xbe", "ShaolinMonksRecomp"}},
    .options = {},
    .load = shaolin::launcher::Load, .save = shaolin::launcher::Save, .init = nullptr, .loaded = nullptr,
    .window = shaolin::controller::Window, .message = shaolin::controller::Message, .ui = shaolin::performance::Draw,
    .pad = shaolin::keyboard::Poll, .rumble = nullptr,
};
