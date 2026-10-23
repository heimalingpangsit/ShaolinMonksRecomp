#pragma once
#include <array>
#include <string>
#include <cstdint>
namespace input { struct Pad; }

namespace shaolin::mouse {
inline constexpr std::array<const char*, 13> actions{"none", "a", "b", "x", "y", "black", "white",
    "left_trigger", "right_trigger", "start", "back", "left_stick", "right_stick"};
inline constexpr std::array<const char*, 5> button_keys{"left", "right", "middle", "side1", "side2"};
struct Settings {
    int sensitivity = 100;
    bool invert_y = false;
    std::array<std::string, 5> bindings{"x", "b", "y", "black", "white"};
    bool operator==(const Settings&) const = default;
};
class State {
public:
    void Configure(const Settings&);
    void Clear();
    void Move(int dx, int dy, double now);
    void Button(unsigned index, bool down);
    input::Pad Sample(double now);
private:
    Settings settings;
    std::array<bool, 5> held{};
    int64_t dx = 0, dy = 0;
    double sampled = -1, moved = -1;
    int16_t rx = 0, ry = 0;
};
}
