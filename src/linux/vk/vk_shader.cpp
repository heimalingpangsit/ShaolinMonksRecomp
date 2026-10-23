#include "linux/vk/vk_shader.h"

// the HLSL parts of glslang's interface are declared only with ENABLE_HLSL, which its build does not export
#ifndef ENABLE_HLSL
#define ENABLE_HLSL 1
#endif
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <mutex>

namespace vkshader {
namespace {

EShLanguage Language(Stage s) {
    switch (s) {
        case Stage::Vertex: return EShLangVertex;
        case Stage::Geometry: return EShLangGeometry;
        default: return EShLangFragment;
    }
}

void InitOnce() {
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
}

}  // namespace

bool Compile(const std::string& hlsl, const char* entry, Stage stage, std::vector<uint32_t>& spirv, std::string& error) {
    InitOnce();
    const EShLanguage lang = Language(stage);
    glslang::TShader shader(lang);
    const char* text = hlsl.c_str();
    const int length = int(hlsl.size());
    shader.setStringsWithLengths(&text, &length, 1);
    shader.setEntryPoint(entry);
    shader.setSourceEntryPoint(entry);
    shader.setEnvInput(glslang::EShSourceHlsl, lang, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    // the D3D registers become bindings through glslang's own register shifts
    shader.setHlslIoMapping(true);
    shader.setShiftBinding(glslang::EResUbo, kUboBase);
    shader.setShiftBinding(glslang::EResTexture, kTextureBase);
    shader.setShiftBinding(glslang::EResSampler, kSamplerBase);
    // stage inputs and outputs take locations in declaration order: every stage declares the same VOut
    shader.setAutoMapLocations(true);
    const auto messages =
        EShMessages(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl | EShMsgHlslOffsets);
    if (!shader.parse(GetDefaultResources(), 100, false, messages)) {
        error = shader.getInfoLog();
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages) || !program.mapIO()) {
        error = program.getInfoLog();
        return false;
    }
    glslang::SpvOptions options;
    options.disableOptimizer = true;
    options.validate = false;
    spirv.clear();
    glslang::GlslangToSpv(*program.getIntermediate(lang), spirv, &options);
    return !spirv.empty();
}

}  // namespace vkshader
