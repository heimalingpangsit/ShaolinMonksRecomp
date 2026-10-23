#include <cstdio>
#include <mutex>
#include <vector>
#include "gpu/state.h"

// Synthetic handles/services; compile the production SetRenderTarget body.
struct X86Context {};
struct Args { uint32_t values[2]; uint32_t operator[](int i) const { return values[i]; } };
std::recursive_mutex g_lock;
gpu::State g_state{};
constexpr uint32_t kDevRenderTarget = 0, kDevDepth = 1;
uint32_t device[2] = {1, 3};
std::vector<uint32_t> added, released;
uint32_t Dev(uint32_t slot) { return device[slot]; }
void SetDev(uint32_t slot, uint32_t value) { device[slot] = value; }
void AddRef(X86Context&, uint32_t h) { added.push_back(h); }
void Release(X86Context&, uint32_t h) { released.push_back(h); }
gpu::Surface SurfaceOf(uint32_t h) { return {.header=h}; }
namespace gpu {
void SurfaceSize(const Surface& s, uint32_t& w, uint32_t& h) {
    w = s.header == 2 ? 640 : 160; h = s.header == 2 ? 480 : 120;
}
}
bool updated = false;
void UpdateViewportConstants() { updated = true; }
#define HLE_STDCALL(name, nargs) uint32_t Test_##name(X86Context& ctx, const Args& a)
#include "set_render_target_test.inl"
#undef HLE_STDCALL

int main() {
    g_state.viewport = {0,0,160,120,0,1};
    g_state.scissor_count = 1;
    g_state.scissors[2] = 160; g_state.scissors[3] = 120;
    X86Context ctx;
    if (Test_D3DDevice_SetRenderTarget(ctx, Args{{2,0}})) return 1;
    if (g_state.viewport.w != 640 || g_state.viewport.h != 480 || g_state.scissor_count) {
        std::fprintf(stderr,"restored target: viewport=%ux%u scissor_count=%u (expected 640x480, no old scissor)\n",
                     g_state.viewport.w,g_state.viewport.h,g_state.scissor_count);
        return 1;
    }
    if (!updated || device[0] != 2 || device[1] != 0 || g_state.rt.header != 2 || g_state.zs.header != 0) return 1;
    if (added != std::vector<uint32_t>{2,0} || released != std::vector<uint32_t>{1,3}) return 1;
    std::puts("PASS: target restore clears temporary clipping, updates viewport and preserves resource ownership");
}
