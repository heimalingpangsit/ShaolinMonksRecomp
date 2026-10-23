#include <cmath>
#include <cstdlib>
#include <iostream>
#include "mouse_state.h"
#include "core/input.h"

static void Check(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    using namespace shaolin::mouse;
    State mouse;
    mouse.Move(12, -8, 1.0);
    const auto initial = mouse.Sample(1.0);
    Check(initial.rx > 0 && initial.ry > 0, "Relative mouse movement must reach both camera axes");
    Check(mouse.Sample(1.001).rx == initial.rx, "Repeated polls must retain the same report within an input interval");
    Check(!mouse.Sample(1.1).rx && !mouse.Sample(1.1).ry, "Camera movement must stop when the mouse stops");
    Settings settings;
    mouse.Configure(settings);
    mouse.Move(8, 4, 2.0);
    const auto normal = mouse.Sample(2.0);
    settings.sensitivity = 200; settings.invert_y = true;
    mouse.Configure(settings);
    mouse.Move(8, 4, 3.0);
    const auto inverted = mouse.Sample(3.0);
    Check(inverted.rx == normal.rx * 2 && inverted.ry == -normal.ry * 2, "Sensitivity and vertical inversion must affect the camera");
    State fast, slow;
    fast.Move(6, 0, 4.0); fast.Sample(4.0);
    slow.Move(6, 0, 4.0); slow.Sample(4.0);
    fast.Move(6, 0, 4.0 + 1.0 / 120);
    slow.Move(12, 0, 4.0 + 1.0 / 60);
    Check(std::abs(fast.Sample(4.0 + 1.0 / 120).rx - slow.Sample(4.0 + 1.0 / 60).rx) <= 1,
          "Equal mouse speed must produce equal camera input at different polling rates");
    mouse.Configure(Settings{});
    for (unsigned button = 0; button < 5; ++button) mouse.Button(button, true);
    auto buttons = mouse.Sample(5.0);
    Check(buttons.analog[2] == 255 && buttons.analog[1] == 255 && buttons.analog[3] == 255 &&
          buttons.analog[4] == 255 && buttons.analog[5] == 255, "The five mouse buttons must use their configured actions");
    mouse.Clear(); buttons = mouse.Sample(5.1);
    Check(!buttons.analog[2] && !buttons.analog[1] && !buttons.rx, "Releasing mouse capture must clear all held input");
    settings = Settings{}; settings.bindings[0] = "right_trigger"; settings.bindings[1] = "none";
    mouse.Configure(settings); mouse.Button(0, true); mouse.Button(1, true);
    buttons = mouse.Sample(6.0);
    Check(buttons.analog[7] == 255 && !buttons.analog[2] && !buttons.analog[1], "Remapping and unassigned mouse buttons must work");
    mouse.Move(INT32_MAX, INT32_MIN, 7.0); buttons = mouse.Sample(7.0);
    Check(buttons.rx == 32767 && buttons.ry == 32767, "Extreme motion must saturate without overflow");
}
