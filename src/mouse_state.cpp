#include "mouse_state.h"
#include "core/input.h"
#include <algorithm>
#include <cmath>
namespace shaolin::mouse {
void State::Configure(const Settings& value) { settings = value; Clear(); }
void State::Clear() { held = {}; dx = dy = 0; sampled = moved = -1; rx = ry = 0; }
void State::Move(int x, int y, double now) {
    dx = std::clamp(dx + x, int64_t(-1000000), int64_t(1000000));
    dy = std::clamp(dy + y, int64_t(-1000000), int64_t(1000000));
    moved = now;
    if (sampled < 0) sampled = now - 1.0 / 60;
}
void State::Button(unsigned index, bool down) { if (index < held.size()) held[index] = down; }
input::Pad State::Sample(double now) {
    if (sampled >= 0 && now - sampled >= 1.0 / 120 - 1e-9) {
        const auto seconds = now - sampled > .05 ? 1.0 / 60 : now - sampled;
        const auto axis = [&](int64_t value) {
            return int16_t(std::lround(std::clamp(value * 20.0 * settings.sensitivity / 100 / seconds, -32767.0, 32767.0)));
        };
        rx = axis(dx); ry = axis(settings.invert_y ? dy : -dy);
        dx = dy = 0; sampled = now;
    }
    if (now - moved > .05) rx = ry = 0;
    input::Pad pad; pad.rx = rx; pad.ry = ry;
    for (unsigned i = 0; i < held.size(); ++i) if (held[i]) {
        const auto action = std::find(actions.begin(), actions.end(), settings.bindings[i]);
        const auto index = action - actions.begin();
        if (index >= 1 && index <= 8) pad.analog[index - 1] = 255;
        else if (index >= 9 && index <= 12) pad.buttons |= uint16_t(1 << (index - 5));
    }
    return pad;
}
}
