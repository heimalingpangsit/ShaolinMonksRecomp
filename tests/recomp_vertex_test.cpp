// Synthetic data only: exercise the assembler used by the D3D12 backend.
#include <cstdio>
#include <cstring>
#include <memory>
#include "gpu/vertex.h"
#include "kernel/kernel.h"

namespace kernel { uint8_t* g_base = nullptr; }

int main() {
    if (!mem::Init()) return 2;
    kernel::g_base = mem::Base();
    const uint32_t data = mem::AllocContiguous(3 * 48, 0, 0xFFFFFFFF, 16);
    const uint32_t header = mem::AllocSystem(20);
    if (!data || !header) return 2;
    const float vertices[36] = {
        40, 60, .5f, 1, 1, 1, 1, 1, 0, 0, 0, 1,
        80, 60, .5f, 1, 1, 1, 1, 1, 1, 0, 0, 1,
        40, 90, .5f, 1, 1, 1, 1, 1, 0, 1, 0, 1};
    std::memcpy(kernel::HostPtr(data), vertices, sizeof(vertices));
    kernel::W32(header + 4, mem::PhysicalFromVirtual(data));
    gpu::State state{};
    state.vs = std::make_shared<gpu::VertexShader>();
    state.vs->inputs[0] = {0, 0, 0x42};
    state.vs->inputs[3] = {0, 16, 0x42};
    state.vs->inputs[9] = {0, 32, 0x42};
    state.vs->stream_stride[0] = 48;
    state.streams[0] = {header, 48};
    gpu::Assembled assembled;
    auto check = [&](uint32_t handle, bool screen, const char* label) {
        state.vs_handle = handle;
        if (!gpu::Assemble(state, 5, 0, 3, nullptr, assembled) || assembled.pretransformed != screen) {
            std::fprintf(stderr, "%s: expected screen=%d, got %d\n", label, screen, assembled.pretransformed);
            return false;
        }
        return assembled.vertex_count == 3 && assembled.regs == ((1u << 0) | (1u << 3) | (1u << 9));
    };
    // EndPrimitive preserves the source FVF bits and tags its synthetic FLOAT4
    // declaration with bit zero. The original 0x144 FVF means XYZRHW.
    if (!check(0x145, true, "immediate XYZRHW")) return 1;
    if (assembled.buffers[0].stride != 48 || std::memcmp(assembled.buffers[0].src, vertices, sizeof(vertices))) return 1;
    if (!check(0x143, false, "immediate XYZ")) return 1;
    if (!check(0xB0000001, false, "declaration-only shader")) return 1;
    state.vs->program = {0, 0, 0, 0};
    if (!check(0x145, false, "programmable shader")) return 1;
    mem::FreeSystem(header);
    mem::FreeContiguous(data);
    std::puts("PASS: immediate XYZRHW selects screen coordinates without changing vertex payload; XYZ/programs retain their pipeline");
}
