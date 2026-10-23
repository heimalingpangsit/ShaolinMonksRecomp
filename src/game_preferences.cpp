#include "launcher.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"
#include "game_preferences.h"
#include <bit>

// The original text tables use en=0, fr=1, it=2, de=3, es=4 with RET 0.
X86_FUNC(sub_0010F0E0) {
    kernel::Return(ctx, shaolin::launcher::Language(), 0);
}

void shaolin::preferences::Apply(X86Context& ctx, uint8_t*) {
    static bool applied = false;
    const auto& settings = launcher::Current();
    if (applied || !settings.original_override) return;
    applied = true;
    const auto saved = ctx;
    const int volumes[] = {settings.music_volume, settings.effects_volume, settings.speech_volume};
    for (unsigned i = 0; i < 3; ++i) {
        const auto value = std::bit_cast<uint32_t>(volumes[i] / 100.f);
        kernel::W32(0x4DF5C4 + 4 * i, value);
        const auto category = kernel::CallGuest(ctx, 0x150E30, {i});
        kernel::CallGuest(ctx, 0x150DB0, {category, value});
    }
    const auto brightness = std::bit_cast<uint32_t>(settings.brightness / 100.f);
    kernel::W32(0x4DF5C0, brightness);
    kernel::CallGuest(ctx, 0x123A50, {brightness});
    kernel::G<uint8_t>(0x4DF5D1) = settings.versus_rounds == 1;
    kernel::G<uint8_t>(0x4DF5D2) = settings.versus_time == 120 ? 0 : settings.versus_time == 180 ? 1 : 2;
    kernel::G<uint8_t>(0x4DF5D3) = settings.camera_reverse;
    kernel::G<uint8_t>(0x599848) = settings.camera_reverse;
    ctx = saved;
    LOGR("Original options applied: music=%d effects=%d speech=%d brightness=%d rounds=%d time=%d camera_reverse=%d",
         settings.music_volume, settings.effects_volume, settings.speech_volume, settings.brightness,
         settings.versus_rounds, settings.versus_time, int(settings.camera_reverse));
}
