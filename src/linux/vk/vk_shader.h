#pragma once
// HLSL to SPIR-V with glslang's HLSL front end. The renderer's shaders keep
// their D3D registers; they map to the one descriptor set every pipeline uses:
//   b0, b1        -> bindings 0, 1   (uniform buffers)
//   t0 .. t3      -> bindings 2 .. 5 (sampled images)
//   s0 .. s3      -> bindings 6 .. 9 (samplers)
#include <cstdint>
#include <string>
#include <vector>

namespace vkshader {

enum class Stage { Vertex, Geometry, Fragment };

constexpr uint32_t kUboBase = 0;
constexpr uint32_t kTextureBase = 2;
constexpr uint32_t kSamplerBase = 6;

// false with the compiler's messages in error
bool Compile(const std::string& hlsl, const char* entry, Stage stage, std::vector<uint32_t>& spirv, std::string& error);

}  // namespace vkshader
