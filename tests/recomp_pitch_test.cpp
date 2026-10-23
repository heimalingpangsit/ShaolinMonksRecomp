// Exercise the production buffer method with synthetic voice metadata/stack.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "kernel/kernel.h"
namespace kernel { uint8_t* g_base = nullptr; }
struct TestVoice { uint32_t rate = 22050, freq = 12345; };
using namespace kernel;
#define BUFFER(name, nargs) static inline uint32_t Test_##name(TestVoice& v, const kernel::Args& a)
#include "audio_compat.inl"
#undef BUFFER

int main() {
    uint8_t stack[32]{};
    kernel::g_base = stack;
    X86Context ctx{};
    kernel::Args args{ctx};
    TestVoice voice;
    struct Case { int32_t pitch; uint32_t expected; };
    const Case cases[] = {{0,48000}, {-4096,24000}, {4096,96000}, {-4597,22050}};
    for (uint32_t format_rate : {8000u, 22050u, 44100u, 48000u}) {
        voice.rate = format_rate;
        for (auto c : cases) {
            kernel::W32(8, uint32_t(c.pitch));
            const auto hr = Test_SetPitch(voice, args);
            if (hr || std::abs(double(voice.freq) - c.expected) > 4) {
                std::fprintf(stderr, "pitch %d, format %u: got %u Hz, expected %u Hz\n", c.pitch, format_rate, voice.freq, c.expected);
                return 1;
            }
        }
    }
    const uint32_t before = voice.freq;
    kernel::W32(8, 0x7FFFFFFF);
    if (Test_SetPitch(voice,args) != 0x80070057u || voice.freq != before) return 1;
    std::puts("PASS: absolute Xbox pitch, octave ratios, 22.05 kHz case and atomic range rejection");
}
